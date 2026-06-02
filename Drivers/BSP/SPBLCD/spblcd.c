/**
 ****************************************************************************************************
 * @file        spblcd.c
 * @author      ����ԭ���Ŷ�(ALIENTEK)
 * @version     V1.0
 * @date        2022-10-26
 * @brief       SPBЧ��ʵ�� ��������
 * @license     Copyright (c) 2020-2032, �������������ӿƼ����޹�˾
 ****************************************************************************************************
 * @attention
 *
 * ʵ��ƽ̨:����ԭ�� STM32F103������
 * ������Ƶ:www.yuanzige.com
 * ������̳:www.openedv.com
 * ��˾��ַ:www.alientek.com
 * �����ַ:openedv.taobao.com
 *
 * �޸�˵��
 * V1.0 20221026
 * ��һ�η���
 *
 ****************************************************************************************************
 */

#include "./BSP/SPBLCD/spblcd.h"
#include "./MALLOC/malloc.h"
#include "./BSP/DMA/dma.h"
#include "./BSP/NORFLASH/norflash.h"
#include "./BSP/SPI/spi.h"
#include "spb.h"
#include "ucos_ii.h"

/**
 * @brief       ��lcd������spi flash
 * @param       offset  : �洢��ַƫ����
 * @param       width   : ����
 * @param       height  : �߶�
 * @retval      0,�ɹ�; ����,�������;
 */
uint8_t slcd_frame_lcd2spi(uint32_t offset, uint16_t width, uint16_t height)
{
    uint16_t *pbuf;
    uint32_t startx;        /* ��ʼ��ַ */
    uint16_t i, j, k;
    uint32_t woffset = 0;   /* д��ַƫ�� */
    
    uint8_t *p;
    uint8_t temp;

    startx = SPILCD_BASE + offset * 2;  /* ��Ϊoffset��������Ϊ��λ��,��д�������ֽ�Ϊ��λ�ģ����Ե�*2 */
    pbuf = mymalloc(SRAMIN, 4096);      /* �����ڴ� */

    if (!pbuf)return 1;

    woffset = 0;
    k = 0;

    for (i = 0; i < width; i++)
    {
        for (j = 0; j < height; j++)
        {
            pbuf[k++] = lcd_read_point(i, j);    /* ��ȡ�� */

            if (k == 2048)
            {
                p = (uint8_t *)pbuf;

                for (k = 0; k < 2048; k++)      /* ��ת�ߵ��ֽ� */
                {
                    temp = p[2 * k];
                    p[2 * k] = p[2 * k + 1];
                    p[2 * k + 1] = temp;
                }
                
                norflash_write((uint8_t *)pbuf, startx + woffset, 4096); /* д��һ������ */
                woffset += 4096;
                k = 0;
            }
        }
    }

    if (k)  /* ����һ������Ҫд�� */
    {
        p = (uint8_t *)pbuf;

        for (i = 0; i < k; i++) /* ��ת�ߵ��ֽ� */
        {
            temp = p[2 * i];
            p[2 * i] = p[2 * i + 1];
            p[2 * i + 1] = temp;
        }

        norflash_write((uint8_t *)pbuf, startx + woffset, k * 2);    /* д�����һ������ */
    }

    myfree(SRAMIN, pbuf);   /* �ͷ��ڴ� */
    return 0;
}

/**
 * @brief       SPI2ģʽ����
 * @param       mode    : 0, 8λ��ͨģʽ; 1, 16λDMAģʽ;
 * @param       ��
 * @retval      ��
 */
