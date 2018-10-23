/*-----------------------------------------------------------------------*/
/* Low level disk control module for Win32              (C)ChaN, 2013    */
/*-----------------------------------------------------------------------*/

#include "diskio.h"		/* Declarations of disk functions */
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>


#define MAX_DRIVES	10		/* Max number of physical drives to be used */
#define	SZ_BLOCK	256		/* Block size to be returned by GET_BLOCK_SIZE command */

#define SZ_RAMDISK	128		/* Size of drive 0 (RAM disk) [MiB] */
#define SS_RAMDISK	512		/* Sector size of drive 0 (RAM disk) [byte] */


/*--------------------------------------------------------------------------

   Module Private Functions

---------------------------------------------------------------------------*/

#define	BUFSIZE 262144UL	/* Size of data transfer buffer */

typedef struct
{
	DSTATUS	status;
	WORD sz_sector;
	DWORD n_sectors;
	HANDLE h_drive;
} STAT;

static HANDLE hMutex, hTmrThread;
static int Drives;

static volatile STAT Stat[MAX_DRIVES];
static volatile HANDLE LOCKDEVICE[MAX_DRIVES][26];


static DWORD TmrThreadID;

static BYTE *Buffer, *RamDisk;	/* Poiter to the data transfer buffer and ramdisk */


/*-----------------------------------------------------------------------*/
/* Timer Functions                                                       */
/*-----------------------------------------------------------------------*/


DWORD WINAPI tmr_thread(LPVOID parms)
{
	DWORD dw;
	int drv;


	for (;;)
	{
		Sleep(100);
		for (drv = 0; drv < Drives; drv++)
		{
			Sleep(1);
			if (Stat[drv].h_drive != INVALID_HANDLE_VALUE && !(Stat[drv].status & STA_NOINIT) && WaitForSingleObject(hMutex, 100) == WAIT_OBJECT_0)
			{
				if (!DeviceIoControl(Stat[drv].h_drive, IOCTL_STORAGE_CHECK_VERIFY, 0, 0, 0, 0, &dw, 0))
					Stat[drv].status |= STA_NOINIT;
				ReleaseMutex(hMutex);
				Sleep(100);
			}
		}
	}
}



int get_status(
	BYTE nDiskNum
)
{
	volatile STAT *stat = &Stat[nDiskNum];
	HANDLE h = stat->h_drive;
	DISK_GEOMETRY_EX parms_ex;
	DISK_GEOMETRY parms;
	DWORD dw;

	/* Get drive size */
	stat->status = STA_NOINIT;
	if (!DeviceIoControl(h, IOCTL_STORAGE_CHECK_VERIFY, 0, 0, 0, 0, &dw, 0)) return 0;
	if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, 0, 0, &parms_ex, sizeof parms_ex, &dw, 0))
	{
		stat->sz_sector = (WORD)parms_ex.Geometry.BytesPerSector;
		stat->n_sectors = (DWORD)(parms_ex.DiskSize.QuadPart / stat->sz_sector);
	}
	else
	{
		if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY, 0, 0, &parms, sizeof parms, &dw, 0)) return 0;
		stat->n_sectors = parms.SectorsPerTrack * parms.TracksPerCylinder * (DWORD)parms.Cylinders.QuadPart;
		stat->sz_sector = (WORD)parms.BytesPerSector;
	}
	if (stat->sz_sector < FF_MIN_SS || stat->sz_sector > FF_MAX_SS) return 0;

	/* Get write protect status */
	stat->status = DeviceIoControl(h, IOCTL_DISK_IS_WRITABLE, 0, 0, 0, 0, &dw, 0) ? 0 : STA_PROTECT;

	return 1;
}




/*--------------------------------------------------------------------------

   Public Functions

---------------------------------------------------------------------------*/

