/**
 ****************************************************************************************************
 * @file        ebook.c
 * @author      ALIENTEK
 * @version     V1.2
 * @date        2022-05-26
 * @brief       Ebook reader  page-by-page reading with swipe support
 * @license     Copyright (c) 2020-2032, ALIENTEK
 ****************************************************************************************************
 * @attention
 *
 * Platform: ALIENTEK Explorer
 * Change log
 * V1.1 20220526
 * 1, Updated comment style
 * 2, Changed u8/u16/u32 to uint8_t/uint16_t/uint32_t
 * V1.2
 * 1, Page-by-page reading to avoid large file memory overflow
 * 2, Touch swipe gesture page turning
 * 3, Physical key page turning (KEY1 next, WK_UP previous)
 ****************************************************************************************************
 */

#include "ebook.h"
#include "./FATFS/exfuns/exfuns.h"
#include "./BSP/KEY/key.h"
#include "./GUI/progressbar.h"
#include "string.h"

/* Progress bar layout */
#define PRGB_HEIGHT     12    /* 进度条总高度(含1px边框) */
#define PRGB_MARGIN      3    /* 进度条与底部栏间距 */
#define PRGB_GAP         3    /* 文本区与进度条间距 */
#define PRGB_LABEL_W    60    /* 百分比标签宽度 */
#define PRGB_LABEL_H    18    /* 百分比标签高度 */

/* File-scope progress bar pointer — used by ebook_draw_page() to
 * auto-redraw after fill, avoiding missing redraw at any call site. */
static _progressbar_obj *g_ebook_prgb = NULL;

/* Reminder messages (GBK encoded for LCD display) */
uint8_t *const ebook_remind_msg_tbl[GUI_LANGUAGE_NUM] =
{
    "Out of memory!",
};

/* Defined in settings.c */

/* ========================================================================
 *  Software PWM for LCD backlight (PC10)
 *
 *  STM32F103RCT6: PC10 has NO timer AF function (only USART3_TX).
 *  Hardware PWM is IMPOSSIBLE on this pin — use TIM3 interrupt +
 *  GPIO bit-banging instead.
 *
 *  Design:
 *    TIM3 @ 3kHz ISR → 3-step sub-counter → ~1kHz effective PWM
 *    Level 1 (dim):  1/3 = 33% duty
 *    Level 2 (mid):  2/3 = 67% duty
 *    Level 3 (bri):  3/3 = 100% (always ON)
 *
 *  PC10 always stays in GPIO output mode — NEVER switched to AF.
 * ======================================================================== */

static volatile uint8_t sw_bl_pwm_cnt;     /* 0,1,2 cyclically */
static volatile uint8_t sw_bl_pwm_level;   /* 1=dim, 2=mid, 3=bright */

/**
 * @brief       TIM3 interrupt handler — software PWM for LCD backlight
 * @param       None
 * @retval      None
 * @note        ISR rate ≈ 3kHz (PSC=71, ARR=332).
 *              3-step counter gives ~1kHz effective PWM — flicker-free.
 */
void TIM3_IRQHandler(void)
{
    if (TIM3->SR & (1 << 0))          /* UIF: update interrupt */
    {
        TIM3->SR &= ~(uint16_t)(1 << 0);  /* clear UIF */

        if (sw_bl_pwm_level >= 3)
        {
            LCD_BL(1);                /* 100% — always ON */
        }
        else
        {
            sw_bl_pwm_cnt++;
            if (sw_bl_pwm_cnt >= 3) sw_bl_pwm_cnt = 0;

            if (sw_bl_pwm_level >= 2)      /* mid: 66% */
                LCD_BL(sw_bl_pwm_cnt < 2 ? 1 : 0);
            else                            /* dim: 33% */
                LCD_BL(sw_bl_pwm_cnt == 0 ? 1 : 0);
        }
    }
}

/**
 * @brief       Initialize software PWM for LCD backlight on PC10
 * @param       None
 * @retval      None
 * @note        PC10 stays GPIO output — never switched to AF.
 *              TIM3 generates update interrupts at ~3kHz.
 *              ISR toggles PC10 in software to emulate PWM.
 */
static void lcd_bl_pwm_init(void)
{
    /* 1. Enable clocks */
    RCC->APB2ENR |= 1 << 4;   /* GPIOC clock */
    RCC->APB1ENR |= 1 << 1;   /* TIM3 clock  */

    /* 2. Ensure PC10 is GPIO output push-pull (keep original mode) */
    sys_gpio_set(GPIOC, SYS_GPIO_PIN10,
                 SYS_GPIO_MODE_OUT, SYS_GPIO_OTYPE_PP,
                 SYS_GPIO_SPEED_HIGH, SYS_GPIO_PUPD_PU);
    LCD_BL(1);                      /* start with backlight ON */

    /* 3. Configure TIM3 for ~3kHz update interrupt */
    TIM3->PSC  = 71;                /* 72MHz / 72 = 1MHz timer clock */
    TIM3->ARR  = 332;               /* 1MHz / 333 ≈ 3003 Hz ISR rate */
    TIM3->DIER |= 1 << 0;           /* UIE = 1: enable update interrupt */
    TIM3->CR1  |= 1 << 7;           /* ARPE: auto-reload preload enable */

    /* 4. Enable TIM3 IRQ in NVIC — lowest priority, safe for μC/OS-II */
    NVIC_SetPriority(TIM3_IRQn, 15);
    NVIC_EnableIRQ(TIM3_IRQn);

    /* 5. Initialize software PWM state */
    sw_bl_pwm_cnt   = 0;
    sw_bl_pwm_level = 2;            /* default mid brightness */

    /* 6. Start TIM3 */
    TIM3->CR1 |= 1 << 0;            /* CEN = 1 */
}

/**
 * @brief       Set LCD backlight brightness level (software PWM)
 * @param       level: 0=off, 1=dim(33%), 2=mid(66%), 3=bright(100%)
 * @retval      None
 */
static void lcd_bl_set_brightness(uint8_t level)
{
    if (level == 0)
    {
        /* Off: stop timer, kill backlight */
        TIM3->CR1 &= ~(uint16_t)(1 << 0);   /* CEN = 0 */
        LCD_BL(0);
        sw_bl_pwm_level = 0;
    }
    else if (level > 3)
    {
        level = 3;
    }

    if (level > 0)
    {
        sw_bl_pwm_level = level;

        /* 100% brightness: stop timer, just drive GPIO high */
        if (level >= 3)
        {
            TIM3->CR1 &= ~(uint16_t)(1 << 0);   /* stop timer */
            LCD_BL(1);
        }
        else
        {
            /* Ensure timer is running for PWM levels 1-2 */
            if (!(TIM3->CR1 & (1 << 0)))
            {
                TIM3->CR1 |= 1 << 0;            /* restart timer */
            }
        }
    }
}

/**
 * @brief       Initialize ebook reader context
 * @param       ctx     : pointer to context struct
 * @param       f_txt   : opened file handle (FIL struct will be copied)
 * @param       pname   : full file path string
 * @param       fname   : display filename
 * @retval      0: success; 1: memory allocation failed
 */
static uint8_t ebook_ctx_init(ebook_ctx_t *ctx, FIL *f_txt, uint8_t *pname, uint8_t *fname)
{
    gui_memset(ctx, 0, sizeof(ebook_ctx_t));

    /* Store file handle pointer (heap-allocated, avoid large FIL on stack) */
    ctx->file      = f_txt;
    ctx->file_size = f_txt->obj.objsize;
    ctx->page_start = 0;
    ctx->page_end   = 0;
    ctx->page_num   = 0;

    /* Text area: below top bar, above bottom bar, 2px top + 6px bottom margin */
    ctx->text_x      = 2;
    ctx->text_y      = gui_phy.tbheight + 2;
    ctx->text_width  = lcddev.width - 4;
    ctx->font_size   = gui_phy.tbfsize;
    {
        uint16_t prgb_y = lcddev.height - gui_phy.tbheight - PRGB_MARGIN - PRGB_HEIGHT;
        uint16_t max_text_bottom = prgb_y - PRGB_GAP;
        ctx->text_height = ((max_text_bottom - ctx->text_y)
                            / ctx->font_size) * ctx->font_size;
    }

    ctx->prev_count = 0;

    /* Allocate working buffers */
    ctx->raw_buf  = gui_memin_malloc(EBOOK_RAW_BUF_SIZE);
    ctx->page_buf = gui_memin_malloc(EBOOK_PAGE_BUF_SIZE);
    ctx->path     = pname;
    ctx->fname    = fname;

    if (!ctx->raw_buf || !ctx->page_buf) return 1;

    ctx->bl_level = 2;    /* 默认中档亮度 */

    return 0;
}

