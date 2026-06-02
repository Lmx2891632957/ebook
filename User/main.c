/**
 ****************************************************************************************************
 * @file        main.c
 * @author      ALIENTEK
 * @version     V1.0
 * @date        2022-12-17
 * @brief       MiniSTM32 Comprehensive Test Experiment
 * @license     Copyright (c) 2020-2032, ALIENTEK
 ****************************************************************************************************
 * @attention
 *
 * Platform: ALIENTEK STM32F103 Dev Board
 * Video:    www.yuanzige.com
 * Forum:    www.openedv.com
 * Company:  www.alientek.com
 * Shop:     openedv.taobao.com
 *
 ****************************************************************************************************
 */

#include "os.h"
#include "common.h"
#include "ebook.h"

volatile uint8_t system_task_return;    /* Force return flag */
volatile uint8_t ledplay_ds0_sta = 0;   /* DS0 LED control state for ledplay task */
volatile uint8_t sd_check_en = 1;       /* SD card check enable flag */

#if !(__ARMCC_VERSION >= 6010050)       /* AC5 compiler */
#define __ALIGNED_8     __align(8)      /* AC5 alignment */
#else                                   /* AC6 compiler */
#define __ALIGNED_8     __ALIGNED(8)    /* AC6 alignment */
#endif

/******************************************************************************************/
/* UCOSII Task Definitions                                                               */
/******************************************************************************************/

/* Start task */
#define START_TASK_PRIO                 10                  /* Priority (lowest) */
#define START_STK_SIZE                  64                  /* Stack size */

__ALIGNED_8 static OS_STK START_TASK_STK[START_STK_SIZE];   /* Task stack, 8-byte aligned */
void start_task(void *pdata);                               /* Task function */

/* USART task */
#define USART_TASK_PRIO                 7                   /* Priority */
#define USART_STK_SIZE                  64                  /* Stack size */

__ALIGNED_8 static OS_STK USART_TASK_STK[USART_STK_SIZE];   /* Task stack, 8-byte aligned */
void usart_task(void *pdata);                               /* Task function */

/* Main task */
#define MAIN_TASK_PRIO                  6                   /* Priority */
#define MAIN_STK_SIZE                   256                 /* Stack size */

__ALIGNED_8 static OS_STK MAIN_TASK_STK[MAIN_STK_SIZE];     /* Task stack, 8-byte aligned */
void main_task(void *pdata);                                /* Task function */

/* Watchdog task */
#define WATCH_TASK_PRIO                 3                   /* Priority */
#define WATCH_STK_SIZE                  128                 /* Stack size */

__ALIGNED_8 static OS_STK WATCH_TASK_STK[WATCH_STK_SIZE];   /* Task stack, 8-byte aligned */
void watch_task(void *pdata);                               /* Task function */

/******************************************************************************************/

/**
 * @brief       Display error message and halt (flashing LED)
 * @param       x, y  : Coordinates
 * @param       err   : Error message string
 * @param       fsize : Font size
 * @retval      None
 */
void system_error_show(uint16_t x, uint16_t y, uint8_t *err, uint8_t fsize)
{
    uint8_t ledr = 1;

    while (1)
    {
        lcd_show_string(x, y, lcddev.width, lcddev.height, fsize, (char *)err, RED);
        delay_ms(400);
        lcd_fill(x, y, lcddev.width - 1, y + fsize, BLACK);
        delay_ms(100);
        LED0(ledr ^= 1);
    }
}

/**
 * @brief       Display error message briefly (2 seconds), then continue
 * @param       x, y  : Coordinates
 * @param       fsize : Font size
 * @param       str   : Message string
 * @retval      None
 */
void system_error_show_pass(uint16_t x, uint16_t y, uint8_t fsize, uint8_t *str)
{
    LED1(1);
    lcd_show_string(x, y, lcddev.width, lcddev.height, fsize, (char *)str, RED);
    delay_ms(2000);
    LED1(0);
}

