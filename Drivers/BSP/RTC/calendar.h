/**
 ****************************************************************************************************
 * @file        calendar.h
 * @author      ALIENTEK
 * @version     V1.0
 * @brief       Calendar function declarations (minimal stub for ebook-only version)
 ****************************************************************************************************
 */

#ifndef __CALENDAR_H
#define __CALENDAR_H

#include "./BSP/RTC/rtc.h"

/* Wrapper functions - original calendar.c removed, implemented as static inline */
static inline void calendar_get_date(_calendar_obj *cal)
{
    rtc_get_time();
}

static inline void calendar_get_time(_calendar_obj *cal)
{
    rtc_get_time();
}

#endif