/**
 * @brief       Deinitialize ebook reader context, free all resources
 * @param       ctx     : pointer to context struct
 * @retval      None
 */
static void ebook_ctx_deinit(ebook_ctx_t *ctx)
{
    g_ebook_prgb = NULL;  /* 安全：确保静态指针不会悬空 */
    /* Disable software backlight PWM — stop ISR + timer, keep backlight ON */
    NVIC_DisableIRQ(TIM3_IRQn);
    TIM3->DIER &= ~(uint16_t)(1 << 0);   /* UIE = 0 */
    TIM3->CR1  &= ~(uint16_t)(1 << 0);   /* CEN = 0 */
    LCD_BL(1);                            /* restore GPIO backlight ON for file browser */
    if (!ctx) return;
    if (ctx->raw_buf)  { gui_memin_free(ctx->raw_buf);  ctx->raw_buf  = NULL; }
    if (ctx->page_buf) { gui_memin_free(ctx->page_buf); ctx->page_buf = NULL; }
    if (ctx->bookmarks) { ebook_bm_free(ctx->bookmarks); ctx->bookmarks = NULL; }
    ctx->file_size  = 0;
    ctx->page_start = 0;
    ctx->page_end   = 0;
    ctx->page_num   = 0;
    gui_memin_free(ctx);
}

/**
 * @brief       Push current page_start onto history stack
 * @param       ctx     : pointer to context struct
 * @retval      None
 */
static void ebook_history_push(ebook_ctx_t *ctx)
{
    if (ctx->prev_count < EBOOK_PREV_MAX)
    {
        ctx->prev_starts[ctx->prev_count] = ctx->page_start;
        ctx->prev_count++;
    }
    else
    {
        /* Queue full: drop oldest entry, shift left */
        uint8_t i;
        for (i = 0; i < EBOOK_PREV_MAX - 1; i++)
        {
            ctx->prev_starts[i] = ctx->prev_starts[i + 1];
        }
        ctx->prev_starts[EBOOK_PREV_MAX - 1] = ctx->page_start;
    }
}

/**
 * @brief       Pop previous page_start from history stack
 * @param       ctx     : pointer to context struct
 * @retval      Previous page_start value; 0 if history is empty
 */
static uint32_t ebook_history_pop(ebook_ctx_t *ctx)
{
    if (ctx->prev_count == 0) return 0;
    ctx->prev_count--;
    return ctx->prev_starts[ctx->prev_count];
}

/**
 * @brief       Align a byte offset backward to a valid GBK character boundary.
 * @param       ctx     : pointer to context struct (for file access)
 * @param       offset  : candidate byte offset to align
 * @retval      Aligned offset (guaranteed to start at a GBK char boundary)
 * @note        If offset lands on the second byte of a GBK double-byte
 *              character, backtrack 1 byte so scanning starts at the lead byte.
 *              This prevents garbled text when jumping via progress bar drag.
 */
static uint32_t ebook_align_gbk(ebook_ctx_t *ctx, uint32_t offset)
{
    if (offset == 0 || offset >= ctx->file_size) return offset;

    {
        uint8_t buf[2];
        UINT    br;

        /* Loop backward: keep backtracking while we're on the second byte
         * of a valid GBK double-byte character.  A single step is usually
         * enough, but consecutive GBK lead bytes (e.g. 0x81 0x81 0x40)
         * may require two steps.  The loop is bounded by design — GBK is
         * always 2 bytes, so we can never be more than 1 byte deep.      */
        while (offset > 0)
        {
            f_lseek(ctx->file, offset - 1);
            f_read(ctx->file, buf, 2, &br);

            /* GBK: lead byte ∈ [0x81, 0xFE], trail byte ∈ [0x40, 0xFE] */
            if (br >= 2 && buf[0] >= 0x81 && buf[1] >= 0x40)
            {
                offset--;   /* buf[1] is a trail byte — step back to lead byte */
            }
            else
            {
                break;      /* found a valid boundary */
            }
        }
    }

    return offset;
}

/**
 * @brief       Core function: scan forward from page_start through the file,
 *              simulating gui_show_string() layout rules to compute page_end.
 * @param       ctx     : pointer to context struct (reads page_start, writes page_end)
 * @retval      None
 * @note        Layout rules (identical to gui_show_string):
 *              - GBK: byte >= 0x81 AND next byte >= 0x40  => char_width = font, 2 bytes
 *              - ASCII: otherwise                           => char_width = font/2, 1 byte
 *              - CR+LF or LF: forced newline
 *              - Auto-wrap: x + char_width > endx + 1  => newline
 *              - Page full: y + font > max_y after newline/auto-wrap
 */
static void ebook_scan_page_forward(ebook_ctx_t *ctx)
{
    uint16_t endx, max_y, x, y, font;
    uint32_t file_pos;
    uint8_t  pending_gbk = 0;
    UINT     bread;
    uint16_t i;

    font  = ctx->font_size;
    endx  = ctx->text_x + ctx->text_width - 1;
    max_y = ctx->text_y + ctx->text_height - 1;
    x     = ctx->text_x;
    y     = ctx->text_y;
    file_pos = ctx->page_start;

    while (file_pos < ctx->file_size)
    {
        f_lseek(ctx->file, file_pos);
        f_read(ctx->file, ctx->raw_buf, EBOOK_RAW_BUF_SIZE, &bread);

        if (bread == 0) break;

        i = 0;

        /* --- Handle GBK lead byte pending from previous chunk --- */
        if (pending_gbk)
        {
            /* pending byte is at file_pos - 1 */
            if (ctx->raw_buf[0] >= 0x40)
            {
                /* Valid GBK double-byte char */
                if (x + font > endx + 1)
                {
                    /* Auto-wrap */
                    y += font;
                    x  = ctx->text_x;
                }
                if (y > max_y)
                {
                    /* Page full �??? start next page from the pending GBK lead byte */
                    ctx->page_end = file_pos - 1;
                    return;
                }
                x += font;
                i = 1;  /* consumed first byte of new chunk as GBK second byte */
            }
            else
            {
                /* Not a valid GBK pair:
                 * treat pending byte as ASCII, then process raw_buf[0] below */
                if (x + font / 2 > endx + 1)
                {
                    y += font;
                    x  = ctx->text_x;
                }
                if (y > max_y)
                {
                    ctx->page_end = file_pos - 1;
                    return;
                }
                x += font / 2;
                /* i stays 0; raw_buf[0] processed in normal loop */
            }
            pending_gbk = 0;
        }

        /* --- Walk through buffer bytes --- */
        while (i < bread)
        {
            uint32_t abs_pos = file_pos + i;  /* file offset of current byte */
            uint8_t  byte    = ctx->raw_buf[i];

            /* ---- Newline: CR+LF or LF ---- */
            if ((byte == 0x0D && (i + 1 < bread) && ctx->raw_buf[i + 1] == 0x0A)
                    || byte == 0x0A)
            {
                y += font;
                x  = ctx->text_x;

                if (y > max_y)
                {
                    /* Newline itself pushes us past the page.
                     * Include the newline bytes so the next page starts clean. */
                    ctx->page_end = abs_pos + ((byte == 0x0D) ? 2 : 1);
                    return;
                }

                if (byte == 0x0D) i += 2;
                else              i += 1;
                continue;
            }

            /* ---- GBK Chinese (lead byte >= 0x81) ---- */
            if (byte >= 0x81)
            {
                if (i + 1 < bread)
                {
                    /* Second byte available */
                    if (ctx->raw_buf[i + 1] >= 0x40)
                    {
                        /* Valid GBK character */
                        if (x + font > endx + 1)
                        {
                            /* Auto-wrap */
                            y += font;
                            x  = ctx->text_x;
                        }
                        if (y > max_y)
                        {
                            /* This char would overflow �??? start next page here */
                            ctx->page_end = abs_pos;
                            return;
                        }
                        x += font;
                        i += 2;
                    }
                    else
                    {
                        /* Invalid GBK second byte: treat first byte as ASCII */
                        if (x + font / 2 > endx + 1)
                        {
                            y += font;
                            x  = ctx->text_x;
                        }
                        if (y > max_y)
                        {
                            ctx->page_end = abs_pos;
                            return;
                        }
                        x += font / 2;
                        i += 1;
                    }
                }
                else
                {
                    /* Last byte of buffer may be a GBK lead byte.
                     * Save it and defer decision to next chunk. */
                    pending_gbk = 1;
                    i++;
                }
                continue;
            }

            /* ---- ASCII (byte < 0x81) ---- */
            if (x + font / 2 > endx + 1)
            {
                /* Auto-wrap */
                y += font;
                x  = ctx->text_x;
            }
            if (y > max_y)
            {
                /* This ASCII char would overflow �??? start next page here */
                ctx->page_end = abs_pos;
                return;
            }
            x += font / 2;
            i += 1;
        }

        file_pos += bread;
    }

    /* EOF reached �??? page ends at file end */
    ctx->page_end = ctx->file_size;
}

