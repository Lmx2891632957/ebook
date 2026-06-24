# Ebook Reader System Block Diagram

## 1. Overall System Architecture

```
+------------------------------------------------------------------+
|                        Application Layer                          |
|  +------------+  +------------+  +------------+  +-------------+  |
|  |   ebook    |  |  bookmark  |  |  settings  |  | filebrowser |  |
|  |  reader    |  |  module    |  |  module    |  | (appplay.c) |  |
|  +-----+------+  +-----+------+  +-----+------+  +------+------+  |
|        |              |                |                |         |
+--------+--------------+----------------+----------------+---------+
|        |              |                |                |         |
|        v              v                v                v         |
|  +------------------------------------------------------------+   |
|  |                   GUI Control Layer                         |   |
|  |  memo . filelistbox . btn . progressbar . window . listbox  |   |
|  +----------------------------+-------------------------------+   |
+-------------------------------|-----------------------------------+
|                               v                                   |
|  +------------------------------------------------------------+   |
|  |                 Driver Abstraction Layer                    |   |
|  |  lcd . touch . key . spi_sdcard . font . malloc . usart    |   |
|  +----------------------------+-------------------------------+   |
+-------------------------------|-----------------------------------+
|                               v                                   |
|  +------------------------------------------------------------+   |
|  |              Middleware / RTOS Layer                        |   |
|  |     UCOS-II (tasks+sched) . FATFS R0.11 . STM32 HAL        |   |
|  +----------------------------+-------------------------------+   |
+-------------------------------|-----------------------------------+
|                               v                                   |
|  +------------------------------------------------------------+   |
|  |                     Hardware Layer                          |   |
|  |  STM32F103RCT6 (Cortex-M3, 72MHz, 384KB Flash, 48KB SRAM)  |   |
|  |  LCD 2.8" ILI9341/ST7789 . SD Card (SPI) . Touch(XPT2046)  |   |
|  |  KEY1 + WK_UP . LED DS0 . Backlight PC10 (Soft PWM)        |   |
|  +------------------------------------------------------------+   |
+------------------------------------------------------------------+
```

## 2. UCOS-II Task Structure

```
+----------+----------+----------+--------------------------------+
| Priority |   Name   | Stack(B) | Role                           |
+----------+----------+----------+--------------------------------+
|    10    | start    |    64    | Init hardware, create tasks,   |
|          |          |          | then delete self               |
+----------+----------+----------+--------------------------------+
|     7    | usart    |    64    | Serial CLI / debug output      |
+----------+----------+----------+--------------------------------+
|     6    | main     |   256    | GUI, ebook reader, filebrowser |
|          |          |          | (main application logic)       |
+----------+----------+----------+--------------------------------+
|     3    | watch    |   128    | Stack overflow monitor         |
+----------+----------+----------+--------------------------------+
```

## 3. Ebook Reader Core Data Flow

```
                       SD Card .txt File (GBK)
                              |
                              v
  +-------------------------------------------------------------+
  |                    ebook_ctx_t (Context)                      |
  |  +--------------------------------------------------------+  |
  |  | page_start  | page_end  | page_num  | prev_starts[40] |  |
  |  +--------------------------------------------------------+  |
  |  | text_width  | text_height | text_x/y | font_size       |  |
  |  +--------------------------------------------------------+  |
  |  | bg_mode     | bl_level   | file_size | path / fname    |  |
  |  +--------------------------------------------------------+  |
  +-------------------------------------------------------------+
               |                          ^
               v                          |
  +---------------------------+  +---------------------------+
  |   raw_buf (4096 bytes)    |  |  page_buf (4096 bytes)    |
  |   [Scan buffer]           |  |   [Render buffer]          |
  |   f_read chunks =>        |  |   f_read page_start..      |
  |   simulate layout =>      |  |   page_end => whole        |
  |   compute page_end        |  |   page text => gui_show    |
  +---------------------------+  +---------------------------+
               |                          ^
               v                          |
       ebook_scan_page_forward    ebook_draw_page
       (lazy pagination)          (render to LCD)
```

## 4. Page Navigation Flow

