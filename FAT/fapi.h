
#ifndef FAPI
#define FAPI

#include <windows.h>


typedef enum
{
	FAPI_OK,
	FAPI_ERROR
} FAPI_RES;

// 初始化
FAPI_RES Fapi_Init(int nIndex);
FAPI_RES Fapi_UnInit(int nIndex);

// 复制文件（从本地电脑到磁盘）
FAPI_RES Fapi_CopyFile(TCHAR* pDes, TCHAR* pSrc);

// 复制文件夹（从本地电脑pSrc里面的内容复制到磁盘pDes下）
FAPI_RES Fapi_CopyDir(TCHAR* pDes, TCHAR* pSrc);

#endif