/**
 * @brief       Erase SPI FLASH system files (with confirmation dialog)
 * @param       x, y  : Coordinates
 * @param       fsize : Font size
 * @retval      0: cancelled; 1: erased
 */
uint8_t system_files_erase(uint16_t x, uint16_t y, uint8_t fsize)
{
    uint8_t ledr = 1;
    uint8_t key;
    uint8_t t = 0;
    uint16_t i = 0;

    lcd_show_string(x, y, lcddev.width, lcddev.height, fsize, "Erase all system files?", RED);

    while (1)
    {
        t++;

        if (t == 20) lcd_show_string(x, y + fsize, lcddev.width, lcddev.height, fsize, "KEY0:NO / KEY1:YES", RED);

        if (t == 40)
        {
            gui_fill_rectangle(x, y + fsize, lcddev.width, fsize, BLACK); /* Clear prompt */
            t = 0;
            LED0(ledr ^= 1);
        }

        key = key_scan(0);

        if (key == KEY0_PRES)   /* KEY0 pressed: cancel */
        {
            gui_fill_rectangle(x, y, lcddev.width, fsize * 2, BLACK); /* Clear display */
            g_point_color = WHITE;
            LED0(1);
            return 0;
        }

        if (key == KEY1_PRES)   /* KEY1 pressed: confirm erase */
        {
            LED0(1);
            lcd_show_string(x, y + fsize, lcddev.width, lcddev.height, fsize, "Erasing SPI FLASH...", RED);

            for (i = 200; i < 4096; i++)
            {
                fonts_progress_show(x + fsize * 22 / 2, y + fsize, fsize, 3895, i - 200, RED);  /* Show progress */
                norflash_erase_sector(i);   /* ~50ms per sector, ~195s for 3895 sectors */
            }

            lcd_show_string(x, y + fsize, lcddev.width, lcddev.height, fsize, "Erasing SPI FLASH OK      ", RED);
            delay_ms(600);
            return 1;
        }

        delay_ms(10);
    }
}

/**
 * @brief       Font update confirmation dialog
 * @param       x, y  : Coordinates
 * @param       fsize : Font size
 * @retval      0: skip; 1: confirm update
 */
uint8_t system_font_update_confirm(uint16_t x, uint16_t y, uint8_t fsize)
{
    uint8_t ledr = 1;
    uint8_t key;
    uint8_t t = 0;
    uint8_t res = 0;
    g_point_color = RED;
    lcd_show_string(x, y, lcddev.width, lcddev.height, fsize, "Update font?", RED);

    while (1)
    {
        t++;

        if (t == 20) lcd_show_string(x, y + fsize, lcddev.width, lcddev.height, fsize, "KEY0:NO / KEY1:YES", RED);

        if (t == 40)
        {
            gui_fill_rectangle(x, y + fsize, lcddev.width, fsize, BLACK); /* Clear prompt */
            t = 0;
            LED0(ledr ^= 1);
        }

        key = key_scan(0);

        if (key == KEY0_PRES) break; /* Cancel */

        if (key == KEY1_PRES)
        {
            res = 1;    /* Confirm update */
            break;
        }

        delay_ms(10);
    }

    LED0(1);
    gui_fill_rectangle(x, y, lcddev.width, fsize * 2, BLACK); /* Clear display */
    g_point_color = WHITE;
    return res;
}

uint8_t g_tpad_failed_flag = 0;     /* TPAD failure flag; if set, use WK_UP as exit key */

/**
 * @brief       System initialization
 * @param       None
 * @retval      None
 */
