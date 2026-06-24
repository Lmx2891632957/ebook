# 分段读取实现文档

> FenDuanDuQu.md --- 电子书阅读器核心技术详解

---

## 目录

1. [问题背景](#1-问题背景)
2. [整体架构](#2-整体架构)
3. [核心数据结构](#3-核心数据结构)
4. [分段读取：page_start 与 page_end](#4-分段读取page_start-与-page_end)
5. [排版引擎：ebook_scan_page_forward() 详解](#5-排版引擎ebook_scan_page_forward-详解)
6. [GBK 编码处理](#6-gbk-编码处理)
7. [GBK 边界对齐：ebook_align_gbk()](#7-gbk-边界对齐ebook_align_gbk)
8. [翻页与历史栈](#8-翻页与历史栈)
9. [页面渲染：ebook_draw_page()](#9-页面渲染ebook_draw_page)
10. [完整流程时序](#10-完整流程时序)
11. [总结](#11-总结)

---

## 1. 问题背景

### 内存约束

| 参数 | 数值 |
|------|------|
| MCU | STM32F103RCT6 |
| SRAM 总量 | 48 KB |
| 系统占用 (UCOS-II + FATFS + GUI框架 + 栈) | ~42 KB |
| 可用于文本缓冲的空间 | ~6 KB |

### 直接全部读入的问题

TXT 电子书文件动辄 500KB ~ 2MB，远超可用 SRAM。如果使用 `f_read` 一次性将整个文件读入内存：

```
f_read(&file, big_buffer, file.objsize, &bread);  // 需要 500KB+ 内存
```

内存直接溢出，系统崩溃。

### 解决方案：分段读取

只将**当前屏幕能显示的那一页内容**读入渲染缓冲区（~4KB），其余数据留在 SD 卡上，用到时再通过 `f_lseek` + `f_read` 按需读取。

```
+-------------------------------------------------------+
|                 SD 卡上的 TXT 文件 (~1MB)                |
|  +--------+--------+--------+--------+--------+-----+  |
|  | 页 1   | 页 2   | 页 3   | ...    | 页 N-1 | 页 N |  |
|  +----+-----+----+-----+----+--------+----+-----+--+  |
|       |          |           |             |           |
|       v          v           v             v           |
|    f_lseek + f_read 4KB chunk -> SRAM page_buf -> LCD |
|                                                        |
|   每次只读一页（~1-2KB），缓冲区仅需 4KB                   |
+-------------------------------------------------------+
```

---

## 2. 整体架构

分段读取的核心思想是：**不存文本内容，只存文件偏移量**。

阅读器状态中不保存当前页的完整文本，只保存两个偏移量：
- `page_start`：当前页在文件中的起始字节位置
- `page_end`：当前页在文件中的结束字节位置

翻页时：
- **向前翻页**：`page_start = page_end`，重新扫描算出新的 `page_end`
- **向后翻页**：从历史栈中取出之前的 `page_start`，重新扫描算出 `page_end`

```
                       SD 卡 TXT 文件
                +---------------------------+
       offset 0 |  "第一章 引言..."          |
                |  ...                      |
   page_start ->|  "第三段文字内容..."        |----+
                |  ...                      |    | ebook_scan_page_forward()
   page_end --->|  "第四段开头..."            |----+ 扫描到此处
                |  ...                      |
                |  "全文完"                  |
                +---------------------------+

       历史栈存储了之前每个 page_start 的 offset，
       向后翻页时从栈中取出即可。
```

---

## 3. 核心数据结构

```c
// ebook.h

#define EBOOK_RAW_BUF_SIZE  4096    // 文件扫描缓冲区（4KB）
#define EBOOK_PREV_MAX      40      // 历史栈最大容量
#define EBOOK_PAGE_BUF_SIZE 4096    // 页面渲染缓冲区（4KB）

typedef struct {
    FIL     *file;                   // FATFS 文件句柄（堆分配）
    uint32_t file_size;              // 文件总大小（字节）
    uint32_t page_start;             // 当前页在文件中的起始偏移
    uint32_t page_end;               // 当前页在文件中的结束偏移
    uint32_t page_num;               // 1-based 当前页码

    uint16_t text_width;             // 文本显示区域像素宽度
    uint16_t text_height;            // 文本显示区域像素高度
    uint16_t text_x;                 // 文本区域左上角 X
    uint16_t text_y;                 // 文本区域左上角 Y
    uint8_t  font_size;              // 字号：12 / 16 / 24
    uint8_t  bg_mode;                // 背景模式：0=白, 1=护眼黄

    /* ===== 历史栈：用于向后翻页 ===== */
    uint32_t prev_starts[EBOOK_PREV_MAX];  // 环形队列，存历史 page_start
    uint8_t  prev_count;                   // 当前有效条目数

    uint8_t *raw_buf;                // 4KB 扫描缓冲区（堆分配）
    uint8_t *page_buf;               // 4KB 渲染缓冲区（堆分配）
    uint8_t *path;                   // 文件完整路径
    uint8_t *fname;                  // 显示用的文件名
} ebook_ctx_t;
```

### 为什么要堆分配？

STM32F103RCT6 的栈空间极其有限（UCOS-II 默认给 `main_task` 分配了 256 字节栈）。如果把 `ebook_ctx_t` 放在栈上（~216 字节），加上 FIL 对象（~544 字节），一个函数调用就会用掉 > 700 字节栈，极易栈溢出。

```c
// 错误做法：全部放栈上
ebook_ctx_t ctx;       // 216 字节栈变量
FIL        f_txt;       // 544 字节栈变量  <- 栈溢出！

// 正确做法：大对象堆分配
ebook_ctx_t *ctx = gui_memin_malloc(sizeof(ebook_ctx_t));  // 216B 在堆上
FIL         *f_txt = gui_memin_malloc(sizeof(FIL));         // 544B 在堆上
```

---

## 4. 分段读取：page_start 与 page_end

### 核心概念

电子书的每一页不是一个"预先切好的文件块"，而是**由排版引擎实时计算出来的**。

```
给定：page_start（文件中的起始偏移）
通过：逐字节扫描文件内容，模拟 LCD 上的排版布局
得到：page_end（刚好填满一屏时的结束偏移）
```

### 为什么不能预先计算总页数？

因为字体大小可变（12/16/24）、屏幕宽度固定（~316px 文本区），每页的实际字节数取决于：
- 中文字符数量（中文字宽 = font_size，占 2 字节）
- 英文字符数量（英文字宽 = font_size/2，占 1 字节）
- 换行符数量
- 自动换行次数

同一个文件，12px 字体可能有 200 页，16px 字体只有 100 页——完全由排版决定。

---

## 5. 排版引擎：ebook_scan_page_forward() 详解

这是整个电子书最核心的函数。它的任务是从 `page_start` 开始，逐字节扫描文件内容，按照 LCD 排版规则模拟字符放置，直到填满一屏，记录下此时的 `page_end`。

### 5.1 排版规则

```
规则 1：GBK 中文字符（首字节 >= 0x81 且第二字节 >= 0x40）
        -> 字符宽度 = font_size，占 2 字节

规则 2：ASCII 字符（byte < 0x81）
        -> 字符宽度 = font_size / 2，占 1 字节

规则 3：遇到 CR+LF (\r\n) 或 LF (\n)
        -> 强制换行

规则 4：当前行 x 坐标超过文本区右边界
        -> 自动换行（word-wrap）

规则 5：当前行 y 坐标超过文本区下边界 max_y
        -> 页面已满，当前字符的 offset 即为 page_end
```

### 5.2 伪代码

```
ebook_scan_page_forward(ctx):
    x = text_x, y = text_y
    file_pos = page_start

    while file_pos < file_size:
        // 1. 从 SD 卡读取一个 chunk（4KB）
        f_lseek(ctx->file, file_pos)
        f_read(ctx->file, raw_buf, 4096, &bread)

        // 2. 逐字节解析 raw_buf
        for each byte in raw_buf:

            // 3. 判断字符类型
            if byte == \r\n or \n:
                y += font_size    // 强制换行
                if y > max_y: 记录 page_end; 返回

            else if byte >= 0x81:  // GBK 首字节
                读取下一字节
                如果构成合法 GBK：
                    if x + font_size 放不下: 自动换行
                    if y > max_y: 记录 page_end; 返回
                    x += font_size; 跳过 2 字节

            else:  // ASCII
                if x + font_size/2 放不下: 自动换行
                if y > max_y: 记录 page_end; 返回
                x += font_size/2; 跳过 1 字节

        file_pos += bread

    // 文件读完
    page_end = file_size
```

### 5.3 关键设计：跨 chunk 的 GBK 处理

`raw_buf` 只有 4KB，一个 GBK 中文字的两字节可能被分割到两个连续的 chunk 中。`pending_gbk` 变量处理这种情况。

```
chunk N:       ... [0x81] [0x40] [0x41] ...
                       ^
                  GBK lead byte

chunk N+1:     [0x82] [0x83] ...
                ^
          可能是 chunk N 中 GBK 的 trail byte

处理方式：
  1. 在 chunk N 末尾，如果有孤立的首字节 (>=0x81)，设置 pending_gbk = 1
  2. 在 chunk N+1 开头，先检查 pending_gbk：
     - 如果 chunk_N_last_byte 是首字节，chunk_N+1_first_byte 是合法的尾字节
       -> 合并为一个 GBK 字符，正常排版
     - 否则 -> 各自作为 ASCII 处理
```

代码实现（`ebook.c:378-417`）：

```c
// --- Handle GBK lead byte pending from previous chunk ---
if (pending_gbk)
{
    // pending byte 是上一个 chunk 的最后一个字节
    // raw_buf[0] 可能是它的配对尾字节
    if (ctx->raw_buf[0] >= 0x40)
    {
        // 合法 GBK 双字节字符 -> 排版一个中文字
        if (x + font > endx + 1) { y += font; x = text_x; }  // 自动换行
        if (y > max_y) { page_end = file_pos - 1; return; }   // 页面满
        x += font;
        i = 1;  // 消耗了新 chunk 的第一个字节
    }
    else
    {
        // 不是合法 GBK -> pending byte 当 ASCII 单独处理
        if (x + font / 2 > endx + 1) { y += font; x = text_x; }
        if (y > max_y) { page_end = file_pos - 1; return; }
        x += font / 2;
        // i = 0，raw_buf[0] 在本轮循环中继续处理
    }
    pending_gbk = 0;
}

// ... 后续正常循环 ...

// GBK lead byte at end of chunk:
if (byte >= 0x81)
{
    if (i + 1 < bread)
    {
        // 第二字节在当前 chunk 内 -> 正常处理
        if (raw_buf[i + 1] >= 0x40) { /* 排版 GBK 字符 */ }
    }
    else
    {
        // 这是 chunk 的最后一个字节，且是 GBK 首字节
        // -> 挂起，等下一个 chunk 联合处理
        pending_gbk = 1;
        i++;
    }
}
```

---

## 6. GBK 编码处理

### 6.1 GBK 编码规则

| 类型 | 字节范围 | 字节数 | 说明 |
|------|----------|--------|------|
| ASCII | 0x00 - 0x7F | 1 | 英文、数字、符号 |
| GBK 中文 | 首字节 0x81-0xFE，尾字节 0x40-0xFE | 2 | 简体中文、GB2312 全集 |
| 扩展区 | 首字节 0x81-0xFE，尾字节 0x40-0xFE | 2 | 罕见字、繁体 |

**检测算法**：

```c
if (byte >= 0x81)                    // 可能是 GBK 首字节
{
    if (next_byte >= 0x40)           // 合法的尾字节
    {
        // 这是一个 GBK 中文字符（2 字节）
    }
    else
    {
        // 不是合法 GBK，当 ASCII 处理（1 字节）
    }
}
else
{
    // ASCII 字符（1 字节）
}
```

### 6.2 为什么不能简单按字节切分

如果从文件中间某个位置开始读取，可能正好落在 GBK 中文字符的第二个字节上：

```
文件内容：  [0x41] [0xB0] [0xA1] [0x42]
               A      中     国     B
                       ^
                 "中" = 0xB0A1

从 offset=2 (0xA1) 开始读取：
  读到 0xA1, 0x42 -> 0xA1 >= 0x81？
    是 -> 检查下一字节 0x42 >= 0x40？
      是 -> 把 0xA142 当作"中"的另外字符，显示为乱码！
```

这就是为什么需要 `ebook_align_gbk()` 函数。

---

## 7. GBK 边界对齐：ebook_align_gbk()

### 7.1 使用场景

当用户拖动进度条跳转到文件的任意位置时，`prgb->curpos` 可能在 GBK 汉字的中间。直接 `f_lseek` 到这个位置会导致当前页全部乱码。

### 7.2 算法

```c
static uint32_t ebook_align_gbk(ebook_ctx_t *ctx, uint32_t offset)
{
    if (offset == 0 || offset >= ctx->file_size) return offset;

    uint8_t buf[2];
    UINT    br;

    while (offset > 0)
    {
        // 读取 offset-1 和 offset 两个字节
        f_lseek(ctx->file, offset - 1);
        f_read(ctx->file, buf, 2, &br);

        // 如果 buf[0] 是 GBK 首字节，buf[1] 是合法尾字节
        // -> offset 恰好落在 GBK 字符的第二字节上
        // -> 向后退 1 字节
        if (br >= 2 && buf[0] >= 0x81 && buf[1] >= 0x40)
        {
            offset--;   // 退回到首字节
        }
        else
        {
            break;      // 已经是安全位置
        }
    }

    return offset;
}
```

### 7.3 图解

```
文件位置:  ... 0xB0  0xA1  0x42  0x43 ...
               中字首  中字尾   B     C
               ^       ^
             offset-1  offset (用户拖到此处)

读取 offset-1 开始的两个字节: [0xB0, 0xA1]
  0xB0 >= 0x81 ? 是
  0xA1 >= 0x40 ? 是
  -> offset 落在 "中" 的第二字节上，不安全

offset-- (向后退 1 字节)，现在 offset 指向 0xB0（"中"的首字节）

再次读取: [上一个字节, 0xB0]
  上一个字节 >= 0x81 且 0xB0 >= 0x40 ?
  假设不是 -> break

返回 aligned_offset = 原来的 offset - 1
安全边界
```

---

## 8. 翻页与历史栈

### 8.1 翻页原理

翻页的关键在于：**不需要重新扫描整个文件**。

```c
static uint8_t ebook_turn_page(ebook_ctx_t *ctx, int dir)
{
    if (dir > 0)  // 向前翻页
    {
        // 1. 将当前 page_start 压入历史栈
        ebook_history_push(ctx);

        // 2. 新页的起始 = 当前页的结束
        ctx->page_start = ctx->page_end;

        // 3. 页码 +1
        ctx->page_num++;
    }
    else if (dir < 0)  // 向后翻页
    {
        // 1. 从历史栈中取出之前的 page_start
        ctx->page_start = ebook_history_pop(ctx);

        // 2. 页码 -1
        if (ctx->page_num > 1) ctx->page_num--;
    }

    // 3. 重新扫描当前页（计算 page_end）
    ebook_scan_page_forward(ctx);

    // 4. 渲染到屏幕
    ebook_draw_page(ctx);

    // 5. 更新顶部页码显示
    ebook_show_page_num(ctx);
}
```

### 8.2 历史栈实现

```
                   prev_starts[40] 数组
              +-----------------------------+
     push --> | [0]偏移0    <- 第1页        |--> pop
              | [1]偏移512  <- 第2页        |
              | [2]偏移1024 <- 第3页        |
              | [3]偏移1536 <- 当前页的前一页 |
              | ...                        |
              | prev_count = 4             |
              +-----------------------------+
              最多 40 条，超出后滑动窗口丢弃最旧条目
```

**压栈**（`ebook.c:268-285`）：

```c
static void ebook_history_push(ebook_ctx_t *ctx)
{
    if (ctx->prev_count < EBOOK_PREV_MAX)  // 40
    {
        // 正常追加
        ctx->prev_starts[ctx->prev_count] = ctx->page_start;
        ctx->prev_count++;
    }
    else
    {
        // 栈满：丢弃最旧的（索引0），所有元素左移，新值放末尾
        for (i = 0; i < EBOOK_PREV_MAX - 1; i++)
            ctx->prev_starts[i] = ctx->prev_starts[i + 1];
        ctx->prev_starts[EBOOK_PREV_MAX - 1] = ctx->page_start;
    }
}
```

**弹栈**（`ebook.c:292-297`）：

```c
static uint32_t ebook_history_pop(ebook_ctx_t *ctx)
{
    if (ctx->prev_count == 0) return 0;  // 历史为空
    ctx->prev_count--;
    return ctx->prev_starts[ctx->prev_count];  // 返回最顶部的
}
```

### 8.3 翻页的完整时序

```
用户左滑（向前翻一页）

  ebook_turn_page(ctx, +1)
    |
    +-- 1. ebook_history_push(ctx)
    |      将当前 page_start=512 存入 prev_starts[prev_count]
    |      prev_count: 1 -> 2
    |
    +-- 2. ctx->page_start = ctx->page_end
    |      page_start = 1024 (上一页的结束位置)
    |      page_num: 1 -> 2
    |
    +-- 3. ebook_scan_page_forward(ctx)
    |      从 offset=1024 开始扫描
    |      逐字节排版，填满屏幕
    |      page_end = 1536
    |
    +-- 4. ebook_draw_page(ctx)
    |      f_lseek(file, 1024)
    |      f_read(file, page_buf, 1536-1024)  = 读 512 字节
    |      gui_show_string() -> 渲染到 LCD
    |
    +-- 5. ebook_show_page_num(ctx)
           LCD 顶部显示 "2"
```

```
用户右滑（向后翻一页）

  ebook_turn_page(ctx, -1)
    |
    +-- 1. ebook_history_pop(ctx)
    |      prev_count: 2 -> 1
    |      返回 prev_starts[1] = 512
    |      page_start = 512
    |
    +-- 2. page_num: 2 -> 1
    |
    +-- 3. ebook_scan_page_forward(ctx)
    |      从 offset=512 开始扫描
    |      page_end = 1024
    |
    +-- 4. ebook_draw_page(ctx)
    |      渲染 offset 512~1024
    |
    +-- 5. ebook_show_page_num(ctx)
           LCD 顶部显示 "1"
```

### 8.4 跳转时清空历史栈

书签跳转、进度条拖动跳转都会清除历史栈（`ctx->prev_count = 0`），因为跳转后的"上一页"已经没有语义。

---

## 9. 页面渲染：ebook_draw_page()

扫描完成后，`page_start` 和 `page_end` 已经确定，将这一段文件内容读取到 `page_buf` 中，交由 GUI 框架渲染。

```c
static void ebook_draw_page(ebook_ctx_t *ctx)
{
    uint32_t page_size = ctx->page_end - ctx->page_start;

    // 1. 清除整个文本区域（用当前背景色填充）
    uint16_t bg_color = (ctx->bg_mode == 0) ? WHITE : EYECARE_BG;
    gui_fill_rectangle(0, top_bar_bottom,
                       screen_width, text_area_height,
                       bg_color);

    // 2. 从 SD 卡读取当前页的所有字节
    if (page_size > 0 && page_size < EBOOK_PAGE_BUF_SIZE)  // < 4KB
    {
        f_lseek(ctx->file, ctx->page_start);     // 定位到页起始
        f_read(ctx->file, ctx->page_buf, page_size, &bread);  // 读到 page_buf
        ctx->page_buf[bread] = '\0';              // 以 '\0' 结尾

        // 3. 调用 GUI 框架渲染 GBK 文本到 LCD
        gui_show_string(ctx->page_buf,
                        ctx->text_x, ctx->text_y,
                        ctx->text_width, ctx->text_height,
                        ctx->font_size, BLACK);
    }

    // 4. 重绘进度条（被步骤 1 的填充覆盖了）
    if (g_ebook_prgb && ctx->file_size > 0)
    {
        g_ebook_prgb->curpos = ctx->page_start;
        progressbar_draw_progressbar(g_ebook_prgb);
    }
}
```

---

## 10. 完整流程时序

### 10.1 打开一本书

```
用户双击选中书籍
  |
  +-- f_open(f_txt, path, FA_READ)          打开文件
  |
  +-- ebook_ctx_init(ctx, f_txt, ...)        初始化上下文
  |   +-- ctx->file_size = f_txt->obj.objsize
  |   +-- ctx->page_start = 0               <- 从文件头开始
  |   +-- ctx->page_num = 1
  |   +-- ctx->raw_buf  = malloc(4KB)       分配扫描缓冲区
  |   +-- ctx->page_buf = malloc(4KB)       分配渲染缓冲区
  |
  +-- ebook_scan_page_forward(ctx)          扫描第一页
  |   +-- page_end = 1024                   <- 第一页在 offset 1024 结束
  |
  +-- ebook_draw_page(ctx)                  渲染第一页
  |   +-- f_lseek(file, 0)
  |   +-- f_read(file, page_buf, 1024)      读 offset 0~1024 到内存
  |   +-- gui_show_string()                 -> LCD
  |
  +-- ebook_show_page_num(ctx)              LCD 顶部显示 "1"
```

### 10.2 连续翻 3 页的过程

```
初始状态: page_start=0, page_end=1024, prev_count=0

---------- 翻到第 2 页 ----------
push: prev_starts[0]=0, prev_count=1
page_start = 1024, page_num = 2
scan:  从 1024 扫描，page_end = 2048
draw:  渲染 offset 1024~2048

---------- 翻到第 3 页 ----------
push: prev_starts[1]=1024, prev_count=2
page_start = 2048, page_num = 3
scan:  从 2048 扫描，page_end = 3072
draw:  渲染 offset 2048~3072

---------- 翻到第 4 页 ----------
push: prev_starts[2]=2048, prev_count=3
page_start = 3072, page_num = 4
scan:  从 3072 扫描，page_end = 4096
draw:  渲染 offset 3072~4096

此时历史栈:
  prev_starts[0] = 0     (第 1 页)
  prev_starts[1] = 1024  (第 2 页)
  prev_starts[2] = 2048  (第 3 页)
  prev_count = 3
```

### 10.3 回退到第 2 页

```
---------- 退回第 3 页 ----------
pop:  prev_starts[2]=2048, prev_count=2
page_start = 2048, page_num = 3
scan: 从 2048 扫描，page_end = 3072
draw: 渲染 offset 2048~3072

---------- 退回第 2 页 ----------
pop:  prev_starts[1]=1024, prev_count=1
page_start = 1024, page_num = 2
scan: 从 1024 扫描，page_end = 2048
draw: 渲染 offset 1024~2048
```

---

## 11. 总结

### 核心设计思想

| 设计决策 | 原因 |
|----------|------|
| 不存文本内容，只存文件偏移量 | 文本在 SD 卡上，内存只放两个 uint32_t offset |
| page_start / page_end 由排版引擎实时计算 | 字号可变，每页字节数不固定 |
| raw_buf 只做扫描，page_buf 只做渲染 | 职责分离，缓冲区可复用 |
| pending_gbk 跨 chunk 处理 | 4KB chunk 边界可能切断 GBK 双字节 |
| ebook_align_gbk() 跳转前对齐 | 防止从汉字中间跳转导致乱码 |
| 历史栈只存 offset（不存文本） | 40 x 4 字节 = 仅 160 字节的向前翻页能力 |
| 全部大对象堆分配 | 避免在 48KB SRAM / 256B 任务栈上溢出 |

### 内存占用分析

```
ebook_ctx_t 结构体:         ~216 字节 (堆)
raw_buf (扫描):            4,096 字节 (堆)
page_buf (渲染):           4,096 字节 (堆)
FIL 对象:                    ~544 字节 (堆)
bookmark_set:               ~704 字节 (堆)
prev_starts[40]:             160 字节 (在 ebook_ctx_t 内)
----------------------------------------------
合计:                     ~9,816 字节 (堆)
                                      远小于 500KB+ 的全文件加载
```

### 性能分析

每个翻页操作只需：
1. 扫描一次文件（`ebook_scan_page_forward`）：读取 ~2-5 个 4KB chunk，约 10-25ms
2. 读取当前页内容（`ebook_draw_page`）：读取 ~0.5-2KB，约 2-5ms
3. LCD 渲染：约 10-20ms（由 GUI 框架完成）

总翻页时间约 30-50ms，用户体感瞬间完成。