DSTATUS disk_lock(BYTE nDiskNum, BYTE nPartNum)
{
	DWORD nr;
	char dn[MAX_PATH] = "\\\\.\\Harddisk9Partition9";
	dn[12] = nDiskNum + '0';
	HANDLE hd;
	dn[22] = nPartNum + '0' + 1;
	hd = CreateFileA(dn, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, 0, 0);

	if (hd == INVALID_HANDLE_VALUE)
	{
		return (RES_OK);
	}


	if (!DeviceIoControl(hd, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0,
		&nr, 0))
	{
		CloseHandle(hd);
		return (RES_ERROR);
	}

	LOCKDEVICE[nDiskNum][nPartNum] = hd;
	return (RES_ERROR);
}


DSTATUS disk_unlock(BYTE nDiskNum, BYTE nPartNum)
{
	if (0 == LOCKDEVICE[nDiskNum][nPartNum])
	{
		return (RES_ERROR);
	}

	CloseHandle(LOCKDEVICE[nDiskNum][nPartNum]);
	LOCKDEVICE[nDiskNum][nPartNum] = 0;

	return (RES_OK);
}

DSTATUS disk_reennum(BYTE nDiskNum)
{
	DWORD nr;
	if (!DeviceIoControl(Stat[nDiskNum].h_drive, IOCTL_DISK_UPDATE_PROPERTIES,
		NULL, 0, NULL, 0, &nr, 0))
	{
		return (RES_ERROR);
	}

	return (RES_OK);
}

/*-----------------------------------------------------------------------*/
/* Initialize Windows disk accesss layer                                 */
/*-----------------------------------------------------------------------*/

DSTATUS assign_drives(BYTE nDiskNum)
{
	if (Stat[nDiskNum].h_drive)
	{
		return (RES_OK);
	}

	WCHAR str[50];
	HANDLE h;
	hMutex = CreateMutex(0, 0, 0);

	if (hMutex == INVALID_HANDLE_VALUE)
	{
		return (RES_ERROR);
	}

	Buffer = VirtualAlloc(0, BUFSIZE, MEM_COMMIT, PAGE_READWRITE);

	if (!Buffer)
	{
		return (RES_ERROR);
	}

	RamDisk = VirtualAlloc(0, SZ_RAMDISK * 0x100000, MEM_COMMIT, PAGE_READWRITE);

	if (!RamDisk)
	{
		return (RES_ERROR);
	}

	swprintf(str, 50, L"\\\\.\\PhysicalDrive%u", nDiskNum);
	h = CreateFileW(str, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, 0);
	if (h != INVALID_HANDLE_VALUE)
	{
		Stat[nDiskNum].h_drive = h;
	}
	else
	{
		return (RES_ERROR);
	}

	hTmrThread = CreateThread(0, 0, tmr_thread, 0, 0, &TmrThreadID);
	Drives = MAX_DRIVES;
	return (RES_OK);
}


/*-----------------------------------------------------------------------*/
/* Initialize Disk Drive                                                 */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize(
	BYTE nDiskNum		/* Physical drive nmuber */
)
{
	DSTATUS sta;

	if (WaitForSingleObject(hMutex, 5000) != WAIT_OBJECT_0)
	{
		return STA_NOINIT;
	}

	if (nDiskNum >= Drives)
	{
		sta = STA_NOINIT;
	}
	else
	{
		get_status(nDiskNum);
		sta = Stat[nDiskNum].status;
	}

	ReleaseMutex(hMutex);
	return sta;
}