void system_init(void)
{
    uint16_t okoffset = 162;
    uint16_t ypos = 0;
    uint16_t j = 0;
    uint16_t temp = 0;
    uint8_t res;
    uint32_t dtsize, dfsize;
    uint8_t *stastr = 0;
    uint8_t *version = 0;
    uint8_t verbuf[12];
    uint8_t fsize;
    uint8_t icowidth;

    sys_stm32_clock_init(9);            /* Set clock to 72MHz */
    usart_init(72, 115200);             /* Init USART at 115200 */

    norflash_init();                    /* Init SPI FLASH */
    delay_init(72);                     /* Init delay timer */
    usmart_dev.init(72);                /* Init USMART */
    lcd_init();                         /* Init LCD */
    led_init();                         /* Init LED */
    key_init();                         /* Init keypad */
    at24cxx_init();                     /* Init EEPROM */
    adc_temperature_init();             /* Init internal temperature sensor */

    my_mem_init(SRAMIN);                /* Init internal SRAM memory pool */

    tp_dev.init();
    gui_init();
    piclib_init();                      /* Init picture library */
    slcd_dma_init();                    /* Init SLCD DMA */
    exfuns_init();                      /* Init FATFS system memory */

    version = mymalloc(SRAMIN, 31);     /* Allocate 31 bytes for version string */

REINIT: /* Re-initialize */

    lcd_clear(BLACK);                   /* Clear screen */
    g_point_color = WHITE;
    g_back_color = BLACK;
    j = 0;

    /* Show copyright info */
    ypos = 2;

    if (lcddev.width <= 272)
    {
        fsize = 12;
        icowidth = 24;
        okoffset = 190;
        app_show_mono_icos(5, ypos, icowidth, 24, (uint8_t *)APP_ALIENTEK_ICO2424, YELLOW, BLACK);
    }
    else if (lcddev.width == 320)
    {
        fsize = 16;
        icowidth = 32;
        okoffset = 250;
        app_show_mono_icos(5, ypos, icowidth, 32, (uint8_t *)APP_ALIENTEK_ICO3232, YELLOW, BLACK);
    }
    else if (lcddev.width >= 480)
    {
        fsize = 24;
        icowidth = 48;
        okoffset = 370;
        app_show_mono_icos(5, ypos, icowidth, 48, (uint8_t *)APP_ALIENTEK_ICO4848, YELLOW, BLACK);
    }

    lcd_show_string(icowidth + 5 * 2, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, "ALIENTEK STM32F103", g_point_color);
    lcd_show_string(icowidth + 5 * 2, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, "Copyright (C) 2022-2032", g_point_color);
    app_get_version(verbuf, HARDWARE_VERSION, 2);
    strcpy((char *)version, "HARDWARE:");
    strcat((char *)version, (const char *)verbuf);
    strcat((char *)version, ", SOFTWARE:");
    app_get_version(verbuf, SOFTWARE_VERSION, 3);
    strcat((char *)version, (const char *)verbuf);
    lcd_show_string(5, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, (char *)version, g_point_color);
    sprintf((char *)verbuf, "LCD ID:%04X", lcddev.id);  /* Print LCD ID */
    lcd_show_string(5, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, (char *)verbuf, g_point_color);

    /* Basic hardware init */
    LED0(0);
    LED1(0);    /* Turn on both LEDs */

    lcd_show_string(5, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, "CPU:STM32F103RCT6 72Mhz", g_point_color);
    lcd_show_string(5, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, "FLASH:256KB + 8MB SRAM:48KB", g_point_color);
    LED0(1);
    LED1(1);    /* Turn off both LEDs */

    /* SPI FLASH check */
    if (norflash_read_id() != BY25Q64 && norflash_read_id() != W25Q64 && norflash_read_id() != NM25Q64)  /* Read SPI FLASH ID */
    {
        system_error_show(5, ypos + fsize * j++, "SPI Flash Error!!", fsize);
    }
    else temp = 8 * 1024;  /* 8MB capacity */

    lcd_show_string(5, ypos + fsize * j, lcddev.width, lcddev.height, fsize, "SPI Flash:     KB", g_point_color);
    lcd_show_xnum(5 + 10 * (fsize / 2), ypos + fsize * j, temp, 5, fsize, 0, g_point_color); /* Show SPI flash size */
    lcd_show_string(5 + okoffset, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, "OK", g_point_color);

    /* Check if SPI FLASH erase is requested */
    res = key_scan(1);

    if (res == WKUP_PRES)   /* WKUP pressed during startup: erase SPI FLASH font & system files */
    {
        res = system_files_erase(5, ypos + fsize * j, fsize);

        if (res) goto REINIT;
    }

    /* RTC check */
    lcd_show_string(5, ypos + fsize * j, lcddev.width, lcddev.height, fsize, "RTC Check...", g_point_color);

    if (rtc_init()) system_error_show_pass(5 + okoffset, ypos + fsize * j++, fsize, "ERROR"); /* RTC error */
    else
    {
        lcd_show_string(5 + okoffset, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, "OK", g_point_color);
    }

    /* Check SPI FLASH filesystem */
    lcd_show_string(5, ypos + fsize * j, lcddev.width, lcddev.height, fsize, "FATFS Check...", g_point_color);
    f_mount(fs[0], "0:", 1);    /* Mount SD card */
    f_mount(fs[1], "1:", 1);    /* Mount SPI FLASH */
    lcd_show_string(5 + okoffset, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, "OK", g_point_color);

    /* SD card check */
    lcd_show_string(5, ypos + fsize * j, lcddev.width, lcddev.height, fsize, "SD Card:     MB", g_point_color);
    temp = 0;

    do
    {
        temp++;
        res = exfuns_get_free("0:", &dtsize, &dfsize);  /* Get SD card free space */
        delay_ms(200);
    } while (res && temp < 5); /* Retry up to 5 times */

    if (res == 0)   /* Got capacity info */
    {
        gui_phy.memdevflag |= 1 << 0;   /* Mark SD card as valid */
        temp = dtsize >> 10;            /* Convert to MB */
        stastr = "OK";
    }
    else
    {
        temp = 0; /* Failed, set to 0 */
        stastr = "ERROR";
    }

    lcd_show_xnum(5 + 8 * (fsize / 2), ypos + fsize * j, temp, 5, fsize, 0, g_point_color);      /* Show SD capacity */
    lcd_show_string(5 + okoffset, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, (char *)stastr, g_point_color);   /* SD status */

    /* SPI FLASH filesystem check; format if needed */
    temp = 0;

    do
    {
        temp++;
        res = exfuns_get_free("1:", &dtsize, &dfsize); /* Get FLASH free space */
        delay_ms(200);
    } while (res && temp < 20); /* Retry up to 20 times */

    if (res == 0X0D)   /* Filesystem not found */
    {
        lcd_show_string(5, ypos + fsize * j, lcddev.width, lcddev.height, fsize, "SPI Flash Disk Formatting...", g_point_color); /* Format FLASH */
        res = f_mkfs("1:", 0, 0, FF_MAX_SS);    /* Format SPI FLASH */

        if (res == 0)
        {
            f_setlabel((const TCHAR *)"1:ALIENTEK");        /* Set volume label */
            lcd_show_string(5 + okoffset, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, "OK", g_point_color); /* Format success */
            res = exfuns_get_free("1:", &dtsize, &dfsize);  /* Re-read free space */
        }
    }

    if (res == 0)   /* Got FLASH free space */
    {
        gui_phy.memdevflag |= 1 << 1;   /* Mark SPI FLASH as valid */
        lcd_show_string(5, ypos + fsize * j, lcddev.width, lcddev.height, fsize, "SPI Flash Disk:     KB", g_point_color);
        temp = dtsize;
    }
    else system_error_show(5, ypos + fsize * (j + 1), "Flash Fat Error!", fsize);   /* FLASH filesystem error */

    lcd_show_xnum(5 + 15 * (fsize / 2), ypos + fsize * j, temp, 5, fsize, 0, g_point_color);    /* Show FLASH capacity */
    lcd_show_string(5 + okoffset, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, "OK", g_point_color); /* FLASH status */

    /* 24C02 EEPROM check */
    lcd_show_string(5, ypos + fsize * j, lcddev.width, lcddev.height, fsize, "24C02 Check...", g_point_color);

    if (at24cxx_check()) system_error_show(5, ypos + fsize * (j + 1), "24C02 Error!", fsize);    /* 24C02 error */
    else lcd_show_string(5 + okoffset, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, "OK", g_point_color);

    /* Font check */
    lcd_show_string(5, ypos + fsize * j, lcddev.width, lcddev.height, fsize, "Font Check...", g_point_color);
    res = key_scan(1); /* Scan keys */

    if (res == KEY1_PRES)   /* KEY1 pressed: force font update */
    {
        res = system_font_update_confirm(5, ypos + fsize * (j + 1), fsize);
    }
    else res = 0;

    if (fonts_init() || (res == 1))   /* Font missing or force update requested */
    {
        res = 0; /* Clear key state */

        if (fonts_update_font(5, ypos + fsize * j, fsize, "0:", g_point_color) != 0)        /* Try SD card */
        {

            if (fonts_update_font(5, ypos + fsize * j, fsize, "1:", g_point_color) != 0)    /* Try SPI FLASH */
            {
                system_error_show(5, ypos + fsize * (j + 1), "Font Error!", fsize);         /* Font update failed */
            }

        }

        lcd_fill(5, ypos + fsize * j, lcddev.width - 1, ypos + fsize * (j + 1), BLACK);     /* Clear line */
        lcd_show_string(5, ypos + fsize * j, lcddev.width, lcddev.height, fsize, "Font Check...", g_point_color);
    }

    lcd_show_string(5 + okoffset, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, "OK", g_point_color); /* Font OK */

    /* System files check */
    lcd_show_string(5, ypos + fsize * j, lcddev.width, lcddev.height, fsize, "SYSTEM Files Check...", g_point_color);

    while (app_system_file_check("1"))      /* Check system files on FLASH */
    {
        lcd_fill(5, ypos + fsize * j, lcddev.width - 1, ypos + fsize * (j + 1), BLACK); /* Clear line */
        lcd_show_string(5, ypos + fsize * j, (fsize / 2) * 8, fsize, fsize, "Updating", g_point_color); /* Show updating */
        app_boot_cpdmsg_set(5, ypos + fsize * j, fsize);    /* Set copy message */
        temp = 0;

        if (app_system_file_check("0"))     /* Check if SD card has system files */
        {
            if (app_system_file_check("2")) res = 9; /* USB disk source */
            else res = 2;                   /* USB drive source */
        }
        else res = 0;                       /* SD card source */

        if (res == 0 || res == 2)           /* Only update from valid sources */
        {
            sprintf((char *)verbuf, "%d:", res);

            if (app_system_update(app_boot_cpdmsg, verbuf))   /* Update */
            {
                system_error_show(5, ypos + fsize * (j + 1), "SYSTEM File Error!", fsize);
            }
        }

        lcd_fill(5, ypos + fsize * j, lcddev.width - 1, ypos + fsize * (j + 1), BLACK); /* Clear line */
        lcd_show_string(5, ypos + fsize * j, lcddev.width, lcddev.height, fsize, "SYSTEM Files Check...", g_point_color);

        if (app_system_file_check("1"))     /* Double check; if still incomplete, SD files are incomplete */
        {
            system_error_show(5, ypos + fsize * (j + 1), "SYSTEM File Lost!", fsize);
        }
        else break;
    }

    lcd_show_string(5 + okoffset, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, "OK", g_point_color);

    /* Touch screen check */
    lcd_show_string(5, ypos + fsize * j, lcddev.width, lcddev.height, fsize, "Touch Check...", g_point_color);
    res = key_scan(1); /* Scan keys */

    if (tp_init() || (res == KEY0_PRES && (tp_dev.touchtype & 0X80) == 0))   /* Touch init failed, or KEY0 pressed (non-capacitive): calibrate */
    {
        if (res == 1) tp_adjust();

        res = 0;        /* Clear key state */
        goto REINIT;    /* Re-initialize */
    }

    lcd_show_string(5 + okoffset, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, "OK", g_point_color); /* Touch OK */

    /* System parameter loading */
    lcd_show_string(5, ypos + fsize * j, lcddev.width, lcddev.height, fsize, "SYSTEM Parameter Load...", g_point_color);

    if (app_system_parameter_init()) system_error_show(5, ypos + fsize * (j + 1), "Parameter Load Error!", fsize); /* Parameter error */
    else lcd_show_string(5 + okoffset, ypos + fsize * j++, lcddev.width, lcddev.height, fsize, "OK", g_point_color);

    lcd_show_string(5, ypos + fsize * j, lcddev.width, lcddev.height, fsize, "SYSTEM Starting...", g_point_color);

    /* Brief flash to indicate ready */
    LED1(0);
    delay_ms(100);
    LED1(1);
    myfree(SRAMIN, version);
    delay_ms(1500);
}

