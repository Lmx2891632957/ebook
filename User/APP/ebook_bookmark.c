/**
 ****************************************************************************************************
 * @file        ebook_bookmark.c
 * @author      ALIENTEK
 * @brief       Ebook bookmark module implementation (~170 lines)
 * @license     Copyright (c) 2020-2032, ALIENTEK
 ****************************************************************************************************
 */

#include "ebook_bookmark.h"
#include "./FATFS/exfuns/exfuns.h"
#include "string.h"

/**
 * @brief       Build bookmark file path from book path
 *              e.g. "0:/books/▒▒▒▒.txt" -> "0:/books/▒▒▒▒.bmk"
 * @param       book_path   : original book path (GBK string)
 * @param       dst         : output buffer (must be at least 280 bytes)
 * @retval      None
 */
void ebook_bm_get_path(const uint8_t *book_path, uint8_t *dst)
{
    uint8_t *dot = NULL;
    uint8_t *p   = (uint8_t *)book_path;
    uint8_t *d;

    /* Copy full path */
    strcpy((char *)dst, (const char *)book_path);

    /* Find last '.' in filename */
    while (*p)
    {
        if (*p == '.') dot = p;
        p++;
    }

    d = dst + (dot ? (dot - book_path) : strlen((const char *)dst));

    /* Replace or append extension */
    *d++ = '.';
    *d++ = 'b';
    *d++ = 'm';
    *d++ = 'k';
    *d   = '\0';
}

/**
 * @brief       Load bookmark set from SD card for given book
 *              Returns empty set if no bookmark file exists
 * @param       book_path   : full path of the book file (GBK)
 * @retval      pointer to ebook_bm_book_t (heap-allocated), NULL on alloc failure
 */
ebook_bm_book_t *ebook_bm_load(const uint8_t *book_path)
{
    ebook_bm_book_t *bm;
    uint8_t  *bm_path;       /* heap-allocated to save stack (280 bytes) */
    FIL      *bm_file;       /* heap-allocated to save stack (~512 bytes) */
    UINT     br;
    uint8_t  res;

    /* Allocate path buffer on heap to avoid stack overflow */
    bm_path = (uint8_t *)gui_memin_malloc(280);
    if (!bm_path) return NULL;

    /* Allocate FIL on heap (contains 512B sector buffer) */
    bm_file = (FIL *)gui_memin_malloc(sizeof(FIL));
    if (!bm_file) { gui_memin_free(bm_path); return NULL; }

    bm = (ebook_bm_book_t *)gui_memin_malloc(sizeof(ebook_bm_book_t));
    if (!bm) { gui_memin_free(bm_path); gui_memin_free(bm_file); return NULL; }

    /* Try to open existing bookmark file */
    ebook_bm_get_path(book_path, bm_path);
    res = f_open(bm_file, (const TCHAR *)bm_path, FA_READ);

    if (res == FR_OK)
    {
        /* Read entire structure (424 bytes) */
        res = f_read(bm_file, bm, sizeof(ebook_bm_book_t), &br);
        f_close(bm_file);

        /* Validate: magic + path match + count within range */
        if (res == FR_OK
                && br == sizeof(ebook_bm_book_t)
                && bm->magic == EBOOK_BM_MAGIC
                && bm->count <= EBOOK_BM_MAX
                && strcmp((const char *)bm->book_path, (const char *)book_path) == 0)
        {
            gui_memin_free(bm_path);
            gui_memin_free(bm_file);
            return bm;  /* Valid, return as-is */
        }
        /* else: corrupted, fall through to init empty */
    }

    /* Initialize empty bookmark set */
    bm->magic = EBOOK_BM_MAGIC;
    bm->count = 0;
    strcpy((char *)bm->book_path, (const char *)book_path);
    gui_memset(bm->entries, 0, sizeof(bm->entries));

    gui_memin_free(bm_path);
    gui_memin_free(bm_file);
    return bm;
}

/**
 * @brief       Save bookmark set to SD card
 *              File is 424 bytes, single f_write is safe (atomic at sector level)
 * @param       bm  : bookmark set to save
 * @retval      0: success; non-zero: FATFS error code
 */
uint8_t ebook_bm_save(ebook_bm_book_t *bm)
{
    uint8_t  *bm_path;       /* heap-allocated to save stack (280 bytes) */
    FIL      *bm_file;       /* heap-allocated to save stack (~512 bytes) */
    UINT     bw;
    uint8_t  res;

    if (!bm) return 1;

    bm_path = (uint8_t *)gui_memin_malloc(280);
    if (!bm_path) return 1;

    /* Allocate FIL on heap (contains 512B sector buffer) */
    bm_file = (FIL *)gui_memin_malloc(sizeof(FIL));
    if (!bm_file) { gui_memin_free(bm_path); return 1; }

    bm->magic = EBOOK_BM_MAGIC;  /* ensure magic is set */

    ebook_bm_get_path(bm->book_path, bm_path);
    res = f_open(bm_file, (const TCHAR *)bm_path, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) { gui_memin_free(bm_path); gui_memin_free(bm_file); return res; }

    res = f_write(bm_file, bm, sizeof(ebook_bm_book_t), &bw);
    f_close(bm_file);

    gui_memin_free(bm_path);
    gui_memin_free(bm_file);
    return (res == FR_OK && bw == sizeof(ebook_bm_book_t)) ? 0 : 1;
}

/**
 * @brief       Add a bookmark entry
 * @param       bm          : bookmark set
 * @param       file_offset : byte offset in book file
 * @param       page_num    : current page number (for display)
 * @retval      0: success; 1: full; 2: already exists at this position
 */
uint8_t ebook_bm_add(ebook_bm_book_t *bm, uint32_t file_offset, uint32_t page_num)
{
    uint8_t i;
    uint32_t delta;

    if (!bm) return 1;
    if (bm->count >= EBOOK_BM_MAX) return 1;  /* full */

    /* Check for duplicate (within ▒EBOOK_BM_DEDUP_DELTA bytes) */
    for (i = 0; i < bm->count; i++)
    {
        delta = (bm->entries[i].file_offset > file_offset)
                ? (bm->entries[i].file_offset - file_offset)
                : (file_offset - bm->entries[i].file_offset);
        if (delta < EBOOK_BM_DEDUP_DELTA) return 2;  /* already exists */
    }

    /* Add new entry at end */
    bm->entries[bm->count].file_offset = file_offset;
    bm->entries[bm->count].page_num   = page_num;
    bm->entries[bm->count].reserved1  = 0;
    bm->entries[bm->count].reserved2  = 0;
    bm->count++;

    return 0;
}

/**
 * @brief       Delete a bookmark by index
 *              Shifts subsequent entries left to fill the gap
 * @param       bm    : bookmark set
 * @param       index : 0-based index to delete
 * @retval      0: success; 1: index out of range
 */
uint8_t ebook_bm_delete(ebook_bm_book_t *bm, uint8_t index)
{
    uint8_t i;

    if (!bm || index >= bm->count) return 1;

    /* Shift remaining entries left */
    for (i = index; i < bm->count - 1; i++)
    {
        bm->entries[i] = bm->entries[i + 1];
    }

    /* Clear the last (now unused) slot */
    gui_memset(&bm->entries[bm->count - 1], 0, sizeof(ebook_bm_entry_t));
    bm->count--;

    return 0;
}

/**
 * @brief       Free bookmark set memory
 * @param       bm  : bookmark set to free
 * @retval      None
 */
void ebook_bm_free(ebook_bm_book_t *bm)
{
    if (bm)
    {
        gui_memin_free(bm);
    }
}
