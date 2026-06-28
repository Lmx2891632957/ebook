# 基于 STM32 的电子书阅读器

> UCOS-II RTOS · FATFS R0.11 · FSMC LCD · XPT2046 Touch · MicroSD (SPI)

---

## 硬件平台

| 参数 | 详情 |
|------|------|
| **开发板** | 正点原子 MiniSTM32 V4 |
| **主控芯片** | STM32F103RCT6 (Cortex-M3, 72MHz) |
| **Flash / SRAM** | 256KB / 48KB |
| **显示屏** | 2.8" TFT LCD, 320×240 像素, 16-bit 色深 |
| **显示接口** | FSMC 8080 16-bit 并行 (PB0-PB15 数据, PC6-PC9 控制) |
| **触摸屏** | 电阻式触摸, XPT2046 控制器, 软件 SPI (PC0-CLK, PC1-PEN, PC2-MISO, PC3-MOSI, PC13-CS) |
| **存储** | MicroSD 卡 (SPI1: PA5-SCK, PA6-MISO, PA7-MOSI, PA3-CS) + 8MB SPI Flash (W25Q64, PA2-CS) |
| **背光控制** | 软件 PWM (PC10, TIM3 ISR + BSRR 原子操作) |
| **调试接口** | USART1 115200bps (PA9-TX, PA10-RX) + SWD |

---

## 软件栈

| 层级 | 组件 |
|------|------|
| **RTOS** | UCOS-II V2.93 (4 用户任务 + 统计 + 空闲, `OS_TICKS_PER_SEC=1000`) |
| **文件系统** | FATFS R0.11 (Code Page 936 GBK, LFN 长文件名, 双磁盘挂载) |
| **GUI** | 正点原子控件库 (memo / btn / filelistbox / progressbar) |
| **固件库** | STM32F10x Standard Peripherals Library |

---

## 功能

### 基础功能

| 功能 | 说明 |
|------|------|
| **txt 文本读取与显示** | FATFS 分段读取 + GBK 排版引擎, 支持中英文混排 |
| **滑动翻页** | 触摸滑动手势检测 (左滑 >50px 下一页, 右滑 >50px 上一页), 支持物理按键翻页 |
| **选书界面** | filelistbox 控件, 目录层级浏览, .txt 过滤器, 双击打开 |
| **分档调节字体** | 12 / 16 / 24 三档字号, 切换后自动重排页面 |
| **书签功能** | 每书最多 10 条, 持久化为独立 .bmk 文件, 首句自动命名, ±200 字节容差去重 |

### 扩展功能

| 功能 | 说明 |
|------|------|
| **可拖动进度条** | 触摸拖动实时显示百分比, 松手后 GBK 边界对齐跳转 |
| **护眼模式** | 白色背景 ↔ 暖黄色背景一键切换 |
| **三档亮度调节** | 软件 PWM 控制 LCD 背光 (暗 33% / 中 66% / 亮 100%) |
| **SD 卡热插拔** | 自动检测卡插入/拔出, 动态挂载/卸载 |

---

## 使用方法

### 1. 硬件准备

- 正点原子 MiniSTM32 V4 开发板一块
- MicroSD 卡一张 (FAT32 格式, 根目录存放 `.txt` 电子书文件)
- USB 转 TTL 串口模块 (可选, 用于串口调试)
- ST-Link 或 J-Link 下载器

### 2. 固件下载

用 Keil MDK 打开 `Projects/MDK-ARM/atk_f103.uvprojx`, 编译后通过 SWD 接口下载到开发板。

### 3. 操作流程

#### 选书

上电后自动进入文件浏览界面, 显示 SD 卡根目录下的 `.txt` 文件列表:

- **上/下滑动** — 翻页浏览文件列表
- **双击文件名** — 打开电子书进入阅读模式

#### 阅读

打开书籍后进入阅读模式, 屏幕显示书籍内容:

- **左滑** (>50px) — 下一页
- **右滑** (>50px) — 上一页
- **KEY1 按键** — 下一页
- **WK_UP 按键** — 上一页
- **底部工具栏** — 点击对应按钮:

| 按钮 | 功能 |
|------|------|
| **返回** | 关闭当前书籍, 回到文件浏览 |
| **字体** | 弹出字号面板 (12 / 16 / 24) |
| **书签** | 弹出书签面板 (添加当前页 / 查看已有书签 / 跳转 / 删除) |
| **护眼** | 白色背景 ↔ 暖黄色背景切换 |
| **亮度** | 弹出亮度面板 (暗 / 中 / 亮) |