int main(void)
{
    system_init();  /* Hardware & system initialization */
    OSInit();
    OSTaskCreateExt((void(*)(void *) )start_task,               /* Task function */
                    (void *          )0,                        /* Argument */
                    (OS_STK *        )&START_TASK_STK[START_STK_SIZE - 1], /* Stack top */
                    (INT8U          )START_TASK_PRIO,           /* Priority */
                    (INT16U         )START_TASK_PRIO,           /* Task ID (same as priority) */
                    (OS_STK *        )&START_TASK_STK[0],       /* Stack bottom */
                    (INT32U         )START_STK_SIZE,            /* Stack size */
                    (void *          )0,                        /* User storage */
                    (INT16U         )OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR | OS_TASK_OPT_SAVE_FP); /* Options: stack check, clear, save FPU */
    OSStart();
}

/**
 * @brief       Start task: creates sub-tasks then suspends itself
 * @param       pdata : Unused
 * @retval      None
 */
void start_task(void *pdata)
{
    uint32_t cnts;
    OS_CPU_SR cpu_sr = 0;
    pdata = pdata;

    /* Configure SysTick for the OS tick rate */
    cnts = (CPU_INT32U)((72 * 1000000) / OS_TICKS_PER_SEC);
    OS_CPU_SysTickInit(cnts);

    OSStatInit();                           /* Init statistics task (1s delay) */
    app_srand(OSTime);

    OS_ENTER_CRITICAL();                    /* Enter critical section */
    OSTaskCreateExt((void(*)(void *) )main_task,                /* Task function */
                    (void *          )0,                        /* Argument */
                    (OS_STK *        )&MAIN_TASK_STK[MAIN_STK_SIZE - 1], /* Stack top */
                    (INT8U          )MAIN_TASK_PRIO,            /* Priority */
                    (INT16U         )MAIN_TASK_PRIO,            /* Task ID (same as priority) */
                    (OS_STK *        )&MAIN_TASK_STK[0],        /* Stack bottom */
                    (INT32U         )MAIN_STK_SIZE,             /* Stack size */
                    (void *          )0,                        /* User storage */
                    (INT16U         )OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR | OS_TASK_OPT_SAVE_FP); /* Options: stack check, clear, save FPU */

    OSTaskCreateExt((void(*)(void *) )usart_task,               /* Task function */
                    (void *          )0,                        /* Argument */
                    (OS_STK *        )&USART_TASK_STK[USART_STK_SIZE - 1], /* Stack top */
                    (INT8U          )USART_TASK_PRIO,           /* Priority */
                    (INT16U         )USART_TASK_PRIO,           /* Task ID (same as priority) */
                    (OS_STK *        )&USART_TASK_STK[0],       /* Stack bottom */
                    (INT32U         )USART_STK_SIZE,            /* Stack size */
                    (void *          )0,                        /* User storage */
                    (INT16U         )OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR | OS_TASK_OPT_SAVE_FP); /* Options: stack check, clear, save FPU */

    OSTaskCreateExt((void(*)(void *) )watch_task,               /* Task function */
                    (void *          )0,                        /* Argument */
                    (OS_STK *        )&WATCH_TASK_STK[WATCH_STK_SIZE - 1], /* Stack top */
                    (INT8U          )WATCH_TASK_PRIO,           /* Priority */
                    (INT16U         )WATCH_TASK_PRIO,           /* Task ID (same as priority) */
                    (OS_STK *        )&WATCH_TASK_STK[0],       /* Stack bottom */
                    (INT32U         )WATCH_STK_SIZE,            /* Stack size */
                    (void *          )0,                        /* User storage */
                    (INT16U         )OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR | OS_TASK_OPT_SAVE_FP); /* Options: stack check, clear, save FPU */

    OSTaskSuspend(START_TASK_PRIO);     /* Suspend self */
    OS_EXIT_CRITICAL();                 /* Exit critical section */
}

