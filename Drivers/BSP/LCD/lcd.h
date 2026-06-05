/**
 ****************************************************************************************************
 * @file        lcd.h
 * @author      ÕıµãÔ­×ÓÍÅ¶Ó(ALIENTEK)
 * @version     V1.2
 * @date        2023-05-31
 * @brief       2.8´ç/3.5´ç/4.3´ç/7´ç TFTLCD(MCUÆÁ) Çı¶¯´úÂë
 *              Ö§³ÖÇı¶¯ICĞÍºÅ°üÀ¨:ILI9341/NT35310/NT35510/SSD1963/ST7789/ST7796/ILI9806 µÈ
 *
 * @license     Copyright (c) 2020-2032, ¹ãÖİÊĞĞÇÒíµç×Ó¿Æ¼¼ÓĞÏŞ¹«Ë¾
 ****************************************************************************************************
 * @attention
 *
 * ÊµÑéÆ½Ì¨:ÕıµãÔ­×Ó STM32F103¿ª·¢°å
 * ÔÚÏßÊÓÆµ:www.yuanzige.com
 * ¼¼ÊõÂÛÌ³:www.openedv.com
 * ¹«Ë¾ÍøÖ·:www.alientek.com
 * ¹ºÂòµØÖ·:openedv.taobao.com
 *
 * ĞŞ¸ÄËµÃ÷
 * V1.0 20200530
 * µÚÒ»´Î·¢²¼
 * V1.1 20200607
 * ĞÂÔölcd_wr_xdataº¯Êı, ÓÃÓÚlcd_ex.c, ÒÔÊ±¼ä»»¿Õ¼ä½µµÍlcdÇı¶¯µÄ´úÂëÁ¿
 * V1.2 20230531
 * 1£¬ĞÂÔö¶ÔST7796ºÍILI9806 ICÖ§³Ö
 *
 ****************************************************************************************************
 */

#ifndef __LCD_H
#define __LCD_H

#include "stdlib.h"
#include "./SYSTEM/sys/sys.h"


/******************************************************************************************/
/* LCD RST/BL/WR/RD/CS/RS/D0~D15 Òı½Å ¶¨Òå */

/* RESET ºÍÏµÍ³¸´Î»½Å¹²ÓÃ ËùÒÔÕâÀï²»ÓÃ¶¨Òå RESETÒı½Å */
//#define LCD_RST_GPIO_PORT               GPIOx
//#define LCD_RST_GPIO_PIN                SYS_GPIO_PINx
//#define LCD_RST_GPIO_CLK_ENABLE()       do{ RCC->APB2ENR |= 1 << x; }while(0)   /* ËùÔÚIO¿ÚÊ±ÖÓÊ¹ÄÜ */

#define LCD_BL_GPIO_PORT                GPIOC
#define LCD_BL_GPIO_PIN                 SYS_GPIO_PIN10
#define LCD_BL_GPIO_CLK_ENABLE()        do{ RCC->APB2ENR |= 1 << 4; }while(0)   /* ËùÔÚIO¿ÚÊ±ÖÓÊ¹ÄÜ */

#define LCD_WR_GPIO_PORT                GPIOC
#define LCD_WR_GPIO_PIN                 SYS_GPIO_PIN7
#define LCD_WR_GPIO_CLK_ENABLE()        do{ RCC->APB2ENR |= 1 << 4; }while(0)   /* ËùÔÚIO¿ÚÊ±ÖÓÊ¹ÄÜ */

#define LCD_RD_GPIO_PORT                GPIOC
#define LCD_RD_GPIO_PIN                 SYS_GPIO_PIN6
#define LCD_RD_GPIO_CLK_ENABLE()        do{ RCC->APB2ENR |= 1 << 4; }while(0)   /* ËùÔÚIO¿ÚÊ±ÖÓÊ¹ÄÜ */

#define LCD_CS_GPIO_PORT                GPIOC
#define LCD_CS_GPIO_PIN                 SYS_GPIO_PIN9
#define LCD_CS_GPIO_CLK_ENABLE()        do{ RCC->APB2ENR |= 1 << 4; }while(0)   /* ËùÔÚIO¿ÚÊ±ÖÓÊ¹ÄÜ */

