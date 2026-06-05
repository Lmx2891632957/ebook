/**
 ****************************************************************************************************
 * @file        ebook_bookmark.h
 * @author      ALIENTEK
 * @brief       Ebook bookmark module header
 * @license     Copyright (c) 2020-2032, ALIENTEK
 ****************************************************************************************************
 */

#ifndef __EBOOK_BOOKMARK_H
#define __EBOOK_BOOKMARK_H

#include "common.h"
#include "FATFS/source/ff.h"

/* Bookmark configuration */
#define EBOOK_BM_MAX        10          /* max bookmarks per book */
#define EBOOK_BM_MAGIC      0x424D4B00  /* "BMK\0" magic number */
#define EBOOK_BM_DEDUP_DELTA 200        /* offset delta for duplicate detection (卤200 bytes) */
#define EBOOK_BM_LABEL_LEN   28          /* 书签首句最大字节数(GBK,约14个汉字+结尾符) */

/* Single bookmark entry (44 bytes — was 16 bytes) */
typedef struct {
    uint32_t file_offset;    /* primary key: byte offset in file */
    uint32_t page_num;       /* display only, not used for navigation */
    uint8_t  label[EBOOK_BM_LABEL_LEN];  /* first-sentence label (GBK string) */
    uint32_t reserved;       /* reserved for future use (was reserved2) */
} ebook_bm_entry_t;

/* Bookmark set for one book (also the on-disk format, 704 bytes total) */
typedef struct {
    uint32_t magic;                             /* magic: 0x424D4B00 */
    uint32_t count;                             /* number of valid bookmarks (0..10) */
    uint8_t  book_path[256];                    /* book path (GBK), for verification */
    ebook_bm_entry_t entries[EBOOK_BM_MAX];     /* bookmark array */
} ebook_bm_book_t;

/* API functions */
ebook_bm_book_t *ebook_bm_load(const uint8_t *book_path);                    /* load from SD, or init empty */
uint8_t          ebook_bm_save(ebook_bm_book_t *bm);                         /* save to SD, 0=success */
uint8_t          ebook_bm_add(ebook_bm_book_t *bm, uint32_t file_offset,
                              uint32_t page_num, uint8_t *label);            /* add bookmark, 0=ok 1=full 2=exists */
uint8_t          ebook_bm_delete(ebook_bm_book_t *bm, uint8_t index);        /* delete by index, 0=ok 1=invalid */
void             ebook_bm_free(ebook_bm_book_t *bm);                         /* free memory */
void             ebook_bm_get_path(const uint8_t *book_path, uint8_t *dst);  /* build .bmk file path */
void             ebook_bm_extract_label(FIL *file, uint32_t offset,
                                        uint8_t *label, uint8_t max_len);    /* extract first sentence as label */

#endif