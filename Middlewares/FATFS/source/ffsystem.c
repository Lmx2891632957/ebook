/**
 ****************************************************************************************************
 * @file        ffsystem.c
 * @author      ����ԭ���Ŷ�(ALIENTEK)
 * @version     V1.0
 * @date        2022-01-14
 * @brief       FATFS�ײ�(ffsystem) ��������
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
 * V1.0 20220114
 * ��һ�η���
 *
 ****************************************************************************************************
 */

#include "./MALLOC/malloc.h"
#include "./SYSTEM/sys/sys.h"
#include "./FATFS/source/ff.h"
#include "./SYSTEM/usart/usart.h"
#include "ucos_ii.h"
#include "./BSP/RTC/calendar.h"

volatile uint8_t cnt0 = 0;
volatile uint8_t cnt1 = 0;


OS_CPU_SR cpu_sr = 0;


/**
 * @brief       �����ٽ���
 * @param       fs   : FATFSָ��
 * @retval      ��
 */
void ff_enter(FATFS *fs)
{
    if (cnt0)
    {
        printf("in shit:%d\r\n", cnt0);
    }

    if (fs->pdrv != 2)
    {
        OS_ENTER_CRITICAL();    /* �����ٽ���(�޷����жϴ��) */
        cnt0++;
    }
    else
    {
        OSSchedLock();          /* ��ֹucos���� */
        cnt1++;
    }
}

/**
 * @brief       �˳��ٽ���
 * @param       fs   : FATFSָ��
 * @retval      ��
 */
void ff_leave(FATFS *fs)
{
    if (cnt0)
    {
        cnt0--;
        OS_EXIT_CRITICAL();     /* �˳��ٽ���(���Ա��жϴ��) */
    }

    if (cnt1)
    {
        cnt1--;
        OSSchedUnlock();        /* ����ucos���� */
    }
}

/**
 * @brief       ���ʱ��
 * @param       mf  : �ڴ��׵�ַ
 * @retval      ʱ��
 *   @note      ʱ������������:
 *              User defined function to give a current time to fatfs module
 *              31-25: Year(0-127 org.1980), 24-21: Month(1-12), 20-16: Day(1-31)
 *              15-11: Hour(0-23), 10-5: Minute(0-59), 4-0: Second(0-29 *2)
 */
DWORD get_fattime (void)
{
    uint32_t time = 0;
    calendar_get_date(&calendar);
    calendar_get_time(&calendar);

    if (calendar.year < 1980)calendar.year = 1980;

    time = (calendar.year - 1980) << 25;    /* ��� */
    time |= (calendar.month) << 21;         /* �·� */
    time |= (calendar.date) << 16;          /* ���� */
    time |= (calendar.hour) << 11;          /* ʱ */
    time |= (calendar.min) << 5;            /* �� */
    time |= (calendar.sec / 2);             /* �� */
    return time;
}

/**
 * @brief       ��̬�����ڴ�
 * @param       size : Ҫ������ڴ��С(�ֽ�)
 * @retval      ���䵽���ڴ��׵�ַ.
 */
void *ff_memalloc (UINT size)
{
    return (void*)mymalloc(SRAMIN,size);
}

/**
 * @brief       �ͷ��ڴ�
 * @param       mf  : �ڴ��׵�ַ
 * @retval      ��
 */
void ff_memfree (void* mf)
{
    myfree(SRAMIN,mf);
}

















