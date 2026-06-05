# 亮度调节功能实现计划

> **项目**：基于 STM32F103RCT6 的电子书阅读器
> **方案**：硬件 PWM 三档亮度调节（暗 / 中 / 亮），底部栏按钮 + 弹出选择面板
> **OS**：μC/OS-II

---

## 一、需求描述

在阅读页面底部栏添加一个"亮度"按钮，点击后弹出三档选择面板（类似字体选择面板）：

1. **暗**：~33% 背光占空比，适合暗光环境阅读
2. **中**：~66% 背光占空比，默认档位
3. **亮**：~100% 背光占空比，适合强光环境阅读

---

## 二、关键架构说明（防止幻觉）

### 2.1 硬件背光引脚

| 属性 | 值 |
|------|-----|
| 引脚 | **PC10** |
| 当前控制方式 | 简单 GPIO 开关（`LCD_BL(0/1)` 宏） |
| AFIO 复用功能 | **TIM3_CH3**（定时器 3 通道 3） |
| TIM3 占用情况 | **空闲** — `gtim_timx_int_init` 仅声明未调用，GUI 使用 μC/OS SysTick 计时 |

### 2.2 GPIO 模式切换

**当前**（`lcd.c:639`）：PC10 配置为 `SYS_GPIO_MODE_OUT`（普通推挽输出），只能高低电平。

**改为 PWM 后**：需将 PC10 重配置为 `SYS_GPIO_MODE_AF`（复用功能推挽输出），由 TIM3_CH3 控制。

关键约束：`lcd.c` 的 `lcd_init()` 中初始化了所有 LCD 引脚（含 BL）。启动 PWM 时必须在运行时**重新配置** PC10 为 AF 模式，不需要修改 `lcd_init()` 本身。

### 2.3 PWM 基础函数