/*-----------------------------------------------------------------------*/
/* Get Disk Status                                                       */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status(
	BYTE nDiskNum		/* Physical drive nmuber (0) */
)
{
	DSTATUS sta;

	if (nDiskNum >= Drives)
	{
		sta = STA_NOINIT;
	}
	else
	{
		sta = Stat[nDiskNum].status;
	}

	return sta;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read(
	BYTE nDiskNum,			/* Physical drive nmuber (0) */
	BYTE *buff,			/* Pointer to the data buffer to store read data */
	DWORD sector,		/* Start sector number (LBA) */
	UINT count			/* Number of sectors to read */
)
{
	DWORD nc, rnc;
	LARGE_INTEGER ofs;
	DSTATUS res;


	if (nDiskNum >= Drives || Stat[nDiskNum].status & STA_NOINIT || WaitForSingleObject(hMutex, 3000) != WAIT_OBJECT_0)
	{
		return RES_NOTRDY;
	}

	nc = (DWORD)count * Stat[nDiskNum].sz_sector;
	ofs.QuadPart = (LONGLONG)sector * Stat[nDiskNum].sz_sector;
	
	if (nc > BUFSIZE)
	{
		res = RES_PARERR;
	}
	else
	{
		if (SetFilePointer(Stat[nDiskNum].h_drive, ofs.LowPart, &ofs.HighPart, FILE_BEGIN) != ofs.LowPart)
		{
			res = RES_ERROR;
		}
		else
		{
			if (!ReadFile(Stat[nDiskNum].h_drive, Buffer, nc, &rnc, 0) || nc != rnc)
			{
				res = RES_ERROR;
			}
			else
			{
				memcpy(buff, Buffer, nc);
				res = RES_OK;
			}
		}
	}

	ReleaseMutex(hMutex);
	return res;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

DRESULT disk_write(
	BYTE nDiskNum,			/* Physical drive nmuber (0) */
	const BYTE *buff,	/* Pointer to the data to be written */
	DWORD sector,		/* Start sector number (LBA) */
	UINT count			/* Number of sectors to write */
)
{
	DWORD nc = 0, rnc;
	LARGE_INTEGER ofs;
	DRESULT res;


	if (nDiskNum >= Drives || Stat[nDiskNum].status & STA_NOINIT || WaitForSingleObject(hMutex, 3000) != WAIT_OBJECT_0)
	{
		return RES_NOTRDY;
	}

	res = RES_OK;
	if (Stat[nDiskNum].status & STA_PROTECT)
	{
		res = RES_WRPRT;
	}
	else
	{
		nc = (DWORD)count * Stat[nDiskNum].sz_sector;
		if (nc > BUFSIZE) res = RES_PARERR;
	}

	ofs.QuadPart = (LONGLONG)sector * Stat[nDiskNum].sz_sector;

	if (res == RES_OK)
	{
		if (SetFilePointer(Stat[nDiskNum].h_drive, ofs.LowPart, &ofs.HighPart, FILE_BEGIN) != ofs.LowPart)
		{
			res = RES_ERROR;
		}
		else
		{
			memcpy(Buffer, buff, nc);
			if (!WriteFile(Stat[nDiskNum].h_drive, Buffer, nc, &rnc, 0) || nc != rnc)
			{
				res = RES_ERROR;
			}
		}
	}

	ReleaseMutex(hMutex);
	return res;
}



/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl(
	BYTE nDiskNum,		/* Physical drive nmuber (0) */
	BYTE ctrl,		/* Control code */
	void *buff		/* Buffer to send/receive data */
)
{
	DRESULT res;


	if (nDiskNum >= Drives || (Stat[nDiskNum].status & STA_NOINIT))
	{
		return RES_NOTRDY;
	}

	res = RES_PARERR;
	switch (ctrl)
	{
	case CTRL_SYNC:			/* Nothing to do */
		res = RES_OK;
		break;

	case GET_SECTOR_COUNT:	/* Get number of sectors on the drive */
		*(DWORD*)buff = Stat[nDiskNum].n_sectors;
		res = RES_OK;
		break;

	case GET_SECTOR_SIZE:	/* Get size of sector for generic read/write */
		*(WORD*)buff = Stat[nDiskNum].sz_sector;
		res = RES_OK;
		break;

	case GET_BLOCK_SIZE:	/* Get internal block size in unit of sector */
		*(DWORD*)buff = SZ_BLOCK;
		res = RES_OK;
		break;

	case 200:				/* Load disk image file to the RAM disk (drive 0) */
	{
		HANDLE h;
		DWORD br;

		if (nDiskNum == 0)
		{
			h = CreateFileW(buff, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
			if (h != INVALID_HANDLE_VALUE)
			{
				if (ReadFile(h, RamDisk, SZ_RAMDISK * 1024 * 1024, &br, 0))
				{
					res = RES_OK;
				}
				CloseHandle(h);
			}
		}
	}
	break;

	}

	return res;
}



