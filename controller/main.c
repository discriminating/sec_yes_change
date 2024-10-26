#include <windows.h>
#include <stdio.h>

enum IOCTL_CODES
{
	IOCTL_WRITERESTORE = 2233344,
	IOCTL_WRITENORESTORE = 2555904,
	IOCTL_STRIP = 15859712,
};

struct IOCTL_REQUEST
{
	ULONG ProcessId;

	ULONG_PTR Address;
	SIZE_T Size;
	PVOID wBuffer;

	ULONG sNewProtection;
};

int main(int argc, char* argv[])
{
	DWORD bytesReturned = 0;
	int IOCTL;

	DWORD pid;
	ULONG_PTR address2strip;
	ULONG sNewProtection;
	int writebuffer;

	if (argc != 6)
	{
		printf("Usage: %s <pid> <address> <ioctl> <writebuffer> <newprotection>\n", argv[0]);
		return 1;
	}

	sscanf_s(argv[1], "%d", &pid);
	sscanf_s(argv[2], "%llx", &address2strip);
	sscanf_s(argv[3], "%d", &IOCTL);
	sscanf_s(argv[4], "%d", &writebuffer);
	sscanf_s(argv[5], "%d", &sNewProtection);

	HANDLE hDevice = CreateFile(L"\\\\.\\SEC_YES_CHANGE", GENERIC_READ | GENERIC_WRITE, NULL, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDevice == INVALID_HANDLE_VALUE)
	{
		printf("osspps couldn topen device hand:(le : %d\n", GetLastError());
		return 1;
	}

	struct IOCTL_REQUEST request = { 0 };
	request.ProcessId = pid;
	request.Address = address2strip;
	request.sNewProtection = sNewProtection;

	if (IOCTL == 1)
	{
		request.wBuffer = &writebuffer;
		request.Size = sizeof(writebuffer);
		DeviceIoControl(hDevice, IOCTL_WRITERESTORE, &request, sizeof(request), &request, sizeof(request), &bytesReturned, NULL);
	}
	else if (IOCTL == 2)
	{
		request.wBuffer = &writebuffer;
		request.Size = sizeof(writebuffer);
		DeviceIoControl(hDevice, IOCTL_WRITENORESTORE, &request, sizeof(request), &request, sizeof(request), &bytesReturned, NULL);
	}
	else if (IOCTL == 3)
	{
		DeviceIoControl(hDevice, IOCTL_STRIP, &request, sizeof(request), &request, sizeof(request), &bytesReturned, NULL);
	}

	CloseHandle(hDevice);
	return 0;
}