#define LCD_RS_GPIO_PORT                GPIOC
#define LCD_RS_GPIO_PIN                 SYS_GPIO_PIN8
#define LCD_RS_GPIO_CLK_ENABLE()        do{ RCC->APB2ENR |= 1 << 4; }while(0)   /* ËùÔÚIO¿ÚÊ±ÖÓÊ¹ÄÜ */

#define LCD_DATA_GPIO_PORT              GPIOB
#define LCD_DATA_GPIO_PIN               0XFFFF                                  /* 16¸öIO¶¼ÓÃµ½ */
#define LCD_DATA_GPIO_CLK_ENABLE()      do{ RCC->APB2ENR |= 1 << 3; }while(0)   /* ËùÔÚIO¿ÚÊ±ÖÓÊ¹ÄÜ */

/******************************************************************************************/

/* LCD ¶Ë¿Ú¿ØÖÆº¯Êı ¶¨Òå */
//#define LCD_RST(x)      sys_gpio_pin_set(LCD_RST_GPIO_PORT,  LCD_RST_GPIO_PIN,  x)  /* ÉèÖÃRSTÒı½Å */
#define LCD_BL(x)       sys_gpio_pin_set(LCD_BL_GPIO_PORT, LCD_BL_GPIO_PIN, x)      /* ÉèÖÃBLÒı½Å */

#define LCD_WR(x)       LCD_WR_GPIO_PORT->BSRR = LCD_WR_GPIO_PIN << (16 * (!x))     /* ÉèÖÃWRÒı½Å */
#define LCD_RD(x)       LCD_RD_GPIO_PORT->BSRR = LCD_RD_GPIO_PIN << (16 * (!x))     /* ÉèÖÃRDÒı½Å */
#define LCD_CS(x)       LCD_CS_GPIO_PORT->BSRR = LCD_CS_GPIO_PIN << (16 * (!x))     /* ÉèÖÃCSÒı½Å */
#define LCD_RS(x)       LCD_RS_GPIO_PORT->BSRR = LCD_RS_GPIO_PIN << (16 * (!x))     /* ÉèÖÃRSÒı½Å */

#define LCD_DATA_OUT(x) LCD_DATA_GPIO_PORT->ODR = x                                 /* Ğ´D0~D15Òı½Å */
#define LCD_DATA_IN     LCD_DATA_GPIO_PORT->IDR                                     /* ¶ÁD0~D15Òı½Å */

/******************************************************************************************/


/* LCDÖØÒª²ÎÊı¼¯ */
typedef struct
{
    uint16_t width;     /* LCD ¿í¶È */
    uint16_t height;    /* LCD ¸ß¶È */
    uint16_t id;        /* LCD ID */
    uint8_t dir;        /* ºáÆÁ»¹ÊÇÊúÆÁ¿ØÖÆ£º0£¬ÊúÆÁ£»1£¬ºáÆÁ¡£ */
    uint16_t wramcmd;   /* ¿ªÊ¼Ğ´gramÖ¸Áî */
    uint16_t setxcmd;   /* ÉèÖÃx×ø±êÖ¸Áî */
    uint16_t setycmd;   /* ÉèÖÃy×ø±êÖ¸Áî */
} _lcd_dev;

/* LCD²ÎÊı */
extern _lcd_dev lcddev; /* ¹ÜÀíLCDÖØÒª²ÎÊı */

/* LCDµÄ»­±ÊÑÕÉ«ºÍ±³¾°É« */
extern uint32_t  g_point_color;     /* Ä¬ÈÏºìÉ« */
extern uint32_t  g_back_color;      /* ±³¾°ÑÕÉ«.Ä¬ÈÏÎª°×É« */

/******************************************************************************************/
/* LCDÉ¨Ãè·½ÏòºÍÑÕÉ« ¶¨Òå */

/* É¨Ãè·½Ïò¶¨Òå */
#define L2R_U2D         0           /* ´Ó×óµ½ÓÒ,´ÓÉÏµ½ÏÂ */
#define L2R_D2U         1           /* ´Ó×óµ½ÓÒ,´ÓÏÂµ½ÉÏ */
#define R2L_U2D         2           /* ´ÓÓÒµ½×ó,´ÓÉÏµ½ÏÂ */
#define R2L_D2U         3           /* ´ÓÓÒµ½×ó,´ÓÏÂµ½ÉÏ */

