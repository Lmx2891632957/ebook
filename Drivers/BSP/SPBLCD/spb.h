/**
 ****************************************************************************************************
 * @file        spb.h
 * @author      ALIENTEK
 * @version     V1.0
 * @brief       SPB declarations (minimal stub for ebook-only version)
 ****************************************************************************************************
 */

#ifndef __SPB_H
#define __SPB_H

#include "./SYSTEM/sys/sys.h"

/* SPB device struct (minimal stub) */
typedef struct
{
    uint16_t spbwidth;      /* SPB width */
    uint16_t spbheight;     /* SPB height */
    uint16_t stabarheight;  /* Status bar height */
} _spb_dev;

extern _spb_dev spbdev;     /* SPB device instance */

#endif
