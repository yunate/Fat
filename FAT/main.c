
#include "fapi.h"

/*-----------------------------------------------------------------------*/
/* Main                                                                  */
/*-----------------------------------------------------------------------*/

int main(void)
{
	DWORD ff = get_fattime();

	SYSTEMTIME tm1;
	SYSTEMTIME tm2;
	GetLocalTime(&tm1);
	{
		Fapi_Init(4);
		Fapi_CopyDir(L"4:", L"C:\\Users\\ydh\\Desktop\\新建文件夹");
		Fapi_UnInit(4);
	}
	GetLocalTime(&tm2);
}