```
              +-----+
              |START|   Jump (bookmark / progressbar / font change)
              +--+--+     => prev_count = 0
                 |
                 v
         +-------+-------+
         |  Open File     |
         |  page_start=0  |
         +-------+-------+
                 |
                 v
         +-------+-------+    ebook_scan_page_forward()
         |  Scan Forward  |   raw_buf chunks => sim layout
         |  => page_end   |   => compute page_end
         +-------+-------+
                 |
                 v
         +-------+-------+
         |  Draw Page     |   page_buf = f_read(page_start..page_end)
         |  => LCD Render |   gui_show_string(page_buf, ...)
         +-------+-------+
                 |
        +--------+--------+
        |                 |
        v                 v
  +-----+------+   +-----+------+
  | Next Page  |   | Prev Page  |
  | dir = +1   |   | dir = -1   |
  +-----+------+   +-----+------+
        |                 |
        v                 v
  push page_start    prev_count > 0 ?
  to prev_starts[]      |
  page_start =       +--YES--> pop prev_starts[]
  page_end                |
  page_num++        +-----+------+
                    | NO          |
                    | (after jump)|
                    +-------------+
                          |
                          v
                    ebook_scan_page_backward()
                    binary search [0, target)
                    => finds page_start, page_end
```

## 5. Bookmark Module Structure

```
  +------------------------------------------------------------+
  |              ebook_bm_book_t (704 bytes total)              |
  |  +------------------------------------------------------+  |
  |  | magic (0x424D4B00) | count (0..10) | book_path[256]  |  |
  |  +------------------------------------------------------+  |
  |  | entries[0..9]:                                       |  |
  |  |   +------------------------------------------------+ |  |
  |  |   | file_offset | page_num | label[28] | reserved  | |  |
  |  |   +------------------------------------------------+ |  |
  |  +------------------------------------------------------+  |
  +------------------------------------------------------------+
               |                  ^
               v                  |
       ebook_bm_save()    ebook_bm_load()
       => SD .bmk file    <= SD .bmk file

  API:
  +--------------------------+----------------------------------+
  | ebook_bm_load()          | load from .bmk or init empty     |
  | ebook_bm_save()          | save to .bmk file (704 bytes)    |
  | ebook_bm_add()           | add bookmark (dedup by offset)   |
  | ebook_bm_delete()        | delete by index (0..9)           |
  | ebook_bm_get_path()      | build .bmk filename from path    |
  | ebook_bm_extract_label() | extract 1st sentence as label    |
  | ebook_bm_free()          | free allocated memory            |
  +--------------------------+----------------------------------+
```

## 6. Font & Display Parameters

```
  +----------+----------+-------------------+-------------------+
  | Font ID  | Size(px) | Char Width        | Source            |
  +----------+----------+-------------------+-------------------+
  |    12    |    12    | 12(CJK)  6(ASCII) | SD Font Library   |
  |    16    |    16    | 16(CJK)  8(ASCII) | SD Font Library   |
  |    24    |    24    | 24(CJK) 12(ASCII) | SD Font Library   |
  |    32    |    32    | 32(CJK) 16(ASCII) | SD Font Library   |
  +----------+----------+-------------------+-------------------+

  Display:        320 x 240 TFT LCD
  Text Area:      full_width - 4px margin, excludes top/bottom bars
  Background:     WHITE (normal) / EYECARE_BG (eye-care mode)
  Brightness:     3 levels (1=33%, 2=67%, 3=100% duty)
                  via software PWM on PC10 (TIM3 ISR @ ~1kHz)
```

## 7. Key Memory Buffers

```
  +------------------+---------+----------------------------------+
  | Buffer           | Size    | Usage                            |
  +------------------+---------+----------------------------------+
  | raw_buf          | 4096 B  | File scan chunk buffer           |
  | page_buf         | 4096 B  | Page render buffer (whole page)  |
  | prev_starts[]    |  160 B  | Page history (40 x uint32_t)     |
  | ebook_ctx_t      |  ~60 B  | Ebook reader context             |
  | ebook_bm_book_t  |  704 B  | Bookmark data per book           |
  | MAIN_STK         | 1024 B  | Main task stack                  |
  +------------------+---------+----------------------------------+
  Total core memory: ~10 KB (safe within 48 KB SRAM)
```
