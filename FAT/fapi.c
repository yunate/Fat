#include "fapi.h"
#include "diskio.h"
#include <stdio.h>

FATFS FatFs[FF_VOLUMES];

extern PARTITION VolToPart[] ={
	{ 0, 0 },	/* "0:" <== PD# 1, 1st partition */
	{ 0, 1 },	/* "1:" <== PD# 1, 2st partition */
	{ 1, 0 },	/* "2:" <== PD# 2, 1st partition */
	{ 1, 1 },	/* "3:" <== PD# 2, 2st partition */
	{ 2, 0 },	/* "4:" <== PD# 3, 1st partition */
	{ 2, 1 },	/* "5:" <== PD# 3, 2st partition */
	{ 3, 0 },	/* "6:" <== PD# 4, 1st partition */
	{ 3, 1 },	/* "7:" <== PD# 4, 2st partition */
};

#define LD2PD(vol) VolToPart[vol].pd	/* Get physical drive number */
#define LD2PT(vol) VolToPart[vol].pt	/* Get partition index */

DWORD get_fattime(void)
{
	SYSTEMTIME tm;

	/* Get local time */
	GetLocalTime(&tm);

	/* Pack date and time into a DWORD variable */
	return    ((DWORD)(tm.wYear - 1980) << 25)
		| ((DWORD)tm.wMonth << 21)
		| ((DWORD)tm.wDay << 16)
		| (WORD)(tm.wHour << 11)
		| (WORD)(tm.wMinute << 5)
		| (WORD)(tm.wSecond >> 1);
}

int DirPathFormat(TCHAR * pPath)
{
	int nLen = wcslen(pPath);

	if ('\\' != pPath[nLen - 1])
	{
		pPath[nLen++] = '\\';
		pPath[nLen] = 0;
	}

	return nLen;
}

FAPI_RES Fapi_Init(int nIndex)
{
	FRESULT res = FR_DISK_ERR;

	if (RES_OK == assign_drives(LD2PD(4)))
	{
		TCHAR path[10];
		wsprintf(path, L"%d:", LD2PD(nIndex));
		res = f_mount(&FatFs[LD2PD(nIndex)], path, 0);

		if (RES_OK == disk_lock(LD2PD(nIndex), LD2PT(nIndex)))
		{
			return FAPI_OK;
		}
	}

	return FAPI_ERROR;
}

FAPI_RES Fapi_UnInit(int nIndex)
{
	disk_unlock(LD2PD(nIndex), LD2PT(nIndex));
	return FAPI_OK;
}

FAPI_RES Fapi_CopyFile(TCHAR* pDes, TCHAR* pSrc)
{
	FIL file;

	if (FR_OK != f_open(&file, pDes, FA_WRITE | FA_CREATE_NEW))
	{
		return FAPI_ERROR;
	}

	const int nBuffSize = 512 * 16;
	unsigned char buff[512 * 16];
	UINT nWritten;
	FILE* fp = _wfopen(pSrc, L"rb");
	int nReaded;

	if (fp)
	{
		memset(buff, 0, nBuffSize);
		nReaded = 0;

		for (;;)
		{
			nReaded = fread(buff, sizeof(unsigned char), nBuffSize, fp);

			if (0 < nReaded)
			{
				if (FR_OK != f_write(&file, buff, nReaded, &nWritten))
				{
					return FAPI_ERROR;
				}
			}
			else
			{
				break;
			}

		}

		fclose(fp);
	}

	if (FR_OK != f_close(&file))
	{
		return FAPI_ERROR;
	}

	return FAPI_OK;
}

FAPI_RES Fapi_CopyDir(TCHAR * pDes, TCHAR * pSrc)
{
	FRESULT res = f_mkdir(pDes);
	TCHAR pTmpSrc[MAX_PATH] ={ 0 };
	wcscpy(pTmpSrc, pSrc);
	TCHAR pTmpDes[MAX_PATH] = { 0 };

	if (FR_OK == res || FR_EXIST == res)
	{
		HANDLE hFile = 0;
		WIN32_FIND_DATA fileInfo;
		memset(&fileInfo, 0, sizeof(LPWIN32_FIND_DATA));
		int nLen = DirPathFormat(pTmpSrc);
		pTmpSrc[nLen++] = '*';
		pTmpSrc[nLen] = 0;
		hFile = FindFirstFile(pTmpSrc, &fileInfo);
		pTmpSrc[--nLen] = 0;

		if (INVALID_HANDLE_VALUE == hFile)
		{
			return FAPI_ERROR;
		}

		do
		{
			//如果是当前目录或者是上级目录，就直接进入下一次循环  
			if ('.' == fileInfo.cFileName[0])
			{
				continue;
			}

			wcscpy(pTmpSrc, pSrc);
			DirPathFormat(pTmpSrc);
			wcscat(pTmpSrc, fileInfo.cFileName);
			wcscpy(pTmpDes, pDes);
			DirPathFormat(pTmpDes);
			wcscat(pTmpDes, fileInfo.cFileName);

			if (FILE_ATTRIBUTE_DIRECTORY == fileInfo.dwFileAttributes)
			{
				// 这是一个目录
				if (FAPI_ERROR == Fapi_CopyDir(pTmpDes, pTmpSrc))
				{
					return FAPI_ERROR;
				}
			}
			else
			{
				if (FAPI_ERROR == Fapi_CopyFile(pTmpDes, pTmpSrc))
				{
					return FAPI_ERROR;
				}
			}
		} while (FindNextFile(hFile, &fileInfo));

		FindClose(hFile);
		return FAPI_OK;
	}

	return FAPI_ERROR;
}