#define U2D_L2R         4           /* ´ÓÉÏµ½ÏÂ,´Ó×óµ½ÓÒ */
#define U2D_R2L         5           /* ´ÓÉÏµ½ÏÂ,´ÓÓÒµ½×ó */
#define D2U_L2R         6           /* ´ÓÏÂµ½ÉÏ,´Ó×óµ½ÓÒ */
#define D2U_R2L         7           /* ´ÓÏÂµ½ÉÏ,´ÓÓÒµ½×ó */

#define DFT_SCAN_DIR    L2R_U2D     /* Ä¬ÈÏµÄÉ¨Ãè·½Ïò */

/* ³£ÓÃ»­±ÊÑÕÉ« */
#define WHITE           0xFFFF      /* °×É« */
#define BLACK           0x0000      /* ºÚÉ« */
#define RED             0xF800      /* ºìÉ« */
#define GREEN           0x07E0      /* ÂÌÉ« */
#define BLUE            0x001F      /* À¶É« */
#define MAGENTA         0XF81F      /* Æ·ºìÉ«/×ÏºìÉ« = BLUE + RED */
#define YELLOW          0XFFE0      /* »ÆÉ« = GREEN + RED */
#define CYAN            0X07FF      /* ÇàÉ« = GREEN + BLUE */

/* ·Ç³£ÓÃÑÕÉ« */
#define BROWN           0XBC40      /* ×ØÉ« */
#define BRRED           0XFC07      /* ×ØºìÉ« */
#define GRAY            0X8430      /* »ÒÉ« */
#define DARKBLUE        0X01CF      /* ÉîÀ¶É« */
#define LIGHTBLUE       0X7D7C      /* Ç³À¶É« */
#define GRAYBLUE        0X5458      /* »ÒÀ¶É« */
#define LIGHTGREEN      0X841F      /* Ç³ÂÌÉ« */
#define EYECARE_BG      0xFFF0      /* æŠ¤çœ¼æµ…é»„è‰²èƒŒæ™¯ */
#define LGRAY           0XC618      /* Ç³»ÒÉ«(PANNEL),´°Ìå±³¾°É« */
#define LGRAYBLUE       0XA651      /* Ç³»ÒÀ¶É«(ÖĞ¼ä²ãÑÕÉ«) */
#define LBBLUE          0X2B12      /* Ç³×ØÀ¶É«(Ñ¡ÔñÌõÄ¿µÄ·´É«) */

/******************************************************************************************/
/* SSD1963Ïà¹ØÅäÖÃ²ÎÊı(Ò»°ã²»ÓÃ¸Ä) */

/* LCD·Ö±æÂÊÉèÖÃ */
#define SSD_HOR_RESOLUTION      800     /* LCDË®Æ½·Ö±æÂÊ */
#define SSD_VER_RESOLUTION      480     /* LCD´¹Ö±·Ö±æÂÊ */

/* LCDÇı¶¯²ÎÊıÉèÖÃ */
#define SSD_HOR_PULSE_WIDTH     1       /* Ë®Æ½Âö¿í */
#define SSD_HOR_BACK_PORCH      46      /* Ë®Æ½Ç°ÀÈ */
#define SSD_HOR_FRONT_PORCH     210     /* Ë®Æ½ºóÀÈ */

#define SSD_VER_PULSE_WIDTH     1       /* ´¹Ö±Âö¿í */
#define SSD_VER_BACK_PORCH      23      /* ´¹Ö±Ç°ÀÈ */
#define SSD_VER_FRONT_PORCH     22      /* ´¹Ö±Ç°ÀÈ */

/* ÈçÏÂ¼¸¸ö²ÎÊı£¬×Ô¶¯¼ÆËã */
#define SSD_HT          (SSD_HOR_RESOLUTION + SSD_HOR_BACK_PORCH + SSD_HOR_FRONT_PORCH)
#define SSD_HPS         (SSD_HOR_BACK_PORCH)
#define SSD_VT          (SSD_VER_RESOLUTION + SSD_VER_BACK_PORCH + SSD_VER_FRONT_PORCH)
#define SSD_VPS         (SSD_VER_BACK_PORCH)