项目已有 `gtim_timx_pwm_chy_init()`（[gtim.c:84](Drivers/BSP/TIMER/gtim.c#L84)），但它是用宏硬绑定到 TIM2_CH1 + PA0 的。亮度控制需要的是 TIM3_CH3 + PC10，因此**不能直接复用**该函数，需要编写专用的 `lcd_bl_pwm_init()` 函数。

具体操作（直接在 `ebook.c` 或新增 `lcd` 相关文件中实现）：

1. 使能 TIM3 时钟（`RCC->APB1ENR |= 1 << 1`）
2. 使能 GPIOC 时钟（`RCC->APB2ENR |= 1 << 4`）
3. 配置 PC10 为 `SYS_GPIO_MODE_AF, SYS_GPIO_OTYPE_PP`（复用推挽输出）
4. 设置 TIM3 ARR=999, PSC=71 → PWM 频率 = 72MHz / 72 / 1000 = **1kHz**
5. 配置 TIM3_CH3 为 PWM 模式 1（`CCMR2 |= 6 << 4; CCMR2 |= 1 << 3`）
6. 使能 TIM3_CH3 输出（`CCER |= 1 << 8`）
7. 设置 CCR3 初始值 = 660（默认中档）
8. 启动 TIM3（`CR1 |= 1 << 0`）

### 2.4 亮度设置函数

```c
/**
 * @brief       设置背光亮度
 * @param       level: 1=暗(33%), 2=中(66%), 3=亮(100%)
 * @retval      无
 * @note        ARR=999, 对应 CCR3 = level * 333（暗=333, 中=666, 亮=999）
 */
void lcd_bl_set_brightness(uint8_t level);
```

调用 `TIM3->CCR3 = (uint16_t)level * 333;` 完成设置。

> **注意**：如果亮度档位改了但 TIM3 还没启动，该函数不生效。所以 PWM 模式需在进入阅读页时由 `lcd_bl_pwm_init()` 启动一次，之后只用 `lcd_bl_set_brightness()` 调档位即可。

### 2.5 退出阅读时恢复 GPIO 模式

退出阅读页面（返回选书界面）时，需要：

1. 关闭 TIM3（`TIM3->CR1 &= ~(1 << 0)`）
2. 将 PC10 恢复为普通 GPIO 输出模式
3. 调用 `LCD_BL(0)` 关背光（选书界面不需要背光常亮）

或者简化处理：直接保持 PWM 模式但不关闭 TIM3，只需在退出阅读时将 CCR3 设为 0（相当于关背光），回到选书时用 `lcd_bl_set_brightness(0)`。

### 2.6 亮度值存储

当前 `ebook_ctx_t` 结构体已有 `bg_mode` 字段（护眼模式）。类似地新增一个 `bl_level` 字段：

```c
uint8_t  bg_mode;     /* 背景模式: 0=正常(白色), 1=护眼(浅黄)    */
uint8_t  bl_level;    /* 亮度档位: 1=暗, 2=中(默认), 3=亮      */
```

`gui_memset(ctx, 0, ...)` 会将其清零，需在 `ebook_ctx_init()` 末尾显式设为默认值 2。

> **暂不持久化**：本次实现不写入 EEPROM，每次打开书籍恢复默认中档（2）。后续可按需扩展。

### 2.7 底部栏按钮布局

当前从右到左的按钮：

| 按钮 | n | X 坐标公式 | 说明 |
|------|---|-----------|------|
| `rbtn`（返回） | 1 | `lcddev.width - 2*tbfsize - 9` | |
| `fbtn`（字体） | 2 | `lcddev.width - 4*tbfsize - 21` | |
| `bbtn`（书签） | 3 | `lcddev.width - 6*tbfsize - 34` | |
| `ebtn`（护眼） | 4 | `lcddev.width - 8*tbfsize - 47` | |
| **`brbtn`（亮度）** | **5** | **`lcddev.width - 10*tbfsize - 60`** | **新增** |

所有按钮：宽 `2 * tbfsize + 8`，高 `tbheight - 1`，type `0x03`（`BTN_TYPE_TEXTA`）。

### 2.8 三档选择面板（仿字体面板）

弹出面板包含 3 个档位按钮 + 1 个取消按钮：

| 按钮 | 标签（GBK） | 说明 |
|------|-----------|------|
| `br_btn_dim` | `"\xB0\xB5"` | "暗" — 33% 占空比 |
| `br_btn_mid` | `"\xD6\xD0"` | "中" — 66% 占空比 |
| `br_btn_bri` | `"\xC1\xC1"` | "亮" — 100% 占空比 |
| `br_btn_cancel` | 同字体面板取消按钮 | "取消" / "返回" |

面板尺寸（居中显示）：
- 宽：`3 * FONT_SEL_BTN_W + 4 * FONT_SEL_PAD`（3 个按钮 + 4 个边距）
- 高：`FONT_SEL_BTN_H + 36 + 3 * FONT_SEL_PAD`（按钮行 + 取消按钮行 + 3 个边距）

---

## 三、改动清单

| # | 文件 | 改动类型 | 说明 |
|---|------|----------|------|
| 1 | `User/APP/ebook.h` | 添加 1 行 | `ebook_ctx_t` 新增 `bl_level` 字段 |
| 2 | `User/APP/ebook.c` | 新增 2 个函数 | `lcd_bl_pwm_init()` + `lcd_bl_set_brightness()` |
| 3 | `User/APP/ebook.c` | 修改 `ebook_ctx_init` | 初始化 `bl_level = 2`（默认中档） |
| 4 | `User/APP/ebook.c` | 修改 `ebook_ctx_deinit` | 退出时恢复 GPIO 模式或关 PWM |
| 5 | `User/APP/ebook.c` | 修改 `ebook_play` — 变量声明 | 新增 `_btn_obj *brbtn` |
| 6 | `User/APP/ebook.c` | 修改 `ebook_play` — 创建按钮 | 底部栏创建亮度按钮 `brbtn` |
| 7 | `User/APP/ebook.c` | 修改 `ebook_play` — 进入阅读 | 启动 `lcd_bl_pwm_init()` + `lcd_bl_set_brightness(ctx->bl_level)` |
| 8 | `User/APP/ebook.c` | 修改 `ebook_play` — 按钮绘制 | 进入阅读时 `btn_draw(brbtn)` |
| 9 | `User/APP/ebook.c` | 修改 `ebook_play` — 点击处理 | 亮度按钮弹出三档面板（仿字体面板） |
| 10 | `User/APP/ebook.c` | 修改 `ebook_play` — 清理 | 销毁 `brbtn` |

---

## 四、详细修改步骤

### 步骤 1：新增亮度字段 — `ebook.h`

**位置**：`ebook_ctx_t` 结构体内，`bg_mode` 字段后面

```c
    uint8_t  bg_mode;                /* 背景模式: 0=正常(白色), 1=护眼(浅黄)    */
    uint8_t  bl_level;               /* 亮度档位: 1=暗, 2=中(默认), 3=亮      */
```

`gui_memset(ctx, 0, ...)` 会将其初始化为 0，需要在 `ebook_ctx_init()` 中显式设为 2（见步骤 3）。

---

### 步骤 2：新增背光 PWM 函数 — `ebook.c`

**位置**：在 `ebook.c` 的 `#include` 之后、`ebook_ctx_init()` 之前插入两个静态函数。

#### 2a. PWM 初始化函数

```c
/**
 * @brief       Initialize TIM3_CH3 PWM on PC10 for LCD backlight control
 * @param       None
 * @retval      None
 * @note        Configures PC10 as AF push-pull (TIM3_CH3), 1kHz PWM.
 *              ARR=999, PSC=71 → 72MHz/(71+1)/(999+1) = 1kHz.
 *              Leaves TIM3 running; call lcd_bl_set_brightness() to set level.
 *              This replaces the simple GPIO LCD_BL(0/1) macro during reading.
 */
static void lcd_bl_pwm_init(void)
{
    /* 1. Enable clocks */
    RCC->APB2ENR |= 1 << 4;   /* GPIOC clock */
    RCC->APB1ENR |= 1 << 1;   /* TIM3 clock  */

    /* 2. Reconfigure PC10 from GPIO output to AF push-pull (TIM3_CH3) */
    sys_gpio_set(GPIOC, SYS_GPIO_PIN10,
                 SYS_GPIO_MODE_AF, SYS_GPIO_OTYPE_PP,
                 SYS_GPIO_SPEED_HIGH, SYS_GPIO_PUPD_PU);

    /* 3. Set auto-reload and prescaler for ~1kHz PWM */
    TIM3->ARR  = 999;    /* 1000 counts → 1kHz */
    TIM3->PSC  = 71;     /* 72MHz / 72 = 1MHz timer clock */
    TIM3->BDTR |= 1 << 15;   /* MOE (not needed for TIM3 but harmless) */

    /* 4. Configure CH3 as PWM mode 1, preload enabled */
    TIM3->CCMR2 |= 6 << 4;   /* OC3M[2:0] = 110 → PWM mode 1 */
    TIM3->CCMR2 |= 1 << 3;   /* OC3PE = 1 → preload enable */

    /* 5. Enable CH3 output, active high */
    TIM3->CCER |= 1 << 8;    /* CC3E = 1 → output enabled */
    TIM3->CCER |= 1 << 9;    /* CC3P = 0 → active high (default, already 0) */

    /* 6. Auto-reload preload enable + start timer */
    TIM3->CR1 |= 1 << 7;   /* ARPE enable */
    TIM3->CR1 |= 1 << 0;   /* CEN = 1 → start timer */
}
```

> **关于寄存器操作 vs 调用 `gtim_timx_pwm_chy_init()`**：`gtim_timx_pwm_chy_init()` 硬绑定到 `GTIM_TIMX_PWM`（TIM2）和 `GTIM_TIMX_PWM_CHY`（CH1），无法简单重定向到 TIM3_CH3。如果不想直接操作寄存器，也可以在 `gtim.h` 中新增一套宏定义（`GTIM_TIMX_PWM2` 等），但改动面更大。此处用直接寄存器操作更简洁。

#### 2b. 亮度设置函数

```c
/**
 * @brief       Set LCD backlight brightness level
 * @param       level: 0=off, 1=dim(33%), 2=mid(66%), 3=bright(100%)
 * @retval      None
 * @note        ARR=999.  CCR3 = level * 333.
 *              Level 0 works only if TIM3 PWM is running.
 */
static void lcd_bl_set_brightness(uint8_t level)
{
    if (level == 0)
        TIM3->CCR3 = 0;
    else if (level >= 3)
        TIM3->CCR3 = 999;
    else
        TIM3->CCR3 = (uint16_t)level * 333;
}
```

---

### 步骤 3：初始化亮度默认值 — `ebook_ctx_init()`

**位置**：在 `ebook_ctx_init()` 末尾、`return 0;` 之前

```c
    ctx->bl_level = 2;    /* 默认中档亮度 */
```

---

### 步骤 4：退出阅读时恢复 — `ebook_ctx_deinit()`

**位置**：在 `ebook_ctx_deinit()` 中，`g_ebook_prgb = NULL;` 之后、`if (!ctx) return;` 之前

```c
    /* Disable backlight PWM, restore PC10 to simple GPIO off */
    TIM3->CR1 &= ~(1 << 0);       /* stop TIM3 */
    sys_gpio_set(GPIOC, SYS_GPIO_PIN10,
                 SYS_GPIO_MODE_OUT, SYS_GPIO_OTYPE_PP,
                 SYS_GPIO_SPEED_HIGH, SYS_GPIO_PUPD_PU);
    LCD_BL(0);                    /* turn off backlight */
```

> 这确保退出阅读回到选书界面时背光关闭，与原来的 GPIO 控制行为一致。

---

### 步骤 5：声明亮度按钮变量 — `ebook_play()`

**位置**：在 `_btn_obj *ebtn;` 之后

```c
    _btn_obj *ebtn;
    _btn_obj *brbtn;             /* 亮度按钮 */
```

---

### 步骤 6：创建亮度按钮 — `ebook_play()` 初始化区

**位置**：在护眼按钮（`ebtn`）创建代码之后、`buf = gui_memin_malloc(1024);` 之前

```c
    /* Brightness button - positioned to the left of eye-care button */
    brbtn = btn_creat(lcddev.width - 10 * gui_phy.tbfsize - 60,
                      lcddev.height - gui_phy.tbheight,
                      2 * gui_phy.tbfsize + 8, gui_phy.tbheight - 1,
                      0, 0x03);

    if (brbtn)
    {
        brbtn->caption   = (uint8_t *)"\xC1\xC1\xB6\xC8";   /* "亮度" in GBK */
        brbtn->font      = gui_phy.tbfsize;
        brbtn->bcfdcolor = WHITE;
        brbtn->bcfucolor = WHITE;
        btn_draw(brbtn);
    }
```

> **"亮度" GBK 编码**：亮 = `\xC1\xC1`，度 = `\xB6\xC8`

---

### 步骤 7：进入阅读状态时启动 PWM — `ebook_play()`

**位置**：在 `ebooksta = 1;` 之后、`btn_draw(fbtn);` 之前（约第 666 行）

```c
                ebooksta = 1;
                lcd_bl_pwm_init();                       /* 启动背光 PWM */
                lcd_bl_set_brightness(ctx->bl_level);    /* 应用亮度档位 */
                btn_draw(fbtn);
```

---

### 步骤 8：进入阅读时绘制亮度按钮 — `ebook_play()`

**位置**：在 `btn_draw(ebtn);` 之后

```c
                btn_draw(ebtn);   /* ensure eye-care button is visible */
                btn_draw(brbtn);  /* ensure brightness button is visible */
```

---

### 步骤 9：亮度按钮点击处理 — `ebook_play()` 主循环

**位置**：在护眼按钮处理块之后、进度条检查块之前

```c
            /* ---- Check brightness button ---- */
            if (brbtn)
            {
                res = btn_check(brbtn, &in_obj);

                if (res && ((brbtn->sta & 0X80) == 0))
                {
                    uint16_t panel_x, panel_y;
                    uint8_t  bright_sel = 0;
                    _btn_obj *br_btn_dim, *br_btn_mid, *br_btn_bri, *br_btn_cancel;

                    panel_x = (lcddev.width
                               - (3 * FONT_SEL_BTN_W + 4 * FONT_SEL_PAD)) / 2;
                    panel_y = lcddev.height / 3;

                    /* Draw panel background */
                    gui_fill_rectangle(panel_x, panel_y,
                                       3 * FONT_SEL_BTN_W + 4 * FONT_SEL_PAD,
                                       FONT_SEL_BTN_H + 36 + 3 * FONT_SEL_PAD,
                                       0xC618);

                    /* Create brightness level buttons */
                    br_btn_dim = btn_creat(panel_x + FONT_SEL_PAD,
                                           panel_y + FONT_SEL_PAD,
                                           FONT_SEL_BTN_W, FONT_SEL_BTN_H,
                                           0, BTN_TYPE_ANG);
                    br_btn_mid = btn_creat(panel_x + FONT_SEL_PAD * 2 + FONT_SEL_BTN_W,
                                           panel_y + FONT_SEL_PAD,
                                           FONT_SEL_BTN_W, FONT_SEL_BTN_H,
                                           0, BTN_TYPE_ANG);
                    br_btn_bri = btn_creat(panel_x + FONT_SEL_PAD * 3 + FONT_SEL_BTN_W * 2,
                                           panel_y + FONT_SEL_PAD,
                                           FONT_SEL_BTN_W, FONT_SEL_BTN_H,
                                           0, BTN_TYPE_ANG);
                    br_btn_cancel = btn_creat(panel_x + FONT_SEL_PAD,
                                               panel_y + FONT_SEL_PAD * 2 + FONT_SEL_BTN_H,
                                               3 * FONT_SEL_BTN_W + 2 * FONT_SEL_PAD,
                                               36, 0, BTN_TYPE_ANG);

                    /* Configure "暗" (dim) button */
                    if (br_btn_dim)
                    {
                        br_btn_dim->caption   = (uint8_t *)"\xB0\xB5";   /* "暗" */
                        br_btn_dim->font      = 16;
                        br_btn_dim->bcfucolor = WHITE;
                        br_btn_dim->bcfdcolor = BLACK;
                        btn_draw(br_btn_dim);
                    }

                    /* Configure "中" (mid) button */
                    if (br_btn_mid)
                    {
                        br_btn_mid->caption   = (uint8_t *)"\xD6\xD0";   /* "中" */
                        br_btn_mid->font      = 16;
                        br_btn_mid->bcfucolor = WHITE;
                        br_btn_mid->bcfdcolor = BLACK;
                        btn_draw(br_btn_mid);
                    }

                    /* Configure "亮" (bri) button */
                    if (br_btn_bri)
                    {
                        br_btn_bri->caption   = (uint8_t *)"\xC1\xC1";   /* "亮" */
                        br_btn_bri->font      = 16;
                        br_btn_bri->bcfucolor = WHITE;
                        br_btn_bri->bcfdcolor = BLACK;
                        btn_draw(br_btn_bri);
                    }

                    /* Configure cancel button */
                    if (br_btn_cancel)
                    {
                        br_btn_cancel->caption   = (uint8_t *)GUI_BACK_CAPTION_TBL[gui_phy.language];
                        br_btn_cancel->font      = 16;
                        br_btn_cancel->bcfucolor = WHITE;
                        br_btn_cancel->bcfdcolor = BLACK;
                        btn_draw(br_btn_cancel);
                    }

                    /* Modal input loop: block until selection or cancel */
                    while (!bright_sel)
                    {
                        tp_dev.scan(0);
                        in_obj.get_key(&tp_dev, IN_TYPE_TOUCH);

                        if (system_task_return) break;

                        delay_ms(10);

                        if (btn_check(br_btn_dim, &in_obj))
                        { if ((br_btn_dim->sta & 0X80) == 0) bright_sel = 1; }
                        if (btn_check(br_btn_mid, &in_obj))
                        { if ((br_btn_mid->sta & 0X80) == 0) bright_sel = 2; }
                        if (btn_check(br_btn_bri, &in_obj))
                        { if ((br_btn_bri->sta & 0X80) == 0) bright_sel = 3; }
                        if (btn_check(br_btn_cancel, &in_obj))
                        { if ((br_btn_cancel->sta & 0X80) == 0) break; }
                    }

                    /* Destroy dialog buttons */
                    btn_delete(br_btn_dim);
                    btn_delete(br_btn_mid);
                    btn_delete(br_btn_bri);
                    btn_delete(br_btn_cancel);

                    /* Apply brightness change or restore page */
                    if (bright_sel)
                    {
                        ctx->bl_level = bright_sel;
                        lcd_bl_set_brightness(bright_sel);
                        /* Restore page content (panel was drawn over it) */
                        ebook_draw_page(ctx);
                    }
                    else
                    {
                        /* Restore page content */
                        ebook_draw_page(ctx);
                    }

                    swipe_tracking = 0;
                    continue;
                }
            }
```

> **注意**：面板弹出时会覆盖页面内容，用户选择后或取消后都需要调用 `ebook_draw_page(ctx)` 恢复页面显示。`ebook_draw_page` 末尾已有自动进度条重绘逻辑，无需额外处理。

---

### 步骤 10：清理区销毁亮度按钮 — `ebook_play()`

**位置**：在 `btn_delete(ebtn);` 之后

```c
    btn_delete(ebtn);
    btn_delete(brbtn);       /* 销毁亮度按钮 */
```

---

### 步骤 11：TPAD 返回浏览时关闭 PWM

**位置**：在 `system_task_return` 处理中，`if (prgb) { progressbar_delete(prgb); prgb = NULL; }` 之后

```c
                /* Disable backlight PWM before returning to browse */
                TIM3->CR1 &= ~(1 << 0);
                sys_gpio_set(GPIOC, SYS_GPIO_PIN10,
                             SYS_GPIO_MODE_OUT, SYS_GPIO_OTYPE_PP,
                             SYS_GPIO_SPEED_HIGH, SYS_GPIO_PUPD_PU);
                LCD_BL(0);
```

> 此步骤确保通过 TPAD 返回选书界面时背光也被正确关闭。与 `ebook_ctx_deinit()` 中的逻辑对称。

---

## 五、完整修改位置汇总

| # | 文件 | 改动描述 |
|---|------|----------|
| 1 | `ebook.h` | `ebook_ctx_t` 添加 `uint8_t bl_level;`（`bg_mode` 之后） |
| 2 | `ebook.c` | 新增 `lcd_bl_pwm_init()` 函数（`#include` 之后、`ebook_ctx_init` 之前） |
| 3 | `ebook.c` | 新增 `lcd_bl_set_brightness()` 函数（紧跟 2 之后） |
| 4 | `ebook.c` | `ebook_ctx_init()` 末尾加 `ctx->bl_level = 2;` |
| 5 | `ebook.c` | `ebook_ctx_deinit()` 中 `g_ebook_prgb = NULL;` 后加 GPIO 恢复 + 关背光 |
| 6 | `ebook.c` | `ebook_play()` 变量声明区加 `_btn_obj *brbtn;` |
| 7 | `ebook.c` | `ebtn` 创建代码之后创建 `brbtn` |
| 8 | `ebook.c` | `ebooksta = 1;` 之后启动 PWM + 设置亮度 |
| 9 | `ebook.c` | `btn_draw(ebtn);` 之后加 `btn_draw(brbtn);` |
| 10 | `ebook.c` | 护眼按钮处理之后插入亮度按钮点击处理（弹出三档面板） |
| 11 | `ebook.c` | 清理区 `btn_delete(ebtn);` 之后加 `btn_delete(brbtn);` |
| 12 | `ebook.c` | `system_task_return` 处理中进度条清理之后加 GPIO 恢复 + 关背光 |

---

## 六、亮度面板交互流程

```
用户点击 "亮度" 按钮
  │
  ├─→ 绘制半透明灰色面板（0xC618）
  ├─→ 创建 4 个按钮：暗(33%) / 中(66%) / 亮(100%) / 取消
  │
  ├─→ 进入模态循环：
  │   ├─ 点击 "暗" → bright_sel = 1 → 退出循环
  │   ├─ 点击 "中" → bright_sel = 2 → 退出循环
  │   ├─ 点击 "亮" → bright_sel = 3 → 退出循环
  │   └─ 点击 "取消" → break（bright_sel = 0）
  │
  ├─→ 销毁面板按钮
  │
  ├─→ 如果 bright_sel != 0:
  │   ├─ ctx->bl_level = bright_sel（保存档位）
  │   ├─ lcd_bl_set_brightness(bright_sel)（硬件生效）
  │   └─ ebook_draw_page(ctx)（恢复页面显示）
  │
  └─→ 如果取消：ebook_draw_page(ctx)（仅恢复页面显示）
```

---

## 七、PWM 频率与亮度计算

| 参数 | 值 | 说明 |
|------|-----|------|
| 定时器时钟 | 72MHz | APB1 总线时钟 |
| PSC | 71 | 72MHz / (71+1) = 1MHz |
| ARR | 999 | 1MHz / (999+1) = 1kHz PWM |
| CCR3(暗) | 333 | 333/1000 = 33.3% 占空比 |
| CCR3(中) | 666 | 666/1000 = 66.6% 占空比 |
| CCR3(亮) | 999 | 999/1000 = 99.9% 占空比 |
| CCR3(关) | 0 | 0% 占空比 |


## 八、GBK 编码参考

| 字 | GBK 字节 |
|-----|----------|
| 亮 | `\xC1\xC1` |
| 度 | `\xB6\xC8` |
| 暗 | `\xB0\xB5` |
| 中 | `\xD6\xD0` |

---

## 九、与现有功能的兼容性

1. **护眼模式**：亮度与背景色独立，互不影响。`ebook_draw_page()` 末尾的进度条重绘自动生效。
2. **字体切换**：亮度不变，面板复用相同的 `FONT_SEL_*` 宏定义尺寸。
3. **书签/进度条跳转**：亮度不变，`ebook_draw_page()` 自动重绘进度条。
4. **TPAD 返回**：背光关闭 + GPIO 恢复，回到选书界面前灯光熄灭。
5. **翻页性能**：PWM 完全由 TIM3 硬件输出，CPU 零开销。
6. **内存开销**：新增 1 字节 `bl_level` + 1 个按钮指针（4 字节）= 共 5 字节。

---

## 十、注意事项

1. **PC10 模式切换**：PWM 启动时将 PC10 从 GPIO 输出改为 AF 推挽输出；退出时改回 GPIO 输出。切换过程中屏幕可能短暂闪烁一下，属正常现象。
2. **TIM3 未使用**：确保其他模块没有暗自使用 TIM3。已确认 `gtim_timx_int_init` 未被调用，GUI 使用 SysTick 而非 TIM3。
3. **寄存器位域**：`CCMR2` 控制 CH3/CH4，`CCMR1` 控制 CH1/CH2。CH3 对应 `CCMR2` 的 bit[4:6] 和 bit[3]。
4. **面板背景**：使用 `0xC618`（浅灰 `LGRAY`），与字体面板、书签面板一致。
5. **系统时钟 72MHz**：PSC 和 ARR 的计算基于 72MHz 系统时钟。如果项目修改了时钟配置，需要重新计算。