/**
 * @brief       Main task: runs the ebook player loop
 * @param       pdata : Unused
 * @retval      None
 */
void main_task(void *pdata)
{
    while (1)
    {
        ebook_play();
    }
}

void usart_task(void *pdata)
{
    float psin, psex;
    pdata = pdata;

    while (1)
    {
        delay_ms(1000);

        /* alarm code removed - ebook only */

        psin = my_mem_perused(SRAMIN);
        //psex = my_mem_perused(SRAMEX);
        printf("in:%3.1f,ex:%3.1f\r\n", psin / 10, psex / 10);  /* Print memory usage */

    }
}

/**
 * @brief       Watchdog task: monitors keys, SD card, alarm, and LED
 * @param       pdata : Unused
 * @retval      None
 */
void watch_task(void *pdata)
{
    OS_CPU_SR cpu_sr = 0;
    uint8_t t = 0;
    uint8_t res;
    pdata = pdata;

    while (1)
    {
        if (ledplay_ds0_sta == 0)   /* When ledplay_ds0_sta is 0, control LED0 manually */
        {
            if (t == 4) LED0(1);     /* Off at 100ms */

            if (t == 119)
            {
                LED0(0);            /* Blink on every 2.5s */
                t = 0;
            }
        }

        t++;

        if (key0_scan())      /* KEY0 pressed */
        {
            system_task_return = 1;

        }

        if ((t % 60) == 0)  /* Check every ~900ms */
        {
            if (sd_check_en) /* SD card check enabled? */
            {
                /* SD card hot-plug detection */
                OS_ENTER_CRITICAL();        /* Enter critical section */
                res = sd_get_status();      /* Query SD card status */
                OS_EXIT_CRITICAL();         /* Exit critical section */

                if (res == 0XFF)
                {
                    gui_phy.memdevflag &= ~(1 << 0);    /* Clear SD card valid flag */

                    OS_ENTER_CRITICAL();    /* Enter critical section */
                    sd_init();              /* Re-init SD card */
                    OS_EXIT_CRITICAL();     /* Exit critical section */
                }
                else if ((gui_phy.memdevflag & (1 << 0)) == 0)     /* SD card was invalid? */
                {
                    f_mount(fs[0], "0:", 1);        /* Re-mount SD card */
                    gui_phy.memdevflag |= 1 << 0;   /* Set SD card valid flag */
                }
            }
        }

        delay_ms(10);
    }
}

/**
 * @brief       Hard fault handler: prints fault registers and blinks LED
 * @param       None
 * @retval      None
 */
void HardFault_Handler(void)
{
    uint8_t led1sta = 1;
    uint32_t i;
    uint8_t t = 0;
    uint32_t temp;
    temp = SCB->CFSR;               /* Configurable Fault Status Register (MMSR, BFSR, UFSR) */
    printf("CFSR:%8X\r\n", temp);   /* Print fault value */
    temp = SCB->HFSR;               /* HardFault Status Register */
    printf("HFSR:%8X\r\n", temp);   /* Print fault value */
    temp = SCB->DFSR;               /* Debug Fault Status Register */
    printf("DFSR:%8X\r\n", temp);   /* Print fault value */
    temp = SCB->AFSR;               /* Auxiliary Fault Status Register */
    printf("AFSR:%8X\r\n", temp);   /* Print fault value */

    while (t < 5)
    {
        t++;
        LED1(led1sta ^= 1);

        for (i = 0; i < 0X1FFFFF; i++);
    }
}