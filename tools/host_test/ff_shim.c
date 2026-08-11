/* See COPYING.txt for license details. */

/*
 * ff_shim.c  (HOST TEST SHIM — not the real FatFs)
 *
 * Implements the minimal FatFs API from the host ff.h over C stdio, so the real
 * flipper_file.c / flipper_ir.c can be exercised on a PC. Never part of the
 * firmware build.
 *
 * M1 Project — host test harness
 */

#include "ff.h"
#include <stdarg.h>
#include <stdio.h>

/*============================================================================*/
/**
 * @brief  Map a FatFs access mode to an fopen() mode string and open the file.
 */
FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode)
{
	const char *m;

	if (fp == NULL || path == NULL)
		return FR_INVALID_OBJECT;

	if (mode & FA_OPEN_APPEND)          /* 0x30: append, create if absent  */
		m = "ab";
	else if (mode & FA_CREATE_ALWAYS)   /* 0x08: create/truncate for write */
		m = "wb";
	else if (mode & FA_WRITE)           /* plain write, existing           */
		m = "r+b";
	else                                /* FA_READ | FA_OPEN_EXISTING      */
		m = "rb";

	fp->fp = fopen(path, m);
	if (fp->fp == NULL)
		return FR_NO_FILE;

	return FR_OK;
}

/*============================================================================*/
FRESULT f_close(FIL *fp)
{
	if (fp == NULL || fp->fp == NULL)
		return FR_INVALID_OBJECT;

	fclose(fp->fp);
	fp->fp = NULL;
	return FR_OK;
}

/*============================================================================*/
FRESULT f_unlink(const TCHAR *path)
{
	if (path == NULL)
		return FR_INVALID_NAME;

	return (remove(path) == 0) ? FR_OK : FR_NO_FILE;
}

/*============================================================================*/
FRESULT f_rename(const TCHAR *path_old, const TCHAR *path_new)
{
	if (path_old == NULL || path_new == NULL)
		return FR_INVALID_NAME;

	return (rename(path_old, path_new) == 0) ? FR_OK : FR_DISK_ERR;
}

/*============================================================================*/
FRESULT f_stat(const TCHAR *path, FILINFO *fno)
{
	FILE *f;
	long  sz;

	if (path == NULL)
		return FR_INVALID_NAME;

	f = fopen(path, "rb");
	if (f == NULL)
		return FR_NO_FILE;

	fseek(f, 0, SEEK_END);
	sz = ftell(f);
	fclose(f);

	if (fno != NULL)
		fno->fsize = (FSIZE_t)(sz < 0 ? 0 : sz);

	return FR_OK;
}

/*============================================================================*/
int f_printf(FIL *fp, const TCHAR *str, ...)
{
	va_list ap;
	int     r;

	if (fp == NULL || fp->fp == NULL)
		return -1;

	va_start(ap, str);
	r = vfprintf(fp->fp, str, ap);
	va_end(ap);

	return r;
}

/*============================================================================*/
TCHAR *f_gets(TCHAR *buff, int len, FIL *fp)
{
	if (buff == NULL || fp == NULL || fp->fp == NULL)
		return NULL;

	return fgets(buff, len, fp->fp);
}