/******************************************************************************************/
/* º¯ÊıÉêÃ÷ */


/* LCDĞ´Êı¾İ, ½«º¯Êı¸Ä³Éºê¶¨Òåº¯Êı, ÒÔ´ïµ½×î¸ßËÙ¶È
 * -O2ÓÅ»¯Ê±, Èç¹ûlcd_wr_dataÊ¹ÓÃÆÕÍ¨º¯Êı¶¨Òå, Ö»ÄÜµ½15Ö¡Ë¢ÆÁ
 * -O2ÓÅ»¯Ê±, Èç¹ûlcd_wr_dataÊ¹ÓÃ__forceinlineº¯Êı¶¨Òå, ÄÜµ½39Ö¡Ë¢ÆÁ
 * -O2ÓÅ»¯Ê±, Èç¹ûlcd_wr_dataÊ¹ÓÃºê¶¨Òåº¯Êı, ÄÜµ½51Ö¡Ë¢ÆÁ
 */
#define lcd_wr_data(data)\
    {\
        LCD_RS(1);\
        LCD_CS(0);\
        LCD_DATA_OUT(data);\
        LCD_WR(0);\
        LCD_WR(1);\
        LCD_CS(1);\
    }

void lcd_wr_xdata(uint16_t data);                   /* LCDĞ´Êı¾İ, ¸Ãº¯ÊıÍ¬ lcd_wr_data º¯ÊıµÄ¹¦ÄÜÒ»Ä£Ò»Ñù */
void lcd_wr_regno(volatile uint16_t regno);         /* LCDĞ´¼Ä´æÆ÷±àºÅ/µØÖ· */
void lcd_write_reg(uint16_t regno, uint16_t data);  /* LCDĞ´¼Ä´æÆ÷µÄÖµ */


void lcd_init(void);                        /* ³õÊ¼»¯LCD */
void lcd_display_on(void);                  /* ¿ªÏÔÊ¾ */
void lcd_display_off(void);                 /* ¹ØÏÔÊ¾ */
void lcd_scan_dir(uint8_t dir);             /* ÉèÖÃÆÁÉ¨Ãè·½Ïò */
void lcd_display_dir(uint8_t dir);          /* ÉèÖÃÆÁÄ»ÏÔÊ¾·½Ïò */
void lcd_ssd_backlight_set(uint8_t pwm);    /* SSD1963 ±³¹â¿ØÖÆ */

void lcd_write_ram_prepare(void);                           /* ×¼±¸Ğ©GRAM */
void lcd_set_cursor(uint16_t x, uint16_t y);                /* ÉèÖÃ¹â±ê */
uint32_t lcd_read_point(uint16_t x, uint16_t y);            /* ¶Áµã(32Î»ÑÕÉ«,¼æÈİLTDC)  */
void lcd_draw_point(uint16_t x, uint16_t y, uint32_t color);/* »­µã(32Î»ÑÕÉ«,¼æÈİLTDC) */

void lcd_clear(uint16_t color);                                                             /* LCDÇåÆÁ */
void lcd_fill_circle(uint16_t x, uint16_t y, uint16_t r, uint16_t color);                   /* Ìî³äÊµĞÄÔ² */
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color);                  /* »­Ô² */
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color);                  /* »­Ë®Æ½Ïß */
void lcd_set_window(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height);             /* ÉèÖÃ´°¿Ú */
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color);          /* ´¿É«Ìî³ä¾ØĞÎ(32Î»ÑÕÉ«,¼æÈİLTDC) */
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color);   /* ²ÊÉ«Ìî³ä¾ØĞÎ */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);     /* »­Ö±Ïß */
void lcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);/* »­¾ØĞÎ */


void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint16_t color);                       /* ÏÔÊ¾Ò»¸ö×Ö·û */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color);                     /* ÏÔÊ¾Êı×Ö */
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color);      /* À©Õ¹ÏÔÊ¾Êı×Ö */
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color);   /* ÏÔÊ¾×Ö·û´® */


#endif

