void slcd_spi2_mode(uint8_t mode)
{
    uint16_t tempreg = 0;

    if (mode == 0)  /* 8λ��ͨģʽ,��������SPI */
    {
        RCC->APB2RSTR |= 1 << 12;   /* ��λSPI1 */
        RCC->APB2RSTR &= ~(1 << 12);/* ֹͣ��λSPI1 */
        tempreg |= 0 << 10;         /* ȫ˫��ģʽ */
        tempreg |= 1 << 9;          /* ����nss���� */
        tempreg |= 1 << 8;
        tempreg |= 1 << 2;          /* SPI���� */
        tempreg |= 0 << 11;         /* 8λ���ݸ�ʽ */
        tempreg |= 1 << 1;          /* ����ģʽ��SCKΪ1 CPOL=1 */
        tempreg |= 1 << 0;          /* ���ݲ����ӵ�2��ʱ����ؿ�ʼ,CPHA=1 */
        tempreg |= 0 << 3;          /* 2��Ƶ,����Ϊ36MhzƵ�� */
        tempreg |= 0 << 7;          /* MSB First */
        tempreg |= 1 << 6;          /* SPI���� */
        SPI1->CR1 = tempreg;        /* ����CR1 */
        SPI1->I2SCFGR &= ~(1 << 11);/* ѡ��SPIģʽ */
    }
    else    /* 16λDMAģʽ */
    {
        SPI1->CR1 &= ~(1 << 6);     /* �ر�SPI�� */
        SPI1->CR1 |= 1 << 10;       /* ������ģʽ */
        SPI1->CR1 |= 1 << 11;       /* 16bit���ݸ�ʽ */
    }
}

/**
 * @brief       QSPI --> LCD_RAM dma����
 *  @note       PAR��NDTR�ں���ʹ�ܵ�ʱ��������
 * @param       ��
 * @retval      ��
 */
void slcd_dma_init(void)
{
    RCC->AHBENR |= 1 << 1;  /* ����DMA2ʱ�� */
    delay_ms(5);            /* �ȴ�DMAʱ���ȶ� */
    DMA2_Channel5->CPAR = (uint32_t)&SPI1->DR;      /* �����ַΪ:SPI1->DR */
    DMA2_Channel5->CMAR = (uint32_t)&GPIOB->ODR;    /* Ŀ���ַΪGPIOB_ODR */
    DMA2_Channel5->CNDTR = 1;           /* DMA2,����������Ϊ1 */
    DMA2_Channel5->CCR = 0X00000000;    /* ��λ */
    DMA2_Channel5->CCR |= 0 << 4;       /* ������� */
    DMA2_Channel5->CCR |= 1 << 5;       /* ѭ��ģʽ */
    DMA2_Channel5->CCR |= 0 << 6;       /* �����ַ������ģʽ */
    DMA2_Channel5->CCR |= 0 << 7;       /* �洢��������ģʽ */
    DMA2_Channel5->CCR |= 1 << 8;       /* �������ݿ���Ϊ16λ */
    DMA2_Channel5->CCR |= 1 << 10;      /* �洢�����ݿ���16λ */
    DMA2_Channel5->CCR |= 1 << 12;      /* �е����ȼ� */
    DMA2_Channel5->CCR |= 0 << 14;      /* �Ǵ洢�����洢��ģʽ */
    
    RCC->APB2ENR |= 1 << 4;             /* GPIOC ʱ��ʹ�� */
    RCC->APB2ENR |= 1 << 13;            /* TIM8 ʱ��ʹ�� */

    TIM8->ARR = 16 - 1;                 /* �趨�������Զ���װֵ 15�� ÿ16����ʱ��ʱ��һ������ */
    TIM8->CCR2 = 16 / 2;                /* 50%ռ�ձ� */
    TIM8->PSC = 2 - 1;                  /* ����Ԥ��Ƶ��4, 72 / 4 = 18Mhz  */
    TIM8->BDTR |= 1 << 15;              /* ʹ��MOEλ, �߼���ʱ����������MOE�������PWM */

    TIM8->CCMR1 |= 6 << 12;             /* CH2 PWMģʽ1 */
    TIM8->CCMR1 |= 0 << 11;             /* CH2 Ԥװ�ز�ʹ�� */
   

    TIM8->CCER |= 1 << 4;               /* OC2 ���ʹ�� */
    TIM8->CCER |= 0 << 5;               /* OC2 �ߵ�ƽ��Ч */

    TIM8->CR1 |= 0 << 7;                /* ARPE��ʹ�� */
    TIM8->CR1 |= 1 << 2;                /* URSʹ�� */
    TIM8->CR2 |= 0 << 3;                /* ����CCx�¼�,�ͳ�DMA���� */
    TIM8->DIER |= 1 << 10;              /* ����CC2��DMA���� */
    TIM8->DIER |= 0 << 0;               /* �رո����ж� */

    //sys_nvic_init(0, 0, TIM8_UP_IRQn, 2);   /* ����Ϊ������ȼ��жϣ� ��ռ0�������ȼ�0����2 */
}
 
