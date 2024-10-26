/*
	SEC_YES_CHANGE is a driver that allows you to write to read-only memory protected by the SEC_NO_CHANGE flag.
*/

#include "MMVAD.h" // thank you vergiliusproject <3
#include <intrin.h>

#pragma warning(push)
#pragma warning(disable: 001) // vcr001 function def not found disable (ty Dave Plummer for showing me how to do this)

// why is it mm? is it hungry?
// "it means memory management"--no, it's hungry
extern NTKERNELAPI NTSTATUS MmCopyVirtualMemory(IN PEPROCESS FromProcess, IN PVOID FromAddress, IN PEPROCESS ToProcess, OUT PVOID ToAddress, IN SIZE_T BufferSize, IN KPROCESSOR_MODE PreviousMode, OUT PSIZE_T NumberOfBytesCopied);
extern NTKERNELAPI NTSTATUS MmProtectVirtualMemory(IN PEPROCESS Process, IN PVOID* BaseAddress, IN SIZE_T* NumberOfBytesToProtect, IN ULONG NewAccessProtection, OUT PULONG OldAccessProtection);
#pragma warning(pop)

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD Unload;

#define CTL_NUM(num) num * ( 8 << num)
#define VADROOT_W1122H2 0x7d8 // i am NOT finding this dynamically

enum IOCTL_CODES
{
	IOCTL_WRITERESTORE = CTL_CODE(FILE_DEVICE_UNKNOWN, CTL_NUM(5), METHOD_BUFFERED, FILE_ANY_ACCESS),
	IOCTL_WRITENORESTORE = CTL_CODE(FILE_DEVICE_UNKNOWN, CTL_NUM(10), METHOD_BUFFERED, FILE_ANY_ACCESS),
	IOCTL_STRIP = CTL_CODE(FILE_DEVICE_UNKNOWN, CTL_NUM(15), METHOD_BUFFERED, FILE_ANY_ACCESS),
};

struct IOCTL_REQUEST
{
	ULONG ulProcessId;

	ULONG_PTR pulAddress;
	SIZE_T pulSize;
	PVOID pBuffer;

	ULONG ulNewProtection; // New protection flags
};

NTSTATUS Create(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return Irp->IoStatus.Status;
}

NTSTATUS Close(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return Irp->IoStatus.Status;
}

NTSTATUS FindVad(PEPROCESS Process, ULONG_PTR VirtualAddress, PMMVAD_SHORT* pResult)
{
	ULONG_PTR pulVpnStart = VirtualAddress >> PAGE_SHIFT; // turn VA into VPN

	// get virtual address descriptor root
	struct _RTL_AVL_TREE* psTable = (struct _RTL_AVL_TREE*)((PUCHAR)Process + VADROOT_W1122H2);
	//struct _RTL_BALANCED_NODE* psNode = (psTable->Root);

	PRTL_BALANCED_NODE sNodeToExamine;
	ULONG_PTR pulStartVpn;
	ULONG_PTR pulEndVpn;
	PMMVAD_SHORT sVadNode;

	sNodeToExamine = (PRTL_BALANCED_NODE)(psTable->Root);

	for (;;) 
	{
		sVadNode = (PMMVAD_SHORT)sNodeToExamine;
		pulStartVpn = sVadNode->StartingVpn;
		pulEndVpn = sVadNode->EndingVpn;

		if (pulVpnStart < pulStartVpn)
		{
			if (sNodeToExamine->Left != NULL)
				sNodeToExamine = sNodeToExamine->Left;
			else
				return STATUS_NOT_FOUND;

		}
		else if (pulVpnStart <= pulEndVpn) {
			*pResult = sVadNode;

			return STATUS_SUCCESS;
		}
		else 
		{
			if (sNodeToExamine->Right != NULL)
				sNodeToExamine = sNodeToExamine->Right;
			else 
				return STATUS_NOT_FOUND;
		}
	}
}

NTSTATUS SetNoChange(PEPROCESS Process, ULONG_PTR VirtualAddress, ULONG NoChange, ULONG Protection)
{
	PMMVAD_SHORT sVad = NULL;

	if (NoChange != 0 && NoChange != 1)
	{
		DbgPrint("Invalid NoChange value.\n");

		return STATUS_INVALID_PARAMETER;
	}

	if (!NT_SUCCESS(FindVad(Process, VirtualAddress, &sVad)))
	{
		DbgPrint("VAD not found.\n");

		return STATUS_NOT_FOUND;
	}

	DbgPrint("SEC_NO_CHANGE flag: '%d'\n", sVad->u.VadFlags.NoChange);
	DbgPrint("Protection: '%x'\n", sVad->u.VadFlags.Protection);

	sVad->u.VadFlags.NoChange = NoChange;
	sVad->u.VadFlags.Protection = Protection;

	DbgPrint("SEC_NO_CHANGE set to: '%d'\n", sVad->u.VadFlags.NoChange);
	DbgPrint("New protection: '%x'\n", sVad->u.VadFlags.Protection);

	return STATUS_SUCCESS;
}

