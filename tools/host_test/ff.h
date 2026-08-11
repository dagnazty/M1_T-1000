/* See COPYING.txt for license details. */

/*
 * ff.h  (HOST TEST SHIM — not the real FatFs)
 *
 * Minimal FatFs API surface, implemented over stdio in ff_shim.c, so the real
 * flipper_file.c / flipper_ir.c can be compiled and round-trip tested on a PC.
 * Only the symbols those modules actually use are provided here. This file is
 * NEVER part of the firmware build (the real FatFs/ff.h is used there).
 *
 * M1 Project — host test harness
 */

#ifndef FF_H_
#define FF_H_

#include <stdio.h>

typedef char           TCHAR;
typedef unsigned int   UINT;
typedef unsigned char  BYTE;
typedef unsigned long  FSIZE_t;

/* Opaque-to-callers file object; wraps a stdio stream on the host. */
typedef struct {
	FILE *fp;
} FIL;

/* File status structure (only the field the parsers may read). */
typedef struct {
	FSIZE_t fsize;
} FILINFO;

/* Result codes — only FR_OK is compared by name; the rest give error variety. */
typedef enum {
	FR_OK = 0,
	FR_DISK_ERR,
	FR_INT_ERR,
	FR_NOT_READY,
	FR_NO_FILE,
	FR_NO_PATH,
	FR_INVALID_NAME,
	FR_DENIED,
	FR_EXIST,
	FR_INVALID_OBJECT
} FRESULT;

/* File access mode flags — values match the real FatFs ff.h exactly. */
#define FA_READ            0x01
#define FA_WRITE           0x02
#define FA_OPEN_EXISTING   0x00
#define FA_CREATE_NEW      0x04
#define FA_CREATE_ALWAYS   0x08
#define FA_OPEN_ALWAYS     0x10
#define FA_OPEN_APPEND     0x30

FRESULT f_open   (FIL *fp, const TCHAR *path, BYTE mode);
FRESULT f_close  (FIL *fp);
FRESULT f_unlink (const TCHAR *path);
FRESULT f_rename (const TCHAR *path_old, const TCHAR *path_new);
FRESULT f_stat   (const TCHAR *path, FILINFO *fno);
int     f_printf (FIL *fp, const TCHAR *str, ...);
TCHAR  *f_gets   (TCHAR *buff, int len, FIL *fp);

#endif /* FF_H_ */
