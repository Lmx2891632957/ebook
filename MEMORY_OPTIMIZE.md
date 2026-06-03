# 内存优化方案

## 目录

1. [内存布局现状](#1)
2. [问题诊断：栈溢出](#2)
3. [次要问题：跳转时的不必要重建](#3)
4. [优化方案](#4)
   - [方案 1：增大栈空间](#4.1)
   - [方案 2：消除跳转时的不必要重建](#4.2)
   - [方案 3：将大结构体从栈移到堆](#4.3)
   - [方案 4：拆分 `ebook_play` 函数](#4.4)
5. [建议实施优先级](#5)

---

<h2 id="1">一、内存布局现状</h2>

**硬件**: STM32F103RC，内部 SRAM 共 48KB (0x20000000 ~ 0x2000C000)

**当前内存布局**:

```
+------------------------------------------------------+
| 自定义内存池 (gui_memin_malloc -> mymalloc)             |
|   - 池大小:   36KB (36 x 1024 = 36,864 bytes)         |
|   - 管理表:   1,152 entries x 2 bytes = 2,304 bytes    |
|   - 块大小:   32 bytes/block                          |
|   - 合计占用: ~38.2KB                                  |
|   - 分配算法:  从末尾向前搜索首个适配 (first-fit)       |
|   - 空闲合并:  隐式合并（位图归零后自然连续）           |
+------------------------------------------------------+
| 栈 (STACK):    0x400  = 1,024 bytes                   |
| 标准堆 (HEAP):  0 bytes（未启用）                       |
| 其他全局变量 / C 运行时数据: ~8.8KB                     |
+------------------------------------------------------+
```

**关键文件**:

| 文件 | 作用 |
|------|------|
| [startup_stm32f103xe.s](Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/arm/startup_stm32f103xe.s#L33) | 定义 `Stack_Size = 0x400` (1KB) |
| [malloc.h](Middlewares/MALLOC/malloc.h#L49-L51) | 定义 `MEM1_MAX_SIZE = 36 * 1024` (36KB) |
| [malloc.c](Middlewares/MALLOC/malloc.c#L29) | `mem1base[36864]` - 内存池静态数组 |

---

<h2 id="2">二、问题诊断：栈溢出（Stack Overflow）</h2>

### 2.1 现象

执行书签跳转后，**不定时**出现 LED1 闪烁，程序崩溃。

- LED1 闪烁 = `HardFault_Handler` 被触发
- "不定时" = 取决于文本内容、中断时机、书签数量等因素

### 2.2 根本原因

**栈空间仅 1,024 字节，而书签跳转时的调用链极深，峰值栈使用量超过 1,300 字节。**

### 2.3 书签跳转调用栈深度分析

```
 ebook_play()                           ~300B   (ctx_data 214B + 各种指针/标志)
   +-- 阅读状态 (ebooksta == 1)
   +-- 书签按钮 -> Panel 1 代码块         ~86B    (bm_item_names[10] 40B + 位置变量)
   +-- listbox 双击 -> Panel 2 代码块     ~73B    (p2_info[32] 32B + 位置变量)
   +-- Panel 2 模态循环                  ~90B    (tp_dev 扫描 + in_obj 处理)
   |
   +-- [用户点击"跳转到此处"]
   |
   +-- ebook_scan_page_forward()         ~24B
   |   +-- f_lseek()                    ~120B   (FATFS 扇区缓冲 + 簇链遍历)
   |   +-- f_read()                     ~120B   (FATFS 磁盘读取逻辑)
   |
   +-- ebook_draw_page()                 ~20B
   |   +-- gui_fill_rectangle()         ~30B
   |   +-- f_lseek() + f_read()         ~240B   (再次读取 + 渲染)
   |   +-- gui_show_string()            ~100B   (GBK 排版引擎)
   |
   +-- ebook_show_page_num()             ~30B
   |   +-- gui_show_string()            ~60B
   |
   +-- [可能的 ISR 压栈]                 ~32B+   (SysTick / SDIO / 触摸中断)
   ----------------------------------------------------
    估算总和:                           ~1,305B+
```

> **1,305B > 1,024B** -- 栈使用超过分配上限约 **30%**

### 2.4 为什么"有时候"崩溃？

| 变量因素 | 影响 |
|----------|------|
| **文本内容不同** | `gui_show_string` 的 GBK 排版路径不同，栈使用量轻微浮动 |
| **中断时机** | Cortex-M3 自动压栈 8 个寄存器（32B）。如果 SysTick/SDIO 中断恰好在最深调用时触发，成为压垮栈的最后一根稻草 |
| **书签数量** | 书签越多，listbox 重建时的清理/分配循环越深 |
| **FATFS 内部路径** | 跨簇读取 vs 簇内读取，栈使用量不同 |

### 2.5 崩溃链

```
栈溢出 -> 覆盖 .data 段 或 内存池管理表 (mem1mapbase)
       -> mymalloc / myfree 操作到损坏的元数据
       -> BusFault / MemManage
       -> 升级为 HardFault
       -> LED1 闪烁
```

---

<h2 id="3">三、次要问题：跳转时的不必要重建</h2>

### 3.1 问题描述

在 [ebook.c:1082-1094](User/APP/ebook.c#L1082-L1094)，跳转处理中设置了：

```c
p2_run = 0;           // 退出 Panel 2 循环
bm_panel1_run = 0;    // 退出 Panel 1 循环
```

然而，Panel 2 的清理代码（第 1121-1172 行）仍然无条件执行了一次完整的 Panel 1 列表重建：

```
Panel 2 清理代码:
  (1) 释放旧的 listbox + 所有书签名称
  (2) 创建全新的 listbox + scrollbar
  (3) 为每个书签分配新名称 (32B x N)
  (4) 为每个书签分配列表节点 (16B x N)
  (5) 执行 listbox_draw_listbox (LCD 绘制操作)
       |
       v
Panel 1 循环退出 (bm_panel1_run == 0)
       |
       v
Panel 1 清理代码:
  (6) 释放全新的 listbox（步骤2创建的）
  (7) 释放所有新分配的名称（步骤3创建的）
```

步骤 (2)~(5) 在跳转场景下**完全浪费** -- 创建了只在内存中存在几毫秒就立即被释放的 UI 组件。

### 3.2 代价

| 影响 | 详情 |
|------|------|
| 不必要的内存分配 | ~400 bytes 临时分配（listbox + scrollbar + names + list items） |
| 不必要的栈深度 | `listbox_draw_listbox` -> `gui_show_ptstr` -> LCD 绘制链 ~100B 栈 |
| 内存碎片 | 分配后立即释放的 alloc/free 循环 |

---

<h2 id="4">四、优化方案</h2>

<h3 id="4.1">方案 1：增大栈空间（最推荐）</h3>

**修改文件**: [startup_stm32f103xe.s](Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/arm/startup_stm32f103xe.s#L33)

```asm
; 当前 (第 33 行)
Stack_Size      EQU     0x00000400    ; 1KB -> 1024 bytes

; 修改为
Stack_Size      EQU     0x00000800    ; 2KB -> 2048 bytes
```

**配套修改**: [malloc.h](Middlewares/MALLOC/malloc.h#L50) 内存池相应缩小 1KB

```c
// 当前 (第 50 行)
#define MEM1_MAX_SIZE   36 * 1024     // 36KB

// 修改为
#define MEM1_MAX_SIZE   35 * 1024     // 35KB (释放 1KB 给栈)
```

**内存布局调整后**:

```
+--------------------------------------+
| 内存池:    35KB (35,840 bytes)        |
| 管理表:    1,120 x 2 = 2,240 bytes   |
| 栈:        2KB (2,048 bytes)         |  <-- 增大一倍
| 其他:      ~8.8KB                     |
| 总计:      48KB                       |
+--------------------------------------+
```

**优点**:
- 改动量极小（2 行代码）
- 从根本上解决栈溢出问题
- 35KB 内存池对电子书阅读器绰绰有余（峰值使用量通常 < 20KB）
- 给未来更深调用链预留安全边际

**缺点**:
- 无

---

<h3 id="4.2">方案 2：消除跳转时的不必要重建</h3>

**修改文件**: [ebook.c](User/APP/ebook.c) -- Panel 2 清理代码（约第 1121-1172 行）

**逻辑变更**:

```
当前逻辑:
  Panel 2 退出 -> 无条件重建 Panel 1 列表

修改为:
  Panel 2 退出 -> 检查 bm_panel1_run:
    +-- 如果 == 1: 重建列表（删除/取消后需要继续显示 Panel 1）
    +-- 如果 == 0: 跳过重建（跳转后 Panel 1 也即将退出，无需重建）
```

**伪代码**:

```c
/* 只有当 Panel 1 还要继续运行时才重建 listbox */
if (bm_panel1_run)
{
    /* 重建 listbox（删除或取消后返回 Panel 1） */
    if (bm_lb) { /* 释放旧列表 */ }
    bm_lb = listbox_creat(...);
    /* 填充新列表 */
}
/* 否则（跳转后）跳过重建，Panel 1 清理代码会直接释放资源 */
```

**优点**:
- 跳转时消除约 400B 不必要的内存分配
- 跳转时削减约 100B 调用栈深度
- 减少内存碎片

**缺点**:
- 需增加一个条件判断

---

<h3 id="4.3">方案 3：将大结构体从栈移到堆</h3>

**修改文件**: [ebook.c](User/APP/ebook.c) -- `ebook_play` 函数

**当前代码** (第 404 行):

```c
uint8_t ebook_play(void)
{
    ebook_ctx_t ctx_data;       // 约 214 字节，在栈上
    ebook_ctx_t *ctx = &ctx_data;
```

**修改为**:

```c
uint8_t ebook_play(void)
{
    ebook_ctx_t *ctx;
    ctx = (ebook_ctx_t *)gui_memin_malloc(sizeof(ebook_ctx_t));
    if (!ctx) return 1;
```

同时在所有退出路径确保 `gui_memin_free(ctx)`：

```c
/* 正常退出路径 */
gui_memin_free(ctx);
return rval;

/* 错误退出路径 */
gui_memin_free(ctx);
break;
```

**优点**:
- 从栈上削减约 214 字节
- `ctx` 是函数中最重的局部变量

**缺点**:
- 需要审查所有 `return`/`break` 路径确保不泄漏
- 约 15 行改动

---

<h3 id="4.4">方案 4：拆分 `ebook_play` 函数</h3>

**修改文件**: [ebook.c](User/APP/ebook.c)

**当前结构**:

```
ebook_play()  (约 1300 行，单一函数)
  +-- 文件浏览状态 (State 0)
  +-- 阅读状态 (State 1)
  |   +-- 字体选择面板
  |   +-- 书签管理面板 (Panel 1)
  |   |   +-- 书签操作面板 (Panel 2)
  |   +-- 滑动翻页
  +-- 清理
```

**建议拆分**:

```
ebook_play()                     (~50 行，入口 + 分发)
  +-- ebook_file_browser()       (~80 行，State 0)
  +-- ebook_reading_loop()       (~100 行，State 1 主循环)
  |   +-- ebook_font_panel()     (~70 行)
  |   +-- ebook_bookmark_panel() (~200 行, Panel 1 + Panel 2)
  |   +-- ebook_swipe_handler()  (~30 行)
  +-- ebook_cleanup()            (~30 行)
```

**优点**:
- 编译器可以为不同函数复用栈空间（函数返回后栈帧回收）
- 极大降低任意时刻的峰值栈深度
- 代码可维护性显著提升

**缺点**:
- 改动量大（约 100+ 行）
- 需要仔细处理 `ctx`、文件句柄等共享状态的传递

---

<h2 id="5">五、建议实施优先级</h2>

| 优先级 | 方案 | 改动量 | 预期栈削减 | 说明 |
|--------|------|--------|-----------|------|
| **P0** | 方案 1: 增大栈 + 缩小池 | 2 行 | 栈容量 +1KB | 根本解决，零风险 |
| **P1** | 方案 2: 跳过不必要重建 | ~8 行 | ~100B | 消除浪费的分配/绘制 |
| **P2** | 方案 3: `ctx` 移到堆 | ~15 行 | ~214B | 削减最大局部变量 |
| **P3** | 方案 4: 拆分函数 | ~100 行 | ~200B+ | 长期架构优化 |

### 推荐组合

**P0 + P1** 是最小改动量的高效组合：

| | 改动 | 效果 |
|--|------|------|
| P0 | 栈 1KB -> 2KB，池 36KB -> 35KB | 栈容量翻倍，从根本上消除溢出 |
| P1 | 跳转时跳过不必要的 listbox 重建 | 削减峰值栈 ~100B + 避免无用内存分配 |

两项合计约 **10 行改动**，预期将峰值栈使用量从 ~1,300B 降至安全范围（栈 2,048B 远超需求），彻底消除间歇性 HardFault。

---

> **文档版本**: 1.0
> **创建日期**: 2026-06-03
> **适用硬件**: STM32F103RC (ALIENTEK Explorer)
> **适用分支**: main