NTSTATUS DrvWrite(PEPROCESS Process, ULONG_PTR Address, PVOID Buffer, SIZE_T Size)
{
	NTSTATUS lStatus = STATUS_SUCCESS;
	SIZE_T pulBytesWritten = 0;

	__writecr0(__readcr0() & (~(1 << 16))); // disable write protection (i have been advised not to do this, i will do it anyway)

	lStatus = MmCopyVirtualMemory(PsGetCurrentProcess(), Buffer, Process, (PVOID)Address, Size, KernelMode, &pulBytesWritten);

	if (NT_SUCCESS(lStatus))
		DbgPrint("Write successful.\n");
	else
	{
		DbgPrint("Write failed.\n");

		// print the reason why it failed
		DbgPrint("Status: %x\n", lStatus);
	}

	__writecr0(__readcr0() | (1 << 16)); // enable write protection

	return lStatus;
}

NTSTATUS HandleWriteOperation(PEPROCESS Process, ULONG_PTR Address, PVOID Buffer, SIZE_T Size, ULONG NewProtection, BOOLEAN Restore)
{
	NTSTATUS lStatus = SetNoChange(Process, Address, 0UL, 6); // 6 should set the page to PAGE_READWRITE, but it's bipolar

	if (!NT_SUCCESS(lStatus))
	{
		DbgPrint("Failed to strip SEC_NO_CHANGE flag.\n");

		return lStatus;
	}

	DbgPrint("Writing to address '%llx'...\n", Address);

	lStatus = DrvWrite(Process, Address, Buffer, Size);

	if (!NT_SUCCESS(lStatus))
	{
		DbgPrint("Write failed.\n");

		return lStatus;
	}

	if (Restore)
	{
		lStatus = SetNoChange(Process, Address, 1UL, NewProtection);

		if (!NT_SUCCESS(lStatus))
			DbgPrint("Failed to restore SEC_NO_CHANGE flag.\n");
	}

	return lStatus;
}

NTSTATUS DeviceCTL(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	NTSTATUS lStatus = STATUS_UNSUCCESSFUL;

	DbgPrint("DeviceCTL called.\n");

	PIO_STACK_LOCATION sStack = IoGetCurrentIrpStackLocation(Irp);
	struct IOCTL_REQUEST* sRequest = (struct IOCTL_REQUEST*)Irp->AssociatedIrp.SystemBuffer; // Get the request buffer

	if (!sRequest || !sStack)
	{
		DbgPrint("Invalid request.\n");

		Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);

		return STATUS_INVALID_PARAMETER;
	}

	PEPROCESS sProcess = NULL;
	lStatus = PsLookupProcessByProcessId((HANDLE)sRequest->ulProcessId, &sProcess);

	if (!sProcess)
	{
		DbgPrint("Process with PID %d not found.\n", sRequest->ulProcessId);

		Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);

		return STATUS_INVALID_PARAMETER;
	}

	switch (sStack->Parameters.DeviceIoControl.IoControlCode)
	{

		case IOCTL_STRIP:
			lStatus = SetNoChange(sProcess, sRequest->pulAddress, 0, sRequest->ulNewProtection);
			break;

		case IOCTL_WRITERESTORE:
			lStatus = HandleWriteOperation(sProcess, sRequest->pulAddress, sRequest->pBuffer, sRequest->pulSize, sRequest->ulNewProtection, TRUE);
			break;

		case IOCTL_WRITENORESTORE:
			lStatus = HandleWriteOperation(sProcess, sRequest->pulAddress, sRequest->pBuffer, sRequest->pulSize, sRequest->ulNewProtection, FALSE);
			break;

	}

	ObDereferenceObject(sProcess);

	Irp->IoStatus.Status = lStatus;
	Irp->IoStatus.Information = sizeof(struct IOCTL_REQUEST);
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return lStatus;
}

VOID Unload(PDRIVER_OBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);
	
	DbgPrint("SEC_YES_CHANGE driver unloading...\n");

	UNICODE_STRING sSymbolicLinkName;
	RtlInitUnicodeString(&sSymbolicLinkName, L"\\DosDevices\\SEC_YES_CHANGE");

	IoDeleteSymbolicLink(&sSymbolicLinkName);

	IoDeleteDevice(DriverObject->DeviceObject);

	DbgPrint("Driver unloaded.\n");

	return;
}

NTSTATUS Entry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);

	DriverObject->DriverUnload = Unload;

	DbgPrint("SEC_YES_CHANGE driver loaded.\n");

	UNICODE_STRING sDeviceName;
	RtlInitUnicodeString(&sDeviceName, L"\\Device\\SEC_YES_CHANGE");

	PDEVICE_OBJECT sDeviceObject;
	NTSTATUS lStatus = IoCreateDevice(DriverObject, 0, &sDeviceName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &sDeviceObject);

	if (!NT_SUCCESS(lStatus))
	{
		DbgPrint("Failed to create device object.\n");

		return lStatus;
	}

	DbgPrint("Device object created.\n");

	UNICODE_STRING sSymbolicLinkName;
	RtlInitUnicodeString(&sSymbolicLinkName, L"\\DosDevices\\SEC_YES_CHANGE");

	lStatus = IoCreateSymbolicLink(&sSymbolicLinkName, &sDeviceName);

	if (!NT_SUCCESS(lStatus))
	{
		DbgPrint("Failed to create symbolic link.\n");

		IoDeleteDevice(sDeviceObject);

		return lStatus;
	}

	DbgPrint("Symbolic link created.\n");

	SetFlag(sDeviceObject->Flags, DO_BUFFERED_IO);

	DriverObject->MajorFunction[IRP_MJ_CREATE] = Create;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = Close;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceCTL;

	ClearFlag(sDeviceObject->Flags, DO_DEVICE_INITIALIZING);

	DbgPrint("Driver initialized.\n");

	return STATUS_SUCCESS;
}