/**
 * @brief       ����LCD WR�ŵ�GPIOģʽ
 * @param       mode    : 0, ���ģʽ
 *                        1, ���ù���ģʽ
 * @retval      ��
 */
void slcd_gpio_set(uint8_t mode)
{
    if(mode)
    {
        sys_gpio_set(GPIOC, SYS_GPIO_PIN7,
                     SYS_GPIO_MODE_AF, SYS_GPIO_OTYPE_PP, SYS_GPIO_SPEED_HIGH, SYS_GPIO_PUPD_PU);       /* PC7 ���Ÿ��ù���ģʽ */
    }else
    {
        sys_gpio_set(GPIOC, SYS_GPIO_PIN7,
                     SYS_GPIO_MODE_OUT, SYS_GPIO_OTYPE_PP, SYS_GPIO_SPEED_HIGH, SYS_GPIO_PUPD_PU);      /* PC7 �������ģʽ */
    }
}

/* g_spblcd_ndata_remain��ʾ��ǰ��ʣ�¶��ٸ�����Ҫ����
 * ÿ����෢��256������
 */
static uint32_t g_spblcd_ndata_remain = 0;
static uint8_t g_rcr_cnt = 0;                   /* RCR���ô��������� */

/**
 * @brief       ��ȡʣ�����ݳ���(���256�ֽ�,��С0,0��ʾû����Ҫ������)
 * @param       ��
 * @retval      0-256, �����͵����ݳ���(����)
 */
uint16_t slcd_get_data_remain(void)
{
    uint16_t datalen = 0;

    if(g_spblcd_ndata_remain >= 256)
    {
        datalen = 256;
        g_rcr_cnt++;                        /* RCR���������� */
    }else if(g_spblcd_ndata_remain)
    {
        datalen = g_spblcd_ndata_remain;    /* ���һ������ */
        g_rcr_cnt++;                        /* RCR���������� */
    }
    
    g_spblcd_ndata_remain -= datalen;
    
    return datalen;
}

/**
 * @brief       ����һ��SPI��LCD��DMA�Ĵ���
 * @param       x       : ��ʼ�����ַ���
 * @retval      ��
 */