/**
 * @brief       Read current page content from file and render to LCD
 * @param       ctx     : pointer to context struct
 * @retval      None
 */
static void ebook_draw_page(ebook_ctx_t *ctx)
{
    UINT     bread;
    uint32_t page_size = ctx->page_end - ctx->page_start;

    /* Clear entire area between top and bottom bars with background color.
     * Full-width, full-height fill eliminates any residual pixels from the
     * file browser.  The progress bar (if active) is completely redrawn
     * afterward. */
    {
        uint16_t bg_color = (ctx->bg_mode == 0) ? WHITE : EYECARE_BG;
        gui_fill_rectangle(0, gui_phy.tbheight,
                           lcddev.width,
                           lcddev.height - gui_phy.tbheight * 2,
                           bg_color);
    }

    if (page_size > 0 && page_size < EBOOK_PAGE_BUF_SIZE)
    {
        f_lseek(ctx->file, ctx->page_start);
        f_read(ctx->file, ctx->page_buf, page_size, &bread);
        ctx->page_buf[bread] = '\0';

        gui_show_string(ctx->page_buf,
                        ctx->text_x, ctx->text_y,
                        ctx->text_width, ctx->text_height,
                        ctx->font_size, BLACK);
    }

    /* Redraw progress bar in full (full-area fill above covers it entirely) */
    if (g_ebook_prgb && ctx->file_size > 0)
    {
        g_ebook_prgb->curpos = ctx->page_start;
        progressbar_draw_progressbar(g_ebook_prgb);
    }
}

/**
 * @brief       Display current page number on the top bar (right side)
 * @param       ctx     : pointer to context struct
 * @retval      None
 */
static void ebook_show_page_num(ebook_ctx_t *ctx)
{
    uint8_t  pbuf[16];

    if (ctx->page_num > 0)
    {
        uint16_t top_y = (gui_phy.tbheight - gui_phy.tbfsize) / 2;

        /* Show page number at top-right corner, no background fill */
        gui_show_string(gui_num2str(pbuf, ctx->page_num),
                        lcddev.width - 50, top_y,
                        46, gui_phy.tbfsize,
                        gui_phy.tbfsize, WHITE);
    }
}

/**
 * @brief       Turn page in given direction
 * @param       ctx     : pointer to context struct
 * @param       dir     : +1 forward (next page), -1 backward (previous page)
 * @retval      0: success; 1: cannot turn (at start/end)
 */
static uint8_t ebook_turn_page(ebook_ctx_t *ctx, int dir)
{
    if (dir > 0)
    {
        /* ----- Forward: next page ----- */
        if (ctx->page_end >= ctx->file_size) return 1;  /* already at end */

        ebook_history_push(ctx);
        ctx->page_start = ctx->page_end;
        ctx->page_num++;
    }
    else if (dir < 0)
    {
        /* ----- Backward: previous page ----- */
        if (ctx->prev_count == 0) return 1;  /* no history */

        ctx->page_start = ebook_history_pop(ctx);
        if (ctx->page_num > 1) ctx->page_num--;
    }
    else
    {
        return 1;
    }

    ebook_scan_page_forward(ctx);   /* compute page_end */
    ebook_draw_page(ctx);            /* render to LCD */
    ebook_show_page_num(ctx);       /* update page indicator */

    return 0;
}

/**
 * @brief       Ebook reader main entry point
 * @param       None
 * @retval      0: normal exit; non-zero: error
 */
