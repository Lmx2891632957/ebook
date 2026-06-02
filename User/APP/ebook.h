/**
 ****************************************************************************************************
 * @file        ebook.h
 * @author      ALIENTEK
 * @version     V1.2
 * @date        2022-05-26
 * @brief       Ebook reader header
 * @license     Copyright (c) 2020-2032, ALIENTEK
 ****************************************************************************************************
 */

#ifndef __EBOOK_H
#define __EBOOK_H

#include "common.h"

#define EBOOK_RAW_BUF_SIZE  4096    /* file read chunk size (one page ~1KB, 4KB enough) */
#define EBOOK_PREV_MAX      40      /* max previous page offsets stored */
#define EBOOK_PAGE_BUF_SIZE 4096    /* page render buffer size */
#define EBOOK_SWIPE_THRESH  50      /* swipe threshold in pixels */
#define FONT_SEL_BTN_W      60      /* font selection button width */
#define FONT_SEL_BTN_H      40      /* font selection button height */
#define FONT_SEL_PAD         8      /* font selection panel padding */

/* ebook reader context */
typedef struct {
    FIL     *file;                   /* open file handle (heap-allocated, avoid stack overflow) */
    uint32_t file_size;              /* total file bytes */
    uint32_t page_start;             /* file offset where current page starts */
    uint32_t page_end;               /* file offset where current page ends */
    uint32_t page_num;               /* 1-based current page number */

    uint16_t text_width;             /* text area pixel width */
    uint16_t text_height;            /* text area pixel height */
    uint16_t text_x;                 /* text area left */
    uint16_t text_y;                 /* text area top */
    uint8_t  font_size;              /* font size: 12, 16, 24, or 32 */

    /* page offset history for backward navigation */
    uint32_t prev_starts[EBOOK_PREV_MAX];
    uint8_t  prev_count;             /* number of valid entries */

    uint8_t *raw_buf;                /* file read buffer */
    uint8_t *page_buf;               /* page render buffer */
    uint8_t *path;                   /* full file path */
    uint8_t *fname;                  /* display name (filename only) */
} ebook_ctx_t;

uint8_t ebook_play(void);            /* main entry point */

#endif