void slcd_dma_enable(uint32_t x)
{
    uint32_t lcdsize = spbdev.spbwidth * spbdev.spbheight;    
    uint32_t addr = SPILCD_BASE + (x * spbdev.spbheight) * 2;
    
    uint16_t ndata = 0;

    LCD_RS(1);                          /* RS=1, ��ʾд���� */
    LCD_CS(0);                          /* CS=1, ѡ��LCD */
    slcd_gpio_set(1);                   /* ����WR��Ϊ���ù��� */

    
    g_spblcd_ndata_remain = lcdsize - 1;/* ÿ�ζ��Ǵ���lcdsize��ô������ */
    ndata = slcd_get_data_remain();     /* ���Դ����������? */
    
    TIM8->DIER |= 1 << 10;              /* ��CC2��DMA���� */
    TIM8->RCR = ndata - 1;              /* ���÷���ndata������ */
    TIM8->EGR |= 1 << 0;                /* ����һ�θ����¼�,�Ը���RCR�Ĵ��� */

    NORFLASH_CS(0);                                 /* ʹ��Ƭѡ */
    spi1_read_write_byte(FLASH_FastReadData);       /* ���Ϳ��ٶ�ȡ���� */
    spi1_read_write_byte((uint8_t)((addr) >> 16));  /* ����24bit��ַ */
    spi1_read_write_byte((uint8_t)((addr) >> 8));
    spi1_read_write_byte((uint8_t)addr);
    spi1_read_write_byte(0XFF);                     /* 8 dummy clock */
    
    slcd_spi2_mode(1);                  /* SPI 16λ ֻ����ģʽ */
    DMA2_Channel5->CCR |= 1 << 0;       /* ����DMA���� */
    
    /* ��ǰ������һ��RCR��ֵ */
    ndata = slcd_get_data_remain();     /* ���Դ����������? */
    
    SPI1->CR1 |= 1 << 6;                /* ����SPI ����, ��ʱSPI ��ʼ�������� */
    
    /**
     * �����ж�����ʱ, ����֤SPI���������, Ȼ����ö�ʱ������DMAȥ��ȡSPI->DR
     * �ڲ�ͬ���Ż�������, �˴��������Ͽ�����Ҫ��΢����,δ����ϸ��֤, ������
     */
    if(ndata)
    {
        TIM8->RCR = ndata - 1;          /* ���÷���ndata������ */
    }

    TIM8->CR1 |= 1 << 0;                /* ʹ�ܶ�ʱ��TIM8, ��ʱTIM8���Դ���DMAȥ��ȡSPI->DR�� */
    
    while(1)    /* ��ѭ���ж� */
    {
        if (TIM8->SR & 0X0001)          /* ����ж�, ��RCR=0 */
        {
            if(g_rcr_cnt > 1)g_rcr_cnt--;   /* g_rcr_cntС�ڵ���1��ʾ������� */
            else
            {
                g_rcr_cnt = 0;          /* g_rcr_cnt���� */
                TIM8->CR1 &= ~(1 << 0); /* �رն�ʱ��TIMX */
                break;                  /* ������� */
            }
            
            ndata = slcd_get_data_remain();  /* ��������Ҫ��? */
            
            if (ndata)                  /* ������Ҫ���� */
            {
                TIM8->RCR = ndata - 1;  /* �����ظ������Ĵ���ֵΪnpwm-1, ��npwm������ */
            }
            
            TIM8->SR &= ~(1 << 0);          /* ����жϱ�־λ */
        }
        //printf("DR:%X\r\n",SPI1->DR);
    }

    TIM8->CNT = 0;                      /* ���������,�����´�ֱ������͵�ƽ */
    TIM8->DIER &= ~(1 << 10);           /* �ر�CC2��DMA���� */
    TIM8->SR = 0;                       /* ���TIM8״̬�Ĵ��� */
    DMA2_Channel5->CCR &= ~(1 << 0);    /* �ر�DMA���� */

    slcd_gpio_set(0);                   /* ����WR��Ϊͨ��������� */
    slcd_spi2_mode(0);                  /* SPI�ָ�8λȫ˫��ģʽ */
    LCD_CS(1);                          /* �ر�LCDƬѡ */
    NORFLASH_CS(1);                     /* �ر�FLASHƬѡ */
    lcd_scan_dir(DFT_SCAN_DIR);         /* �ָ�Ĭ�Ϸ��� */

}

/**
 * @brief       ��ʾһ֡,������һ��spi��lcd����ʾ.
 * @param       x       : ����ƫ����
 * @retval      ��
 */
void slcd_frame_show(uint16_t x)
{
    OS_CPU_SR cpu_sr = 0;
    lcd_scan_dir(U2D_L2R);  /* ����ɨ�跽�� */

    if (lcddev.id == 0X9341 || lcddev.id == 0X7789 || lcddev.id == 0X5310 || lcddev.id == 0X7796 || lcddev.id == 0X5510 || lcddev.id == 0X9806)
    {
        lcd_set_window(spbdev.stabarheight, 0, spbdev.spbheight, spbdev.spbwidth);
        lcd_set_cursor(spbdev.stabarheight, 0); /* ���ù��λ�� */
    }
    else
    {
        lcd_set_window(0, spbdev.stabarheight, spbdev.spbwidth, spbdev.spbheight);

        if (lcddev.id != 0X1963)lcd_set_cursor(0, spbdev.stabarheight); /* ���ù��λ�� */
    }

    lcd_write_ram_prepare();    /* ��ʼд��GRAM */
    
    OS_ENTER_CRITICAL();        /* �����ٽ���(�޷����жϴ��) */
    slcd_dma_enable(x);         /* ����һ��SPI��LCD��dma���� */
    OS_EXIT_CRITICAL();         /* �˳��ٽ���(���Ա��жϴ��) */
    
    lcd_scan_dir(DFT_SCAN_DIR); /* �ָ�Ĭ�Ϸ��� */
    lcd_set_window(0, 0, lcddev.width, lcddev.height);  /* �ָ�Ĭ�ϴ��ڴ�С */
}