uint8_t ebook_play(void)
{
    ebook_ctx_t *ctx = NULL;  /* heap-allocated on book open, saves 216B on stack */
    FIL *f_txt;
    DIR ebookdir;
    FILINFO *ebookinfo;
    uint8_t res;
    uint8_t rval = 0;
    uint8_t *pname = 0;
    uint8_t *fn;
    uint8_t errtype = 0;
    uint8_t ebooksta = 0;   /* 0: file browsing; 1: reading */

    uint8_t *buf;

    _btn_obj *rbtn;
    _btn_obj *fbtn;
    _btn_obj *bbtn;
    _btn_obj *ebtn;
    _btn_obj *brbtn;             /* 亮度按钮 */
    _progressbar_obj *prgb = NULL;   /* 进度条 */
    uint8_t  prgb_dragging = 0;      /* 拖拽状态标记：1=正在拖拽进度条 */
    uint16_t prgb_y;                 /* 进度条 Y 坐标（在创建时计算） */
    _filelistbox_obj *flistbox;
    _filelistbox_list *filelistx;

    /* --- Touch swipe tracking --- */
    uint8_t      swipe_tracking = 0;
    uint16_t     swipe_start_x = 0;
    uint16_t     swipe_start_y = 0;
    uint16_t     raw_x, raw_y;
    uint16_t     raw_sta;
    uint8_t      key_val;

    /* --- File browser setup --- */
    app_filebrower((uint8_t *)APP_MFUNS_CAPTION_TBL[0][gui_phy.language], 0X07);
    flistbox = filelistbox_creat(0, gui_phy.tbheight, lcddev.width,
                                  lcddev.height - gui_phy.tbheight * 2,
                                  1, gui_phy.listfsize);

    if (flistbox == NULL) rval = 1;
    else
    {
        flistbox->fliter = FLBOX_FLT_TEXT | FLBOX_FLT_LRC;
        filelistbox_add_disk(flistbox);
        filelistbox_draw_listbox(flistbox);
    }

    f_txt = (FIL *)gui_memin_malloc(sizeof(FIL));
    if (f_txt == NULL) rval = 1;

    rbtn = btn_creat(lcddev.width - 2 * gui_phy.tbfsize - 8 - 1,
                     lcddev.height - gui_phy.tbheight,
                     2 * gui_phy.tbfsize + 8, gui_phy.tbheight - 1,
                     0, 0x03);
    ebookinfo = (FILINFO *)gui_memin_malloc(sizeof(FILINFO));

    if (!ebookinfo || !rbtn) rval = 1;
    else
    {
        rbtn->caption   = (uint8_t *)GUI_BACK_CAPTION_TBL[gui_phy.language];
        rbtn->font      = gui_phy.tbfsize;
        rbtn->bcfdcolor = WHITE;
        rbtn->bcfucolor = WHITE;
        btn_draw(rbtn);
    }

    /* Font button - positioned to the left of return button */
    fbtn = btn_creat(lcddev.width - 4 * gui_phy.tbfsize - 21,
                     lcddev.height - gui_phy.tbheight,
                     2 * gui_phy.tbfsize + 8, gui_phy.tbheight - 1,
                     0, 0x03);

    if (fbtn)
    {
        fbtn->caption   = (uint8_t *)"\xD7\xD6\xCC\xE5";   /* "字体" in GBK */
        fbtn->font      = gui_phy.tbfsize;
        fbtn->bcfdcolor = WHITE;
        fbtn->bcfucolor = WHITE;
        btn_draw(fbtn);
    }

    /* Bookmark button - positioned to the left of font button */
    bbtn = btn_creat(lcddev.width - 6 * gui_phy.tbfsize - 34,
                     lcddev.height - gui_phy.tbheight,
                     2 * gui_phy.tbfsize + 8, gui_phy.tbheight - 1,
                     0, 0x03);

    if (bbtn)
    {
        bbtn->caption   = (uint8_t *)"\xCA\xE9\xC7\xA9";   /* "书签" in GBK */
        bbtn->font      = gui_phy.tbfsize;
        bbtn->bcfdcolor = WHITE;
        bbtn->bcfucolor = WHITE;
        btn_draw(bbtn);
    }
    /* Eye-care button - positioned to the left of bookmark button */
    ebtn = btn_creat(lcddev.width - 8 * gui_phy.tbfsize - 47,
                     lcddev.height - gui_phy.tbheight,
                     2 * gui_phy.tbfsize + 8, gui_phy.tbheight - 1,
                     0, 0x03);

    if (ebtn)
    {
        ebtn->caption   = (uint8_t *)"\xBB\xA4\xD1\xDB";   /* "护眼" in GBK */
        ebtn->font      = gui_phy.tbfsize;
        ebtn->bcfdcolor = WHITE;
        ebtn->bcfucolor = WHITE;
        btn_draw(ebtn);
    }

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

    buf = gui_memin_malloc(1024);
    if (!buf) rval = 1;

    if (rval) errtype = 1;

    /* ========================================================================
     *  Main event loop
     * ======================================================================== */
    while (rval == 0)
    {
        /* ---- Scan input ---- */
        tp_dev.scan(0);

        /* Save raw touch data BEFORE input system processes it */
        raw_x   = tp_dev.x[0];
        raw_y   = tp_dev.y[0];
        raw_sta = tp_dev.sta;

        in_obj.get_key(&tp_dev, IN_TYPE_TOUCH);

        key_val = key_scan(1);
        if (key_val) in_obj.get_key((void *)(uint32_t)key_val, IN_TYPE_KEY);

        delay_ms(5);

        /* ================================================================
         *  Handle system_task_return (TPAD / task switch)
         * ================================================================ */
        if (system_task_return)
        {
            if (ebooksta)
            {
                /* Leave reading state, go back to file browsing */
                if (prgb) { progressbar_delete(prgb); prgb = NULL; }
                /* Disable software backlight PWM before returning to browse */
                NVIC_DisableIRQ(TIM3_IRQn);
                TIM3->DIER &= ~(uint16_t)(1 << 0);   /* UIE = 0 */
                TIM3->CR1  &= ~(uint16_t)(1 << 0);   /* CEN = 0 */
                LCD_BL(1);
                g_ebook_prgb = NULL;
                ebook_ctx_deinit(ctx);
                ctx = NULL;
                gui_memin_free(pname);
                pname = NULL;
                ebooksta = 0;
                app_filebrower((uint8_t *)APP_MFUNS_CAPTION_TBL[0][gui_phy.language], 0X07);
                btn_draw(rbtn);
                filelistbox_rebuild_filelist(flistbox);
                system_task_return = 0;
                swipe_tracking = 0;
            }
            else break;
        }

        /* ================================================================
         *  State 0: File browsing
         * ================================================================ */
        if (ebooksta == 0)
        {
            filelistbox_check(flistbox, &in_obj);
            res = btn_check(rbtn, &in_obj);

            if (res)
            {
                if (((rbtn->sta & 0X80) == 0))
                {
                    filelistx = filelist_search(flistbox->list, flistbox->selindex);

                    if (filelistx->type == FICO_DISK)
                    {
                        break;
                    }
                    else filelistbox_back(flistbox);
                }
            }

            /* Double-click on a file to enter reading state */
            if (flistbox->dbclick == 0X81)
            {
                rval = f_opendir(&ebookdir, (const TCHAR *)flistbox->path);
                if (rval) break;

                ff_enter(ebookdir.obj.fs);
                dir_sdi(&ebookdir,
                        flistbox->findextbl[flistbox->selindex - flistbox->foldercnt]);
                ff_leave(ebookdir.obj.fs);
                rval = f_readdir(&ebookdir, ebookinfo);
                if (rval) break;

                fn = (uint8_t *)(ebookinfo->fname);
                pname = gui_memin_malloc(strlen((const char *)fn)
                                         + strlen((const char *)flistbox->path) + 2);

                if (pname == NULL) { rval = 1; break; }

                /* Draw top bar with filename */
                app_gui_tcbar(0, 0, lcddev.width, gui_phy.tbheight, 0x02);
                gui_show_string(fn, 2,
                                (gui_phy.tbheight - gui_phy.tbfsize) / 2,
                                lcddev.width - 2, gui_phy.tbfsize,
                                gui_phy.tbfsize, WHITE);

                pname = gui_path_name(pname, flistbox->path, fn);
                rval = f_open(f_txt, (const TCHAR *)pname, FA_READ);
                if (rval) break;

                /* Allocate context on heap (saves ~216B on stack vs local struct) */
                ctx = (ebook_ctx_t *)gui_memin_malloc(sizeof(ebook_ctx_t));
                if (!ctx) { rval = 1; break; }

                /* Initialize ebook context (stores f_txt pointer, allocates buffers) */
                if (ebook_ctx_init(ctx, f_txt, pname, fn))
                {
                    gui_memin_free(ctx);
                    ctx = NULL;
                    errtype = 1;
                    break;
                }

                /* f_txt is now owned by ctx->file �?? do NOT free it here */

                /* Load and display first page */
                ctx->page_num = 1;
                ebook_scan_page_forward(ctx);
                ebook_draw_page(ctx);
                ebook_show_page_num(ctx);

                /* Load bookmarks for this book */
                ctx->bookmarks = ebook_bm_load(pname);

                system_task_return = 0;
                flistbox->dbclick = 0;
                ebooksta = 1;
                lcd_bl_pwm_init();                       /* 启动背光 PWM */
                lcd_bl_set_brightness(ctx->bl_level);    /* 应用亮度档位 */
                btn_draw(fbtn);   /* ensure font button is visible */
                btn_draw(bbtn);   /* ensure bookmark button is visible */
                btn_draw(ebtn);   /* ensure eye-care button is visible */
                btn_draw(brbtn);  /* ensure brightness button is visible */

                /* Create progress bar */
                prgb_y = lcddev.height - gui_phy.tbheight
                         - PRGB_MARGIN - PRGB_HEIGHT;
                prgb = progressbar_creat(ctx->text_x, prgb_y,
                                         ctx->text_width, PRGB_HEIGHT,
                                         0x60);  /* 水平+填充+滑块 */
                if (prgb)
                {
                    prgb->totallen = ctx->file_size;
                    prgb->curpos   = 0;
                    /* 自定义颜色（适配白色背景） */
                    prgb->rimcolor    = 0x8430;  /* GRAY 边框 */
                    prgb->btncolor    = 0x0000;  /* BLACK 滑块 */
                    prgb->inbkcolora  = 0xCE59;  /* 浅灰背景 */
                    prgb->inbkcolorb  = 0xCE59;
                    prgb->infcolora   = 0x7BCF;  /* 浅蓝绿填充 */
                    prgb->infcolorb   = 0x7BCF;
                    progressbar_draw_progressbar(prgb);
                    g_ebook_prgb = prgb;  /* 注册到静态指针 */
                }
            }
        }

        /* ================================================================
         *  State 1: Reading (page turning + swipe + keys)
         * ================================================================ */
        if (ebooksta == 1)
        {
            /* ---- Check font button ---- */
            if (fbtn)
            {
                res = btn_check(fbtn, &in_obj);

                if (res && ((fbtn->sta & 0X80) == 0))
                {
                uint16_t panel_x;
                uint16_t panel_y;
                uint16_t font_sel;
                _btn_obj *btn12, *btn16, *btn24, *btn_cancel;

                panel_x  = (lcddev.width
                            - (3 * FONT_SEL_BTN_W + 4 * FONT_SEL_PAD)) / 2;
                panel_y  = lcddev.height / 3;
                font_sel = 0;

                /* Draw panel background over current page */
                gui_fill_rectangle(panel_x, panel_y,
                                   3 * FONT_SEL_BTN_W + 4 * FONT_SEL_PAD,
                                   FONT_SEL_BTN_H + 36 + 3 * FONT_SEL_PAD,
                                   0xC618);

                /* Create font size buttons (12 / 16 / 24) */
                btn12 = btn_creat(panel_x + FONT_SEL_PAD,
                                  panel_y + FONT_SEL_PAD,
                                  FONT_SEL_BTN_W, FONT_SEL_BTN_H,
                                  0, BTN_TYPE_ANG);
                btn16 = btn_creat(panel_x + FONT_SEL_PAD * 2 + FONT_SEL_BTN_W,
                                  panel_y + FONT_SEL_PAD,
                                  FONT_SEL_BTN_W, FONT_SEL_BTN_H,
                                  0, BTN_TYPE_ANG);
                btn24 = btn_creat(panel_x + FONT_SEL_PAD * 3 + FONT_SEL_BTN_W * 2,
                                  panel_y + FONT_SEL_PAD,
                                  FONT_SEL_BTN_W, FONT_SEL_BTN_H,
                                  0, BTN_TYPE_ANG);
                btn_cancel = btn_creat(panel_x + FONT_SEL_PAD,
                                       panel_y + FONT_SEL_PAD * 2 + FONT_SEL_BTN_H,
                                       3 * FONT_SEL_BTN_W + 2 * FONT_SEL_PAD,
                                       36, 0, BTN_TYPE_ANG);

                /* Configure "12" button */
                if (btn12)
                {
                    btn12->caption   = (uint8_t *)"12";
                    btn12->font      = 16;
                    btn12->bcfucolor = WHITE;
                    btn12->bcfdcolor = BLACK;
                    btn_draw(btn12);
                }

                /* Configure "16" button */
                if (btn16)
                {
                    btn16->caption   = (uint8_t *)"16";
                    btn16->font      = 16;
                    btn16->bcfucolor = WHITE;
                    btn16->bcfdcolor = BLACK;
                    btn_draw(btn16);
                }

                /* Configure "24" button */
                if (btn24)
                {
                    btn24->caption   = (uint8_t *)"24";
                    btn24->font      = 16;
                    btn24->bcfucolor = WHITE;
                    btn24->bcfdcolor = BLACK;
                    btn_draw(btn24);
                }

                /* Configure cancel button */
                if (btn_cancel)
                {
                    btn_cancel->caption   = (uint8_t *)GUI_BACK_CAPTION_TBL[gui_phy.language];
                    btn_cancel->font      = 16;
                    btn_cancel->bcfucolor = WHITE;
                    btn_cancel->bcfdcolor = BLACK;
                    btn_draw(btn_cancel);
                }

                /* Modal input loop: block until selection or cancel */
                while (!font_sel)
                {
                    tp_dev.scan(0);
                    in_obj.get_key(&tp_dev, IN_TYPE_TOUCH);

                    if (system_task_return)
                    {
                        break;  /* propagate to outer handler */
                    }

                    delay_ms(10);

                    if (btn_check(btn12, &in_obj))
                    { if ((btn12->sta & 0X80) == 0) font_sel = 12; }
                    if (btn_check(btn16, &in_obj))
                    { if ((btn16->sta & 0X80) == 0) font_sel = 16; }
                    if (btn_check(btn24, &in_obj))
                    { if ((btn24->sta & 0X80) == 0) font_sel = 24; }
                    if (btn_check(btn_cancel, &in_obj))
                    { if ((btn_cancel->sta & 0X80) == 0) break; }
                }

                /* Destroy dialog buttons */
                btn_delete(btn12);
                btn_delete(btn16);
                btn_delete(btn24);
                btn_delete(btn_cancel);

                /* Apply font change or restore page */
                if (font_sel)
                {
                    ctx->font_size  = (uint8_t)font_sel;
                    {
                        uint16_t prgb_y2 = lcddev.height - gui_phy.tbheight
                                          - PRGB_MARGIN - PRGB_HEIGHT;
                        uint16_t max_btm = prgb_y2 - PRGB_GAP;
                        ctx->text_height = ((max_btm - ctx->text_y)
                                            / ctx->font_size) * ctx->font_size;
                    }
                    ctx->prev_count = 0;
                    ctx->page_num   = 1;
                    ebook_scan_page_forward(ctx);
                    ebook_draw_page(ctx);
                    ebook_show_page_num(ctx);
                }
                else
                {
                    /* Restore page content (panel was drawn over it) */
                    ebook_draw_page(ctx);
                }

                    swipe_tracking = 0;
                    continue;
                }
            }

            /* ---- Check bookmark button ---- */
            if (bbtn)
            {
                res = btn_check(bbtn, &in_obj);

                if (res && ((bbtn->sta & 0X80) == 0))
                {
                    /* ================================================================
                     *  Bookmark panel: two-level modal UI
                     *  Panel 1: bookmark list management
                     *  Panel 2: single bookmark operations (jump / delete / cancel)
                     * ================================================================ */
                    uint16_t bm_px, bm_py, bm_pw, bm_ph;
                    uint16_t bm_title_y, bm_addbtn_y, bm_lby, bm_lbh, bm_backbtn_y;
                    _btn_obj *bm_btn_add, *bm_btn_back;
                    _listbox_obj *bm_lb;
                    uint8_t  bm_panel1_run;
                    uint8_t  bm_selected_idx;
                    uint8_t *bm_item_names[EBOOK_BM_MAX];
                    uint8_t  i;

                    /* Init name pointer array */
                    for (i = 0; i < EBOOK_BM_MAX; i++) bm_item_names[i] = NULL;

                    /* Ensure bookmarks are loaded */
                    if (!ctx->bookmarks)
                    {
                        ctx->bookmarks = ebook_bm_load(ctx->path);
                    }

                    /* ---- Panel 1: Bookmark Management ---- */
                    bm_pw = 260;
                    bm_lbh = 6 * gui_phy.listheight;  /* 6 visible rows */
                    bm_ph  = 8 + (gui_phy.tbfsize + 4) + 6 + 36 + 6 + bm_lbh + 6 + 36 + 8;
                    bm_px  = (lcddev.width  - bm_pw) / 2;
                    bm_py  = (lcddev.height - bm_ph) / 2;

                    bm_title_y   = bm_py + 8;
                    bm_addbtn_y  = bm_title_y + gui_phy.tbfsize + 4 + 6;
                    bm_lby       = bm_addbtn_y + 36 + 6;
                    bm_backbtn_y = bm_lby + bm_lbh + 6;

                    /* Draw panel background */
                    gui_fill_rectangle(bm_px, bm_py, bm_pw, bm_ph, 0xC618);

                    /* Draw title */
                    gui_show_string((uint8_t *)"\xCA\xE9\xC7\xA9\xB9\xDC\xC0\xED",  /* "书签管理" */
                                    bm_px + 8, bm_title_y,
                                    bm_pw - 16, gui_phy.tbfsize,
                                    gui_phy.tbfsize, WHITE);

                    /* "Add bookmark" button */
                    bm_btn_add = btn_creat(bm_px + 10, bm_addbtn_y,
                                           bm_pw - 20, 36, 0, BTN_TYPE_ANG);
                    if (bm_btn_add)
                    {
                        bm_btn_add->caption   = (uint8_t *)
                            "\x2B\x20\xCC\xED\xBC\xD3\xB5\xB1\xC7\xB0\xD2\xB3\xCE\xAA\xCA\xE9\xC7\xA9";
                            /* "+ 添加当前页为书签" */
                        bm_btn_add->font      = gui_phy.tbfsize;
                        bm_btn_add->bcfucolor = WHITE;
                        bm_btn_add->bcfdcolor = BLACK;
                        btn_draw(bm_btn_add);
                    }

                    /* Bookmark listbox */
                    bm_lb = listbox_creat(bm_px + 10, bm_lby,
                                          bm_pw - 20, bm_lbh,
                                          1, gui_phy.listfsize);
                    if (bm_lb && ctx->bookmarks)
                    {
                        for (i = 0; i < ctx->bookmarks->count; i++)
                        {
                            uint8_t tmp[8];
                            bm_item_names[i] = gui_memin_malloc(32);
                            if (bm_item_names[i])
                            {
                                strcpy((char *)bm_item_names[i], "\xCA\xE9\xC7\xA9");   /* "书签" */
                                gui_num2str(tmp, i + 1);
                                strcat((char *)bm_item_names[i], (char *)tmp);
                                strcat((char *)bm_item_names[i], ": \xB5\xDA");         /* ": 第" */
                                gui_num2str(tmp, ctx->bookmarks->entries[i].page_num);
                                strcat((char *)bm_item_names[i], (char *)tmp);
                                strcat((char *)bm_item_names[i], " \xD2\xB3");          /* " 页" */
                                listbox_addlist(bm_lb, bm_item_names[i]);
                            }
                        }
                        if (ctx->bookmarks->count > 0)
                            listbox_draw_listbox(bm_lb);
                    }

                    /* "Return" button */
                    bm_btn_back = btn_creat(bm_px + 10, bm_backbtn_y,
                                            bm_pw - 20, 36, 0, BTN_TYPE_ANG);
                    if (bm_btn_back)
                    {
                        bm_btn_back->caption   = (uint8_t *)GUI_BACK_CAPTION_TBL[gui_phy.language];
                        bm_btn_back->font      = gui_phy.tbfsize;
                        bm_btn_back->bcfucolor = WHITE;
                        bm_btn_back->bcfdcolor = BLACK;
                        btn_draw(bm_btn_back);
                    }

                    /* ============================================================
                     *  Panel 1 modal loop
                     * ============================================================ */
                    bm_panel1_run = 1;
                    while (bm_panel1_run)
                    {
                        tp_dev.scan(0);
                        in_obj.get_key(&tp_dev, IN_TYPE_TOUCH);

                        if (system_task_return)
                        {
                            bm_panel1_run = 0;
                            break;
                        }

                        delay_ms(10);

                        /* --- "Add bookmark" button --- */
                        if (bm_btn_add && btn_check(bm_btn_add, &in_obj))
                        {
                            if ((bm_btn_add->sta & 0X80) == 0)
                            {
                                uint8_t add_r = ebook_bm_add(ctx->bookmarks,
                                                              ctx->page_start,
                                                              ctx->page_num);
                                if (add_r == 0)
                                {
                                    /* Success: save and rebuild listbox */
                                    ebook_bm_save(ctx->bookmarks);

                                    /* Free old listbox and names */
                                    if (bm_lb)
                                    {
                                        for (i = 0; i < EBOOK_BM_MAX; i++)
                                        {
                                            if (bm_item_names[i])
                                            { gui_memin_free(bm_item_names[i]); bm_item_names[i] = NULL; }
                                        }
                                        listbox_delete(bm_lb);
                                    }

                                    /* Recreate listbox */
                                    bm_lb = listbox_creat(bm_px + 10, bm_lby,
                                                          bm_pw - 20, bm_lbh,
                                                          1, gui_phy.listfsize);
                                    if (bm_lb)
                                    {
                                        for (i = 0; i < ctx->bookmarks->count; i++)
                                        {
                                            uint8_t tmp[8];
                                            bm_item_names[i] = gui_memin_malloc(32);
                                            if (bm_item_names[i])
                                            {
                                                strcpy((char *)bm_item_names[i], "\xCA\xE9\xC7\xA9");   /* "书签" */
                                                gui_num2str(tmp, i + 1);
                                                strcat((char *)bm_item_names[i], (char *)tmp);
                                                strcat((char *)bm_item_names[i], ": \xB5\xDA");
                                                gui_num2str(tmp,
                                                    ctx->bookmarks->entries[i].page_num);
                                                strcat((char *)bm_item_names[i], (char *)tmp);
                                                strcat((char *)bm_item_names[i], " \xD2\xB3");
                                                listbox_addlist(bm_lb, bm_item_names[i]);
                                            }
                                        }
                                        if (ctx->bookmarks->count > 0)
                                            listbox_draw_listbox(bm_lb);
                                    }
                                }
                                else if (add_r == 1)
                                {
                                    /* Bookmark list full: show toast */
                                    gui_show_string(
                                        (uint8_t *)
                                        "\xCA\xE9\xC7\xA9\xD2\xD1\xC2\xFA\x28\xD7\xEE\xB6\xE0\x31\x30\xB8\xF6\x29",
                                        /* "书签已满(最多10个)" */
                                        bm_px + 10, bm_py + bm_ph - 28,
                                        bm_pw - 20, gui_phy.tbfsize,
                                        gui_phy.tbfsize, BLACK);
                                    delay_ms(800);
                                    gui_fill_rectangle(bm_px + 10, bm_py + bm_ph - 28,
                                                       bm_pw - 20, gui_phy.tbfsize, 0xC618);
                                }
                                else if (add_r == 2)
                                {
                                    /* Already exists: show toast */
                                    gui_show_string(
                                        (uint8_t *)
                                        "\xB8\xC3\xCA\xE9\xC7\xA9\xD2\xD1\xB4\xE6\xD4\xDA",
                                        /* "该书签已存在" */
                                        bm_px + 10, bm_py + bm_ph - 28,
                                        bm_pw - 20, gui_phy.tbfsize,
                                        gui_phy.tbfsize, BLACK);
                                    delay_ms(800);
                                    gui_fill_rectangle(bm_px + 10, bm_py + bm_ph - 28,
                                                       bm_pw - 20, gui_phy.tbfsize, 0xC618);
                                }
                            }
                        }

                        /* --- Listbox: double-click a bookmark item --- */
                        if (bm_lb && ctx->bookmarks->count > 0)
                        {
                            listbox_check(bm_lb, &in_obj);

                            if (bm_lb->dbclick & 0x80)
                            {
                                bm_selected_idx = bm_lb->selindex;

                                if (bm_selected_idx < ctx->bookmarks->count)
                                {
                                    /* ============================================
                                     *  Panel 2: Bookmark Operation
                                     * ============================================ */
                                    uint16_t p2_w, p2_h, p2_x, p2_y;
                                    uint16_t p2_title_y, p2_info_y, p2_btn1_y,
                                             p2_btn2_y, p2_btn3_y;
                                    _btn_obj *p2_btn_jump, *p2_btn_del, *p2_btn_cancel;
                                    uint8_t  p2_run;
                                    uint8_t  p2_info[32];

                                    p2_w = 220;
                                    p2_h = 8 + (gui_phy.tbfsize + 4) + 8
                                           + gui_phy.tbfsize + 8
                                           + 36 + 6 + 36 + 6 + 36 + 8;
                                    p2_x = (lcddev.width  - p2_w) / 2;
                                    p2_y = (lcddev.height - p2_h) / 2;

                                    p2_title_y  = p2_y + 8;
                                    p2_info_y   = p2_title_y + gui_phy.tbfsize + 4 + 8;
                                    p2_btn1_y   = p2_info_y + gui_phy.tbfsize + 8;
                                    p2_btn2_y   = p2_btn1_y + 36 + 6;
                                    p2_btn3_y   = p2_btn2_y + 36 + 6;

                                    /* Draw panel 2 background */
                                    gui_fill_rectangle(p2_x, p2_y, p2_w, p2_h, 0xC618);

                                    /* Title */
                                    gui_show_string(
                                        (uint8_t *)
                                        "\xCA\xE9\xC7\xA9\xB2\xD9\xD7\xF7",  /* "书签操作" */
                                        p2_x + 8, p2_title_y,
                                        p2_w - 16, gui_phy.tbfsize,
                                        gui_phy.tbfsize, WHITE);

                                    /* Bookmark info text */
                                    {
                                        uint8_t tmp2[8];
                                        strcpy((char *)p2_info, "\xCA\xE9\xC7\xA9");     /* "书签" */
                                        gui_num2str(tmp2, bm_selected_idx + 1);
                                        strcat((char *)p2_info, (char *)tmp2);
                                        strcat((char *)p2_info, ": \xB5\xDA");             /* ": 第" */
                                        gui_num2str(tmp2,
                                                   ctx->bookmarks->entries[bm_selected_idx].page_num);
                                        strcat((char *)p2_info, (char *)tmp2);
                                        strcat((char *)p2_info, " \xD2\xB3");              /* " 页" */
                                    }
                                    gui_show_string(p2_info,
                                                    p2_x + 8, p2_info_y,
                                                    p2_w - 16, gui_phy.tbfsize,
                                                    gui_phy.tbfsize, WHITE);

                                    /* "Jump to here" button */
                                    p2_btn_jump = btn_creat(p2_x + 10, p2_btn1_y,
                                                            p2_w - 20, 36, 0, BTN_TYPE_ANG);
                                    if (p2_btn_jump)
                                    {
                                        p2_btn_jump->caption   = (uint8_t *)
                                            "\xCC\xF8\xD7\xAA\xB5\xBD\xB4\xCB\xB4\xA6";
                                            /* "跳转到此处" */
                                        p2_btn_jump->font      = gui_phy.tbfsize;
                                        p2_btn_jump->bcfucolor = WHITE;
                                        p2_btn_jump->bcfdcolor = BLACK;
                                        btn_draw(p2_btn_jump);
                                    }

                                    /* "Delete this bookmark" button */
                                    p2_btn_del = btn_creat(p2_x + 10, p2_btn2_y,
                                                           p2_w - 20, 36, 0, BTN_TYPE_ANG);
                                    if (p2_btn_del)
                                    {
                                        p2_btn_del->caption   = (uint8_t *)
                                            "\xC9\xBE\xB3\xFD\xB4\xCB\xCA\xE9\xC7\xA9";
                                            /* "删除此书签" */
                                        p2_btn_del->font      = gui_phy.tbfsize;
                                        p2_btn_del->bcfucolor = WHITE;
                                        p2_btn_del->bcfdcolor = BLACK;
                                        btn_draw(p2_btn_del);
                                    }

                                    /* "Cancel" button */
                                    p2_btn_cancel = btn_creat(p2_x + 10, p2_btn3_y,
                                                              p2_w - 20, 36, 0, BTN_TYPE_ANG);
                                    if (p2_btn_cancel)
                                    {
                                        p2_btn_cancel->caption   = (uint8_t *)
                                            GUI_BACK_CAPTION_TBL[gui_phy.language];
                                        p2_btn_cancel->font      = gui_phy.tbfsize;
                                        p2_btn_cancel->bcfucolor = WHITE;
                                        p2_btn_cancel->bcfdcolor = BLACK;
                                        btn_draw(p2_btn_cancel);
                                    }

                                    /* ---- Panel 2 modal loop ---- */
                                    p2_run = 1;
                                    while (p2_run)
                                    {
                                        tp_dev.scan(0);
                                        in_obj.get_key(&tp_dev, IN_TYPE_TOUCH);

                                        if (system_task_return)
                                        {
                                            p2_run = 0;
                                            bm_panel1_run = 0;
                                            break;
                                        }

                                        delay_ms(10);

                                        /* Jump to bookmark */
                                        if (p2_btn_jump
                                                && btn_check(p2_btn_jump, &in_obj))
                                        {
                                            if ((p2_btn_jump->sta & 0X80) == 0)
                                            {
                                                /* Seek to bookmarked position */
                                                ctx->page_start =
                                                    ctx->bookmarks->entries[bm_selected_idx].file_offset;
                                                ctx->prev_count = 0;
                                                ctx->page_num   =
                                                    ctx->bookmarks->entries[bm_selected_idx].page_num;
                                                ebook_scan_page_forward(ctx);
                                                ebook_draw_page(ctx);
                                                ebook_show_page_num(ctx);
                                                p2_run = 0;
                                                bm_panel1_run = 0;
                                            }
                                        }

                                        /* Delete bookmark */
                                        if (p2_btn_del
                                                && btn_check(p2_btn_del, &in_obj))
                                        {
                                            if ((p2_btn_del->sta & 0X80) == 0)
                                            {
                                                ebook_bm_delete(ctx->bookmarks, bm_selected_idx);
                                                ebook_bm_save(ctx->bookmarks);
                                                p2_run = 0;
                                                /* back to panel 1, which will rebuild listbox */
                                            }
                                        }

                                        /* Cancel - back to panel 1 */
                                        if (p2_btn_cancel
                                                && btn_check(p2_btn_cancel, &in_obj))
                                        {
                                            if ((p2_btn_cancel->sta & 0X80) == 0)
                                            {
                                                p2_run = 0;
                                            }
                                        }
                                    }

                                    /* Cleanup panel 2: buttons always freed */
                                    btn_delete(p2_btn_jump);
                                    btn_delete(p2_btn_del);
                                    btn_delete(p2_btn_cancel);

                                    /* Only redraw Panel 1 + rebuild listbox if Panel 1 will
                                     * continue running (delete/cancel). When jumping
                                     * (bm_panel1_run == 0), skip everything -- Panel 1
                                     * cleanup will free resources and ebook_draw_page()
                                     * will restore the reading page immediately. */
                                    if (bm_panel1_run)
                                    {
                                        gui_fill_rectangle(bm_px, bm_py, bm_pw, bm_ph, 0xC618);
                                        gui_show_string((uint8_t *)
                                            "\xCA\xE9\xC7\xA9\xB9\xDC\xC0\xED",  /* "书签管理" */
                                            bm_px + 8, bm_title_y,
                                            bm_pw - 16, gui_phy.tbfsize,
                                            gui_phy.tbfsize, WHITE);
                                        if (bm_btn_add) btn_draw(bm_btn_add);
                                        if (bm_btn_back) btn_draw(bm_btn_back);

                                        /* Rebuild listbox (may have changed due to delete) */
                                        if (bm_lb)
                                        {
                                            for (i = 0; i < EBOOK_BM_MAX; i++)
                                            {
                                                if (bm_item_names[i])
                                                { gui_memin_free(bm_item_names[i]); bm_item_names[i] = NULL; }
                                            }
                                            listbox_delete(bm_lb);
                                        }
                                        bm_lb = listbox_creat(bm_px + 10, bm_lby,
                                                              bm_pw - 20, bm_lbh,
                                                              1, gui_phy.listfsize);
                                        if (bm_lb)
                                        {
                                            for (i = 0; i < ctx->bookmarks->count; i++)
                                            {
                                                uint8_t tmp[8];
                                                bm_item_names[i] = gui_memin_malloc(32);
                                                if (bm_item_names[i])
                                                {
                                                    strcpy((char *)bm_item_names[i], "\xCA\xE9\xC7\xA9");   /* "书签" */
                                                    gui_num2str(tmp, i + 1);
                                                    strcat((char *)bm_item_names[i], (char *)tmp);
                                                    strcat((char *)bm_item_names[i], ": \xB5\xDA");
                                                    gui_num2str(tmp,
                                                        ctx->bookmarks->entries[i].page_num);
                                                    strcat((char *)bm_item_names[i], (char *)tmp);
                                                    strcat((char *)bm_item_names[i], " \xD2\xB3");
                                                    listbox_addlist(bm_lb, bm_item_names[i]);
                                                }
                                            }
                                            if (ctx->bookmarks->count > 0)
                                                listbox_draw_listbox(bm_lb);

                                            bm_lb->dbclick = 0;
                                        }
                                    }
                                }  /* end if valid index */
                            }  /* end if dbclick */
                        }  /* end if bm_lb */

                        /* --- "Return" button --- */
                        if (bm_btn_back && btn_check(bm_btn_back, &in_obj))
                        {
                            if ((bm_btn_back->sta & 0X80) == 0)
                            {
                                bm_panel1_run = 0;
                            }
                        }
                    }  /* end while panel 1 */

                    /* ---- Cleanup panel 1 ---- */
                    btn_delete(bm_btn_add);
                    btn_delete(bm_btn_back);
                    if (bm_lb)
                    {
                        for (i = 0; i < EBOOK_BM_MAX; i++)
                        {
                            if (bm_item_names[i]) gui_memin_free(bm_item_names[i]);
                        }
                        listbox_delete(bm_lb);
                    }

                    /* Restore reading page */
                    ebook_draw_page(ctx);
                    ebook_show_page_num(ctx);
                    swipe_tracking = 0;
                    continue;
                }
            }
            /* ---- Check eye-care button ---- */
            if (ebtn)
            {
                res = btn_check(ebtn, &in_obj);

                if (res && ((ebtn->sta & 0X80) == 0))
                {
                    /* Toggle background mode: 0 <-> 1 */
                    ctx->bg_mode = (ctx->bg_mode == 0) ? 1 : 0;

                    /* Redraw page with new background color */
                    ebook_draw_page(ctx);
                    ebook_show_page_num(ctx);
                    swipe_tracking = 0;
                    continue;
                }
            }

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

            /* ---- Check progress bar ---- */
            if (prgb && ctx->file_size > 0)
            {
                uint8_t prgb_ret = progressbar_check(prgb, &in_obj);

                if (prgb_ret)
                {
                    /* 拖拽中：显示百分比标签 */
                    prgb_dragging = 1;

                    {
                        uint8_t pct;
                        uint8_t pbuf[8];
                        uint16_t lbl_x, lbl_y;

                        pct = (uint8_t)(((uint32_t)prgb->curpos * 100)
                                        / prgb->totallen);
                        lbl_x = prgb->left + (prgb->width - PRGB_LABEL_W) / 2;
                        lbl_y = prgb->top - PRGB_LABEL_H - 2;

                        /* 清除旧标签区域（用背景色填充） */
                        {
                            uint16_t bgc = (ctx->bg_mode == 0) ? WHITE : EYECARE_BG;
                            gui_fill_rectangle(lbl_x, lbl_y,
                                               PRGB_LABEL_W, PRGB_LABEL_H,
                                               bgc);
                        }

                        /* 绘制百分比文字 "XX%" */
                        gui_num2str(pbuf, pct);
                        strcat((char *)pbuf, "%");
                        gui_show_strmid(lbl_x, lbl_y,
                                        PRGB_LABEL_W, PRGB_LABEL_H,
                                        BLACK, gui_phy.tbfsize, pbuf);
                    }
                }

                /* 检测松手：之前正在拖拽，现在触摸释放 */
                if (prgb_dragging && !(raw_sta & TP_PRES_DOWN))
                {
                    prgb_dragging = 0;

                    /* 清除百分比标签 */
                    {
                        uint16_t lbl_x, lbl_y;
                        uint16_t bgc = (ctx->bg_mode == 0) ? WHITE : EYECARE_BG;
                        lbl_x = prgb->left
                                + (prgb->width - PRGB_LABEL_W) / 2;
                        lbl_y = prgb->top - PRGB_LABEL_H - 2;
                        gui_fill_rectangle(lbl_x, lbl_y,
                                           PRGB_LABEL_W, PRGB_LABEL_H,
                                           bgc);
                    }

                    /* 跳转到进度条位置（对齐 GBK 字符边界后再跳转，
                     * 避免落在双字节汉字中间导致乱码） */
                    ctx->page_start = ebook_align_gbk(ctx, prgb->curpos);
                    ctx->prev_count = 0;
                    ctx->page_num   = 1;  /* 近似值，scan 后以实际为准 */
                    ebook_scan_page_forward(ctx);
                    ebook_draw_page(ctx);
                    ebook_show_page_num(ctx);
                    swipe_tracking = 0;

                    /* 进度条的 curpos 在 ebook_draw_page 中已自动更新 */
                    continue;
                }
            }

            /* ---- Check return button ---- */
            res = btn_check(rbtn, &in_obj);
            if (res && ((rbtn->sta & 0X80) == 0))
            {
                /* Return to file browsing */
                ebook_ctx_deinit(ctx);
                ctx = NULL;
                gui_memin_free(pname);
                pname = NULL;
                ebooksta = 0;
                swipe_tracking = 0;
                app_filebrower((uint8_t *)APP_MFUNS_CAPTION_TBL[0][gui_phy.language], 0X07);
                btn_draw(rbtn);
                filelistbox_rebuild_filelist(flistbox);
                continue;
            }

            /* ---- Physical key handling ---- */
            if (key_val == KEY1_PRES)
            {
                ebook_turn_page(ctx, 1);   /* next page */
                continue;
            }
            if (key_val == WKUP_PRES)
            {
                ebook_turn_page(ctx, -1);  /* previous page */
                continue;
            }

            /* ---- Touch swipe gesture detection ---- */
            if (raw_sta & TP_PRES_DOWN)
            {
                if (!swipe_tracking)
                {
                    /* First touch: only track if it is in the text area */
                    if (raw_y >= ctx->text_y
                            && raw_y < ctx->text_y + ctx->text_height)
                    {
                        swipe_start_x  = raw_x;
                        swipe_start_y  = raw_y;
                        swipe_tracking = 1;
                    }
                }
            }
            else
            {
                /* Touch released */
                if (swipe_tracking)
                {
                    int dx = (int)raw_x - (int)swipe_start_x;
                    int dy = (int)raw_y - (int)swipe_start_y;

                    /* Convert to positive offsets for comparison */
                    int adx = (dx < 0) ? -dx : dx;
                    int ady = (dy < 0) ? -dy : dy;

                    if (adx >= EBOOK_SWIPE_THRESH && adx > ady)
                    {
                        if (dx < 0)
                            ebook_turn_page(ctx, 1);   /* swipe left �??? next page */
                        else
                            ebook_turn_page(ctx, -1);  /* swipe right �??? previous page */
                    }

                    swipe_tracking = 0;
                }
            }
        }
    }

    /* ========================================================================
     *  Cleanup and error display
     * ======================================================================== */
    if (errtype)
    {
        window_msg_box((lcddev.width - 160) / 2, (lcddev.height - 70) / 2 - 15,
                       160, 70,
                       (uint8_t *)ebook_remind_msg_tbl[gui_phy.language],
                       (uint8_t *)APP_REMIND_CAPTION_TBL[gui_phy.language],
                       12, 0, 0, 0);
        delay_ms(500);
    }

    filelistbox_delete(flistbox);
    btn_delete(rbtn);
    btn_delete(fbtn);
    btn_delete(bbtn);
    btn_delete(ebtn);
    btn_delete(brbtn);       /* 销毁亮度按钮 */
    if (prgb) { progressbar_delete(prgb); prgb = NULL; }   /* 销毁进度条 */
    g_ebook_prgb = NULL;                                     /* 清除静态指针 */
    ebook_ctx_deinit(ctx);
    gui_memin_free(pname);
    gui_memin_free(ebookinfo);
    gui_memin_free(buf);
    gui_memin_free(f_txt);
    return rval;
}
