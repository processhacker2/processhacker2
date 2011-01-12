/*
 * Process Hacker - 
 *   KProcessHacker API
 * 
 * Copyright (C) 2009-2011 wj32
 * 
 * This file is part of Process Hacker.
 * 
 * Process Hacker is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Process Hacker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ph.h>
#include <kphuser.h>

NTSTATUS KphpDeviceIoControl(
    __in HANDLE KphHandle,
    __in ULONG KphControlCode,
    __in PVOID InBuffer,
    __in ULONG InBufferLength
    );

NTSTATUS KphConnect(
    __out PHANDLE KphHandle,
    __in_opt PWSTR DeviceName
    )
{
    NTSTATUS status;
    HANDLE kphHandle;
    UNICODE_STRING objectName;
    OBJECT_ATTRIBUTES objectAttributes;
    IO_STATUS_BLOCK isb;
    OBJECT_HANDLE_FLAG_INFORMATION handleFlagInfo;

    if (DeviceName)
        RtlInitUnicodeString(&objectName, DeviceName);
    else
        RtlInitUnicodeString(&objectName, KPH_DEVICE_NAME);

    InitializeObjectAttributes(
        &objectAttributes,
        &objectName,
        OBJ_CASE_INSENSITIVE,
        NULL,
        NULL
        );

    status = NtOpenFile(
        &kphHandle,
        FILE_GENERIC_READ | FILE_GENERIC_WRITE,
        &objectAttributes,
        &isb,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_NON_DIRECTORY_FILE
        );

    if (NT_SUCCESS(status))
    {
        // Protect the handle from being closed.

        handleFlagInfo.Inherit = FALSE;
        handleFlagInfo.ProtectFromClose = TRUE;

        NtSetInformationObject(
            kphHandle,
            ObjectHandleFlagInformation,
            &handleFlagInfo,
            sizeof(OBJECT_HANDLE_FLAG_INFORMATION)
            );

        *KphHandle = kphHandle;
    }

    return status;
}

NTSTATUS KphConnect2(
    __out PHANDLE KphHandle,
    __in_opt PWSTR DeviceName,
    __in PWSTR FileName
    )
{
    return KphConnect2Ex(KphHandle, DeviceName, FileName, NULL);
}

NTSTATUS KphConnect2Ex(
    __out PHANDLE KphHandle,
    __in_opt PWSTR DeviceName,
    __in PWSTR FileName,
    __in_opt PKPH_PARAMETERS Parameters
    )
{
    NTSTATUS status;
    WCHAR fullDeviceName[MAX_PATH + 1];
    SC_HANDLE scmHandle;
    SC_HANDLE serviceHandle;
    BOOLEAN started = FALSE;
    BOOLEAN created = FALSE;

    if (!DeviceName)
        DeviceName = KPH_DEVICE_SHORT_NAME;

    _snwprintf(fullDeviceName, MAX_PATH, L"\\Device\\%s", DeviceName);

    // Try to open the device.
    status = KphConnect(KphHandle, fullDeviceName);

    if (NT_SUCCESS(status))
        return status;

    if (
        status != STATUS_NO_SUCH_DEVICE &&
        status != STATUS_NO_SUCH_FILE &&
        status != STATUS_OBJECT_NAME_NOT_FOUND &&
        status != STATUS_OBJECT_PATH_NOT_FOUND
        )
        return status;

    // Load the driver, and try again.

    // Try to start the service, if it exists.

    scmHandle = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);

    if (scmHandle)
    {
        serviceHandle = OpenService(scmHandle, DeviceName, SERVICE_START);

        if (serviceHandle)
        {
            if (StartService(serviceHandle, 0, NULL))
                started = TRUE;

            CloseServiceHandle(serviceHandle);
        }

        CloseServiceHandle(scmHandle);
    }

    if (!started)
    {
        // Try to create the service.

        scmHandle = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);

        if (scmHandle)
        {
            serviceHandle = CreateService(
                scmHandle,
                DeviceName,
                DeviceName,
                SERVICE_ALL_ACCESS,
                SERVICE_KERNEL_DRIVER,
                SERVICE_DEMAND_START,
                SERVICE_ERROR_IGNORE,
                FileName,
                NULL,
                NULL,
                NULL,
                NULL,
                L""
                );

            if (serviceHandle)
            {
                created = TRUE;

                // Set parameters if the caller supplied them. 
                // Note that we fail the entire function if this fails, 
                // because failing to set parameters like SecurityLevel may 
                // result in security vulnerabilities.
                if (Parameters)
                {
                    status = KphSetParameters(DeviceName, Parameters);

                    if (!NT_SUCCESS(status))
                    {
                        // Delete the service and fail.
                        goto CreateAndConnectEnd;
                    }
                }

                StartService(serviceHandle, 0, NULL);
            }

            CloseServiceHandle(scmHandle);
        }
    }

    // Try to open the device again.
    status = KphConnect(KphHandle, fullDeviceName);

CreateAndConnectEnd:
    if (created)
    {
        // "Delete" the service. Since we (may) have a handle to 
        // the device, the SCM will delete the service automatically 
        // when it is stopped (upon reboot). If we don't have a 
        // handle to the device, the service will get deleted immediately, 
        // which is a good thing anyway.
        DeleteService(serviceHandle);
        CloseServiceHandle(serviceHandle);
    }

    return status;
}

NTSTATUS KphDisconnect(
    __in HANDLE KphHandle
    )
{
    OBJECT_HANDLE_FLAG_INFORMATION handleFlagInfo;

    // Unprotect the handle.

    handleFlagInfo.Inherit = FALSE;
    handleFlagInfo.ProtectFromClose = FALSE;

    NtSetInformationObject(
        KphHandle,
        ObjectHandleFlagInformation,
        &handleFlagInfo,
        sizeof(OBJECT_HANDLE_FLAG_INFORMATION)
        );

    return NtClose(KphHandle);
}

NTSTATUS KphSetParameters(
    __in_opt PWSTR DeviceName,
    __in PKPH_PARAMETERS Parameters
    )
{
    NTSTATUS status;
    HANDLE parametersKeyHandle = NULL;
    PPH_STRING parametersKeyName;
    ULONG disposition;
    UNICODE_STRING valueName;

    if (!DeviceName)
        DeviceName = KPH_DEVICE_SHORT_NAME;

    parametersKeyName = PhConcatStrings(
        3,
        L"System\\CurrentControlSet\\Services\\",
        DeviceName,
        L"\\Parameters"
        );
    status = PhCreateKey(
        &parametersKeyHandle,
        KEY_WRITE | DELETE,
        PH_KEY_LOCAL_MACHINE,
        &parametersKeyName->sr,
        0,
        0,
        &disposition
        );
    PhDereferenceObject(parametersKeyName);

    if (!NT_SUCCESS(status))
        return status;

    RtlInitUnicodeString(&valueName, L"SecurityLevel");
    status = NtSetValueKey(parametersKeyHandle, &valueName, 0, REG_DWORD, &Parameters->SecurityLevel, sizeof(ULONG));

    if (!NT_SUCCESS(status))
        goto SetValuesEnd;

    // Put more parameters here...

SetValuesEnd:
    if (!NT_SUCCESS(status))
    {
        // Delete the key if we created it.
        if (disposition == REG_CREATED_NEW_KEY)
            NtDeleteKey(parametersKeyHandle);
    }

    NtClose(parametersKeyHandle);

    return status;
}

NTSTATUS KphInstall(
    __in_opt PWSTR DeviceName,
    __in PWSTR FileName
    )
{
    return KphInstallEx(DeviceName, FileName, NULL);
}

NTSTATUS KphInstallEx(
    __in_opt PWSTR DeviceName,
    __in PWSTR FileName,
    __in_opt PKPH_PARAMETERS Parameters
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    SC_HANDLE scmHandle;
    SC_HANDLE serviceHandle;

    if (!DeviceName)
        DeviceName = KPH_DEVICE_SHORT_NAME;

    scmHandle = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);

    if (!scmHandle)
        return PhGetLastWin32ErrorAsNtStatus();

    serviceHandle = CreateService(
        scmHandle,
        DeviceName,
        DeviceName,
        SERVICE_ALL_ACCESS,
        SERVICE_KERNEL_DRIVER,
        SERVICE_SYSTEM_START,
        SERVICE_ERROR_IGNORE,
        FileName,
        NULL,
        NULL,
        NULL,
        NULL,
        L""
        );

    if (serviceHandle)
    {
        // See KphConnect2Ex for more details.
        if (Parameters)
        {
            status = KphSetParameters(DeviceName, Parameters);

            if (!NT_SUCCESS(status))
            {
                DeleteService(serviceHandle);
                goto CreateEnd;
            }
        }

        if (!StartService(serviceHandle, 0, NULL))
            status = PhGetLastWin32ErrorAsNtStatus();

CreateEnd:
        CloseServiceHandle(serviceHandle);
    }
    else
    {
        status = PhGetLastWin32ErrorAsNtStatus();
    }

    CloseServiceHandle(scmHandle);

    return status;
}

NTSTATUS KphUninstall(
    __in_opt PWSTR DeviceName
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    SC_HANDLE scmHandle;
    SC_HANDLE serviceHandle;

    if (!DeviceName)
        DeviceName = KPH_DEVICE_SHORT_NAME;

    scmHandle = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);

    if (!scmHandle)
        return PhGetLastWin32ErrorAsNtStatus();

    serviceHandle = OpenService(scmHandle, DeviceName, SERVICE_STOP | DELETE);

    if (serviceHandle)
    {
        SERVICE_STATUS serviceStatus;

        ControlService(serviceHandle, SERVICE_CONTROL_STOP, &serviceStatus);

        if (!DeleteService(serviceHandle))
            status = PhGetLastWin32ErrorAsNtStatus();

        CloseServiceHandle(serviceHandle);
    }
    else
    {
        status = PhGetLastWin32ErrorAsNtStatus();
    }

    CloseServiceHandle(scmHandle);

    return status;
}

NTSTATUS KphpDeviceIoControl(
    __in HANDLE KphHandle,
    __in ULONG KphControlCode,
    __in PVOID InBuffer,
    __in ULONG InBufferLength
    )
{
    IO_STATUS_BLOCK isb;

    return NtDeviceIoControlFile(
        KphHandle,
        NULL,
        NULL,
        NULL,
        &isb,
        KphControlCode,
        InBuffer,
        InBufferLength,
        NULL,
        0
        );
}

NTSTATUS KphGetFeatures(
    __in HANDLE KphHandle,
    __out PULONG Features
    )
{
    struct
    {
        PULONG Features;
    } input = { Features };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_GETFEATURES,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphOpenProcess(
    __in HANDLE KphHandle,
    __out PHANDLE ProcessHandle,
    __in ACCESS_MASK DesiredAccess,
    __in PCLIENT_ID ClientId
    )
{
    struct
    {
        PHANDLE ProcessHandle;
        ACCESS_MASK DesiredAccess;
        PCLIENT_ID ClientId;
    } input = { ProcessHandle, DesiredAccess, ClientId };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_OPENPROCESS,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphOpenProcessToken(
    __in HANDLE KphHandle,
    __in HANDLE ProcessHandle,
    __in ACCESS_MASK DesiredAccess,
    __out PHANDLE TokenHandle
    )
{
    struct
    {
        HANDLE ProcessHandle;
        ACCESS_MASK DesiredAccess;
        PHANDLE TokenHandle;
    } input = { ProcessHandle, DesiredAccess, TokenHandle };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_OPENPROCESSTOKEN,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphOpenProcessJob(
    __in HANDLE KphHandle,
    __in HANDLE ProcessHandle,
    __in ACCESS_MASK DesiredAccess,
    __out PHANDLE JobHandle
    )
{
    struct
    {
        HANDLE ProcessHandle;
        ACCESS_MASK DesiredAccess;
        PHANDLE JobHandle;
    } input = { ProcessHandle, DesiredAccess, JobHandle };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_OPENPROCESSJOB,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphSuspendProcess(
    __in HANDLE KphHandle,
    __in HANDLE ProcessHandle
    )
{
    struct
    {
        HANDLE ProcessHandle;
    } input = { ProcessHandle };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_SUSPENDPROCESS,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphResumeProcess(
    __in HANDLE KphHandle,
    __in HANDLE ProcessHandle
    )
{
    struct
    {
        HANDLE ProcessHandle;
    } input = { ProcessHandle };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_RESUMEPROCESS,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphTerminateProcess(
    __in HANDLE KphHandle,
    __in HANDLE ProcessHandle,
    __in NTSTATUS ExitStatus
    )
{
    NTSTATUS status;
    struct
    {
        HANDLE ProcessHandle;
        NTSTATUS ExitStatus;
    } input = { ProcessHandle, ExitStatus };

    status = KphpDeviceIoControl(
        KphHandle,
        KPH_TERMINATEPROCESS,
        &input,
        sizeof(input)
        );

    // Check if we're trying to terminate the current process, 
    // because kernel-mode can't do it.
    if (status == STATUS_CANT_TERMINATE_SELF)
    {
        RtlExitUserProcess(ExitStatus);
    }

    return status;
}

NTSTATUS KphReadVirtualMemory(
    __in HANDLE KphHandle,
    __in HANDLE ProcessHandle,
    __in PVOID BaseAddress,
    __out_bcount(BufferSize) PVOID Buffer,
    __in SIZE_T BufferSize,
    __out_opt PSIZE_T NumberOfBytesRead
    )
{
    struct
    {
        HANDLE ProcessHandle;
        PVOID BaseAddress;
        PVOID Buffer;
        SIZE_T BufferSize;
        PSIZE_T NumberOfBytesRead;
    } input = { ProcessHandle, BaseAddress, Buffer, BufferSize, NumberOfBytesRead };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_READVIRTUALMEMORY,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphWriteVirtualMemory(
    __in HANDLE KphHandle,
    __in HANDLE ProcessHandle,
    __in_opt PVOID BaseAddress,
    __in_bcount(BufferSize) PVOID Buffer,
    __in SIZE_T BufferSize,
    __out_opt PSIZE_T NumberOfBytesWritten
    )
{
    struct
    {
        HANDLE ProcessHandle;
        PVOID BaseAddress;
        PVOID Buffer;
        SIZE_T BufferSize;
        PSIZE_T NumberOfBytesWritten;
    } input = { ProcessHandle, BaseAddress, Buffer, BufferSize, NumberOfBytesWritten };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_WRITEVIRTUALMEMORY,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphReadVirtualMemoryUnsafe(
    __in HANDLE KphHandle,
    __in_opt HANDLE ProcessHandle,
    __in PVOID BaseAddress,
    __out_bcount(BufferSize) PVOID Buffer,
    __in SIZE_T BufferSize,
    __out_opt PSIZE_T NumberOfBytesRead
    )
{
    struct
    {
        HANDLE ProcessHandle;
        PVOID BaseAddress;
        PVOID Buffer;
        SIZE_T BufferSize;
        PSIZE_T NumberOfBytesRead;
    } input = { ProcessHandle, BaseAddress, Buffer, BufferSize, NumberOfBytesRead };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_READVIRTUALMEMORYUNSAFE,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphQueryInformationProcess(
    __in HANDLE KphHandle,
    __in HANDLE ProcessHandle,
    __in KPH_PROCESS_INFORMATION_CLASS ProcessInformationClass,
    __out_bcount(ProcessInformationLength) PVOID ProcessInformation,
    __in ULONG ProcessInformationLength,
    __out_opt PULONG ReturnLength
    )
{
    struct
    {
        HANDLE ProcessHandle;
        KPH_PROCESS_INFORMATION_CLASS ProcessInformationClass;
        PVOID ProcessInformation;
        ULONG ProcessInformationLength;
        PULONG ReturnLength;
    } input = { ProcessHandle, ProcessInformationClass, ProcessInformation, ProcessInformationLength, ReturnLength };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_QUERYINFORMATIONPROCESS,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphSetInformationProcess(
    __in HANDLE KphHandle,
    __in HANDLE ProcessHandle,
    __in KPH_PROCESS_INFORMATION_CLASS ProcessInformationClass,
    __in_bcount(ProcessInformationLength) PVOID ProcessInformation,
    __in ULONG ProcessInformationLength
    )
{
    struct
    {
        HANDLE ProcessHandle;
        KPH_PROCESS_INFORMATION_CLASS ProcessInformationClass;
        PVOID ProcessInformation;
        ULONG ProcessInformationLength;
    } input = { ProcessHandle, ProcessInformationClass, ProcessInformation, ProcessInformationLength };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_SETINFORMATIONPROCESS,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphOpenThread(
    __in HANDLE KphHandle,
    __out PHANDLE ThreadHandle,
    __in ACCESS_MASK DesiredAccess,
    __in PCLIENT_ID ClientId
    )
{
    struct
    {
        PHANDLE ThreadHandle;
        ACCESS_MASK DesiredAccess;
        PCLIENT_ID ClientId;
    } input = { ThreadHandle, DesiredAccess, ClientId };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_OPENTHREAD,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphOpenThreadProcess(
    __in HANDLE KphHandle,
    __in HANDLE ThreadHandle,
    __in ACCESS_MASK DesiredAccess,
    __out PHANDLE ProcessHandle
    )
{
    struct
    {
        HANDLE ThreadHandle;
        ACCESS_MASK DesiredAccess;
        PHANDLE ProcessHandle;
    } input = { ThreadHandle, DesiredAccess, ProcessHandle };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_OPENTHREADPROCESS,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphTerminateThread(
    __in HANDLE KphHandle,
    __in HANDLE ThreadHandle,
    __in NTSTATUS ExitStatus
    )
{
    NTSTATUS status;
    struct
    {
        HANDLE ThreadHandle;
        NTSTATUS ExitStatus;
    } input = { ThreadHandle, ExitStatus };

    status = KphpDeviceIoControl(
        KphHandle,
        KPH_TERMINATETHREAD,
        &input,
        sizeof(input)
        );

    if (status == STATUS_CANT_TERMINATE_SELF)
    {
        RtlExitUserThread(ExitStatus);
    }

    return status;
}

NTSTATUS KphTerminateThreadUnsafe(
    __in HANDLE KphHandle,
    __in HANDLE ThreadHandle,
    __in NTSTATUS ExitStatus
    )
{
    struct
    {
        HANDLE ThreadHandle;
        NTSTATUS ExitStatus;
    } input = { ThreadHandle, ExitStatus };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_TERMINATETHREADUNSAFE,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphGetContextThread(
    __in HANDLE KphHandle,
    __in HANDLE ThreadHandle,
    __inout PCONTEXT ThreadContext
    )
{
    struct
    {
        HANDLE ThreadHandle;
        PCONTEXT ThreadContext;
    } input = { ThreadHandle, ThreadContext };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_GETCONTEXTTHREAD,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphSetContextThread(
    __in HANDLE KphHandle,
    __in HANDLE ThreadHandle,
    __in PCONTEXT ThreadContext
    )
{
    struct
    {
        HANDLE ThreadHandle;
        PCONTEXT ThreadContext;
    } input = { ThreadHandle, ThreadContext };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_SETCONTEXTTHREAD,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphCaptureStackBackTraceThread(
    __in HANDLE KphHandle,
    __in HANDLE ThreadHandle,
    __in ULONG FramesToSkip,
    __in ULONG FramesToCapture,
    __out_ecount(FramesToCapture) PVOID *BackTrace,
    __out_opt PULONG CapturedFrames,
    __out_opt PULONG BackTraceHash
    )
{
    struct
    {
        HANDLE ThreadHandle;
        ULONG FramesToSkip;
        ULONG FramesToCapture;
        PPVOID BackTrace;
        PULONG CapturedFrames;
        PULONG BackTraceHash;
    } input = { ThreadHandle, FramesToSkip, FramesToCapture, BackTrace, CapturedFrames, BackTraceHash };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_CAPTURESTACKBACKTRACETHREAD,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphQueryInformationThread(
    __in HANDLE KphHandle,
    __in HANDLE ThreadHandle,
    __in KPH_THREAD_INFORMATION_CLASS ThreadInformationClass,
    __out_bcount(ProcessInformationLength) PVOID ThreadInformation,
    __in ULONG ThreadInformationLength,
    __out_opt PULONG ReturnLength
    )
{
    struct
    {
        HANDLE ThreadHandle;
        KPH_THREAD_INFORMATION_CLASS ThreadInformationClass;
        PVOID ThreadInformation;
        ULONG ThreadInformationLength;
        PULONG ReturnLength;
    } input = { ThreadHandle, ThreadInformationClass, ThreadInformation, ThreadInformationLength, ReturnLength };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_QUERYINFORMATIONTHREAD,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphSetInformationThread(
    __in HANDLE KphHandle,
    __in HANDLE ThreadHandle,
    __in KPH_THREAD_INFORMATION_CLASS ThreadInformationClass,
    __in_bcount(ThreadInformationLength) PVOID ThreadInformation,
    __in ULONG ThreadInformationLength
    )
{
    struct
    {
        HANDLE ThreadHandle;
        KPH_THREAD_INFORMATION_CLASS ThreadInformationClass;
        PVOID ThreadInformation;
        ULONG ThreadInformationLength;
    } input = { ThreadHandle, ThreadInformationClass, ThreadInformation, ThreadInformationLength };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_SETINFORMATIONTHREAD,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphEnumerateProcessHandles(
    __in HANDLE KphHandle,
    __in HANDLE ProcessHandle,
    __out_bcount(BufferLength) PVOID Buffer,
    __in_opt ULONG BufferLength,
    __out_opt PULONG ReturnLength
    )
{
    struct
    {
        HANDLE ProcessHandle;
        PVOID Buffer;
        ULONG BufferLength;
        PULONG ReturnLength;
    } input = { ProcessHandle, Buffer, BufferLength, ReturnLength };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_ENUMERATEPROCESSHANDLES,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphQueryInformationObject(
    __in HANDLE KphHandle,
    __in HANDLE ProcessHandle,
    __in HANDLE Handle,
    __in KPH_OBJECT_INFORMATION_CLASS ObjectInformationClass,
    __out_bcount(ObjectInformationLength) PVOID ObjectInformation,
    __in ULONG ObjectInformationLength,
    __out_opt PULONG ReturnLength
    )
{
    struct
    {
        HANDLE ProcessHandle;
        HANDLE Handle;
        KPH_OBJECT_INFORMATION_CLASS ObjectInformationClass;
        PVOID ObjectInformation;
        ULONG ObjectInformationLength;
        PULONG ReturnLength;
    } input = { ProcessHandle, Handle, ObjectInformationClass, ObjectInformation, ObjectInformationLength, ReturnLength };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_QUERYINFORMATIONOBJECT,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphSetInformationObject(
    __in HANDLE KphHandle,
    __in HANDLE ProcessHandle,
    __in HANDLE Handle,
    __in KPH_OBJECT_INFORMATION_CLASS ObjectInformationClass,
    __in_bcount(ObjectInformationLength) PVOID ObjectInformation,
    __in ULONG ObjectInformationLength
    )
{
    struct
    {
        HANDLE ProcessHandle;
        HANDLE Handle;
        KPH_OBJECT_INFORMATION_CLASS ObjectInformationClass;
        PVOID ObjectInformation;
        ULONG ObjectInformationLength;
    } input = { ProcessHandle, Handle, ObjectInformationClass, ObjectInformation, ObjectInformationLength };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_SETINFORMATIONOBJECT,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphDuplicateObject(
    __in HANDLE KphHandle,
    __in HANDLE SourceProcessHandle,
    __in HANDLE SourceHandle,
    __in_opt HANDLE TargetProcessHandle,
    __out_opt PHANDLE TargetHandle,
    __in ACCESS_MASK DesiredAccess,
    __in ULONG HandleAttributes,
    __in ULONG Options
    )
{
    NTSTATUS status;
    struct
    {
        HANDLE SourceProcessHandle;
        HANDLE SourceHandle;
        HANDLE TargetProcessHandle;
        PHANDLE TargetHandle;
        ACCESS_MASK DesiredAccess;
        ULONG HandleAttributes;
        ULONG Options;
    } input = { SourceProcessHandle, SourceHandle, TargetProcessHandle, TargetHandle, DesiredAccess, HandleAttributes, Options };

    status = KphpDeviceIoControl(
        KphHandle,
        KPH_DUPLICATEOBJECT,
        &input,
        sizeof(input)
        );

    if (status == STATUS_CANT_TERMINATE_SELF)
    {
        // We tried to close a handle in the current process.
        if (Options & DUPLICATE_CLOSE_SOURCE)
            status = NtClose(SourceHandle);
    }

    return status;
}

NTSTATUS KphOpenDriver(
    __in HANDLE KphHandle,
    __out PHANDLE DriverHandle,
    __in POBJECT_ATTRIBUTES ObjectAttributes
    )
{
    struct
    {
        PHANDLE DriverHandle;
        POBJECT_ATTRIBUTES ObjectAttributes;
    } input = { DriverHandle, ObjectAttributes };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_OPENDRIVER,
        &input,
        sizeof(input)
        );
}

NTSTATUS KphQueryInformationDriver(
    __in HANDLE KphHandle,
    __in HANDLE DriverHandle,
    __in DRIVER_INFORMATION_CLASS DriverInformationClass,
    __out_bcount(DriverInformationLength) PVOID DriverInformation,
    __in ULONG DriverInformationLength,
    __out_opt PULONG ReturnLength
    )
{
    struct
    {     
        HANDLE DriverHandle;
        DRIVER_INFORMATION_CLASS DriverInformationClass;
        PVOID DriverInformation;
        ULONG DriverInformationLength;
        PULONG ReturnLength;
    } input = { DriverHandle, DriverInformationClass, DriverInformation, DriverInformationLength, ReturnLength };

    return KphpDeviceIoControl(
        KphHandle,
        KPH_QUERYINFORMATIONDRIVER,
        &input,
        sizeof(input)
        );
}