#### 进度条

阅读模式下屏幕底部显示进度条, 实时显示当前阅读位置百分比:

- **拖动进度条** — 快速跳转
- **松手** — 自动对齐到最近的合法 GBK 字符边界

### 4. 串口调试 (可选)

连接 USART1 (PA9/PA10), 波特率 115200, 每秒输出一次 SRAM 内存使用率:

```
mem:74.3%  (SRAMIN used / total)
```

### 5. 书签文件格式

书签存储在 SD 卡上与 `.txt` 文件同名的 `.bmk` 文件中。每条书签记录格式:

```
偏移量(4B) + 书签名称(28B) + 保留(4B)
```

手动删除 `.bmk` 文件即可清除对应书籍的全部书签。

---

## 核心设计

### 分段读取策略

受限于 48KB SRAM (系统占用 ~42KB, 可用 ~6KB), 不支持将整个 txt 文件读入内存。采用分段读取方案:

1. 用 `f_lseek` 定位到需要显示的偏移位置
2. 用 `f_read` 读取 512 字节片段到 4KB 扫描缓冲区
3. GBK 排版引擎逐字排版, 满一屏后停止
4. 记录当前页的 `page_start` 和 `page_end` 偏移, 用于翻页定位

详见 [FenDuanDuQu.md](readme/FenDuanDuQu.md)。

### GBK 边界处理

`f_lseek` 跳转可能落在 GBK 双字节字符的中间 (半字位置), 导致后续排版出现乱码。解决方案:

- 跳转后向前扫描, 找到第一个合法的 GBK 首字节 (>= 0x81)
- 分段读取时维护 `prev_lead_byte` —— 如果 chunk 的最后一个字节是 GBK 首字节, 则回退一字节, 与下一个 chunk 拼接

### 软件 PWM 背光

PC10 引脚在 64-pin STM32F103RCT6 封装上无定时器复用通道, 无法使用硬件 PWM。采用纯软件方案:

- TIM3 @ 3kHz + 3 步子计数器 → 有效频率 1kHz
- ISR 内通过 `GPIOB->BSRR` 原子操作 (单周期, 无竞态)
- NVIC 优先级设为 15 (最低), 确保不抢占 UCOS-II 的 PendSV (优先级 14)

### UCOS-II 任务设计

| 任务 | 优先级 | 栈 | 周期 | 职责 |
|------|--------|-----|------|------|
| watch_task | 3 | 128B | 10ms | LED 闪烁, KEY0 检测, SD 卡热插拔 |
| main_task | 6 | 256B | 持续 | ebook_play() 阅读器主循环 |
| usart_task | 7 | 64B | 1s | printf 内存使用率 |

> 优先级数值越小越高。watch_task 优先级最高但用 `delay_ms(10)` 主动让出 CPU。

---

## 工程结构

```
ebook/
├── User/
│   ├── main.c                    # 系统入口, 任务创建
│   └── APP/
│       ├── ebook.c               # 阅读器主逻辑 (状态机, UI, 工具栏)
│       ├── ebook.h               # 阅读器数据结构定义
│       ├── ebook_bookmark.c      # 书签持久化模块
│       ├── ebook_bookmark.h
│       ├── common.c              # 公共函数 (字库管理等)
│       └── common.h
├── Drivers/
│   ├── BSP/
│   │   ├── LCD/lcd.c             # LCD 显示驱动 (FSMC 8080)
│   │   ├── TOUCH/touch.c         # 触摸驱动 (XPT2046)
│   │   ├── SDMMC/spi_sdcard.c    # SD 卡 SPI 驱动
│   │   ├── SPI/                  # SPI Flash 驱动
│   │   └── USART/                # 串口驱动
│   └── SYSTEM/                   # 系统时钟, 延时, SysTick
├── Middlewares/
│   ├── UCOSII/                   # UCOS-II RTOS
│   ├── FATFS/                    # FATFS R0.11
│   └── GUI/                      # 正点原子 GUI 控件库
├── Projects/MDK-ARM/             # Keil MDK 工程
└── README.md                     # 本文件
```

---

## 编译与下载

1. 安装 Keil MDK V5 及 STM32F1xx 器件包
2. 打开 `Projects/MDK-ARM/atk_f103.uvprojx`
3. 选择 Target → Rebuild all (F7)
4. 通过 ST-Link/J-Link 连接开发板 SWD 接口
5. 点击 Load (F8) 下载固件
6. 复位开发板, 自动进入电子书阅读器

---


