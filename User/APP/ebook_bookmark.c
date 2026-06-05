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
 * @brief       Extract the first sentence from a file position as bookmark label
 *
 *              Reads raw bytes from file at given offset, skips leading
 *              whitespace/newlines, and copies the first meaningful sentence
 *              into the output buffer.  A sentence ends at:
 *                - GBK period:       0xA1 0xA3 "。"
 *                - GBK question:     0xA1 0xBF "？"
 *                - GBK exclamation:  0xA1 0xB6 "！"
 *                - GBK ellipsis:     0xA1 0xAD "…"
 *                - Newline (CR+LF or LF)
 *                - Buffer full (max_len reached)
 *
 *              If no sentence-ending char is found within max_len bytes,
 *              the buffer is filled to max_len-1 and null-terminated
 *              (truncated at a clean GBK boundary).
 *
 * @param       file    : open book file handle
 * @param       offset  : byte offset of page start in file
 * @param       label   : output buffer (GBK encoded, null-terminated)
 * @param       max_len : max bytes to write including null terminator
 * @retval      None
 */
void ebook_bm_extract_label(FIL *file, uint32_t offset, uint8_t *label, uint8_t max_len)
{
    uint8_t  buf[128];      /* enough for first sentence of any page */
    UINT     br;
    uint8_t  out_idx = 0;
    uint16_t i = 0;

    if (!file || !label || max_len < 2) return;

    /* Read a small chunk from page start */
    f_lseek(file, offset);
    f_read(file, buf, sizeof(buf), &br);

    if (br == 0)
    {
        label[0] = '\0';
        return;
    }

    /* ---- Step 1: skip leading whitespace and newlines ---- */
    while (i < br)
    {
        uint8_t byte = buf[i];

        /* Skip newline sequences */
        if (byte == 0x0D && (i + 1 < br) && buf[i + 1] == 0x0A)
        {
            i += 2;
            continue;
        }
        if (byte == 0x0A)
        {
            i += 1;
            continue;
        }
        /* Skip space (0x20) and full-width space (GBK: 0xA1 0xA1) */
        if (byte == 0x20)
        {
            i += 1;
            continue;
        }
        if (byte == 0xA1 && (i + 1 < br) && buf[i + 1] == 0xA1)
        {
            i += 2;
            continue;
        }
        /* Skip tab */
        if (byte == 0x09)
        {
            i += 1;
            continue;
        }

        /* Non-whitespace found — start extracting */
        break;
    }

    /* ---- Step 2: copy characters until sentence end or buffer full ---- */
    while (i < br && out_idx + 3 < max_len)  /* reserve 3 bytes: 2 for GBK + 1 for \0 */
    {
        uint8_t byte = buf[i];

        /* ---- Check for newline (sentence boundary) ---- */
        if (byte == 0x0D || byte == 0x0A)
            break;

        /* ---- GBK Chinese character (lead byte >= 0x81) ---- */
        if (byte >= 0x81 && (i + 1 < br) && buf[i + 1] >= 0x40)
        {
            uint8_t b1 = byte;
            uint8_t b2 = buf[i + 1];

            /* Check for sentence-ending punctuation */
            if ((b1 == 0xA1 && b2 == 0xA3)   /* 。*/
                    || (b1 == 0xA1 && b2 == 0xBF)   /* ？*/
                    || (b1 == 0xA1 && b2 == 0xB6)   /* ！*/
                    || (b1 == 0xA1 && b2 == 0xAD))  /* …*/
            {
                /* Include the punctuation in the label, then stop */
                if (out_idx + 2 < max_len)
                {
                    label[out_idx++] = b1;
                    label[out_idx++] = b2;
                }
                break;
            }

            /* Regular GBK character: copy 2 bytes */
            if (out_idx + 2 < max_len)
            {
                label[out_idx++] = b1;
                label[out_idx++] = b2;
            }
            i += 2;
        }
        else
        {
            /* ASCII character */
            if (out_idx + 1 < max_len)
            {
                label[out_idx++] = byte;
            }
            i += 1;
        }
    }

    /* ---- Step 3: null terminate ---- */
    label[out_idx] = '\0';

    /* ---- Step 4: if empty (all whitespace page), fallback to empty string ---- */
    if (out_idx == 0)
    {
        label[0] = '\0';  /* caller will use default name */
    }
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
        /* Read entire structure (704 bytes) */
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
 *              File is 704 bytes, single f_write is safe (atomic at sector level)
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
 * @param       label       : first-sentence label (GBK string, max EBOOK_BM_LABEL_LEN-1 bytes)
 * @retval      0: success; 1: full; 2: already exists at this position
 */
uint8_t ebook_bm_add(ebook_bm_book_t *bm, uint32_t file_offset, uint32_t page_num, uint8_t *label)
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

    /* Copy label (with truncation safety) */
    if (label && label[0] != '\0')
    {
        strncpy((char *)bm->entries[bm->count].label, (const char *)label, EBOOK_BM_LABEL_LEN - 1);
        bm->entries[bm->count].label[EBOOK_BM_LABEL_LEN - 1] = '\0';
    }
    else
    {
        bm->entries[bm->count].label[0] = '\0';
    }

    bm->entries[bm->count].reserved  = 0;
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
