/*
 * Process Hacker Extended Tools -
 *   GPU monitoring
 *
 * Copyright (C) 2011-2015 wj32
 * Copyright (C) 2016 dmex
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

#define INITGUID
#include "exttools.h"
#include <cfgmgr32.h>
#include <devpkey.h>
#include <ntddvdeo.h>
#include "d3dkmt.h"
#include "gpumon.h"

BOOLEAN EtGpuEnabled;
static PPH_LIST EtpGpuAdapterList;
static PH_CALLBACK_REGISTRATION ProcessesUpdatedCallbackRegistration;

ULONG EtGpuTotalNodeCount = 0;
ULONG EtGpuTotalSegmentCount = 0;
ULONG EtGpuNextNodeIndex = 0;

PH_UINT64_DELTA EtClockTotalRunningTimeDelta;
LARGE_INTEGER EtClockTotalRunningTimeFrequency;

FLOAT EtGpuNodeUsage = 0;
PH_CIRCULAR_BUFFER_FLOAT EtGpuNodeHistory;
PH_CIRCULAR_BUFFER_ULONG EtMaxGpuNodeHistory; // ID of max. GPU usage process
PH_CIRCULAR_BUFFER_FLOAT EtMaxGpuNodeUsageHistory;

PPH_UINT64_DELTA EtGpuNodesTotalRunningTimeDelta;
PPH_CIRCULAR_BUFFER_FLOAT EtGpuNodesHistory;

ULONG64 EtGpuDedicatedLimit = 0;
ULONG64 EtGpuDedicatedUsage = 0;
ULONG64 EtGpuSharedLimit = 0;
ULONG64 EtGpuSharedUsage = 0;
PH_CIRCULAR_BUFFER_ULONG EtGpuDedicatedHistory;
PH_CIRCULAR_BUFFER_ULONG EtGpuSharedHistory;

VOID EtGpuMonitorInitialization(
    VOID
    )
{
    if (PhGetIntegerSetting(SETTING_NAME_ENABLE_GPU_MONITOR))
    {
        EtpGpuAdapterList = PhCreateList(4);

        if (EtpInitializeD3DStatistics())
            EtGpuEnabled = TRUE;
    }

    if (EtGpuEnabled)
    {
        ULONG sampleCount;
        ULONG i;

        sampleCount = PhGetIntegerSetting(L"SampleCount");
        PhInitializeCircularBuffer_FLOAT(&EtGpuNodeHistory, sampleCount);
        PhInitializeCircularBuffer_ULONG(&EtMaxGpuNodeHistory, sampleCount);
        PhInitializeCircularBuffer_FLOAT(&EtMaxGpuNodeUsageHistory, sampleCount);
        PhInitializeCircularBuffer_ULONG(&EtGpuDedicatedHistory, sampleCount);
        PhInitializeCircularBuffer_ULONG(&EtGpuSharedHistory, sampleCount);

        PhInitializeDelta(&EtClockTotalRunningTimeDelta);

        EtGpuNodesTotalRunningTimeDelta = PhAllocate(sizeof(PH_UINT64_DELTA) * EtGpuTotalNodeCount);
        memset(EtGpuNodesTotalRunningTimeDelta, 0, sizeof(PH_UINT64_DELTA) * EtGpuTotalNodeCount);
        EtGpuNodesHistory = PhAllocate(sizeof(PH_CIRCULAR_BUFFER_FLOAT) * EtGpuTotalNodeCount);

        for (i = 0; i < EtGpuTotalNodeCount; i++)
        {
            PhInitializeCircularBuffer_FLOAT(&EtGpuNodesHistory[i], sampleCount);
        }

        PhRegisterCallback(
            &PhProcessesUpdatedEvent,
            EtGpuProcessesUpdatedCallback,
            NULL,
            &ProcessesUpdatedCallbackRegistration
            );
    }
}

NTSTATUS EtpQueryAdapterInformation(
    _In_ D3DKMT_HANDLE AdapterHandle,
    _In_ KMTQUERYADAPTERINFOTYPE InformationClass, 
    _Out_writes_bytes_opt_(InformationLength) PVOID Information, 
    _In_ UINT32 InformationLength
    )
{
    D3DKMT_QUERYADAPTERINFO queryAdapterInfo;

    memset(&queryAdapterInfo, 0, sizeof(D3DKMT_QUERYADAPTERINFO));

    queryAdapterInfo.AdapterHandle = AdapterHandle;
    queryAdapterInfo.Type = InformationClass;
    queryAdapterInfo.PrivateDriverData = Information;
    queryAdapterInfo.PrivateDriverDataSize = InformationLength;

    return D3DKMTQueryAdapterInfo(&queryAdapterInfo);
}

BOOLEAN EtpCloseAdapterHandle(
    _In_ D3DKMT_HANDLE AdapterHandle
    )
{
    D3DKMT_CLOSEADAPTER closeAdapter;

    memset(&closeAdapter, 0, sizeof(D3DKMT_CLOSEADAPTER));
    closeAdapter.AdapterHandle = AdapterHandle;

    return NT_SUCCESS(D3DKMTCloseAdapter(&closeAdapter));
}

BOOLEAN EtpIsGpuSoftwareDevice(
    _In_ D3DKMT_HANDLE AdapterHandle
    )
{
    D3DKMT_ADAPTERTYPE adapterType;

    memset(&adapterType, 0, sizeof(D3DKMT_ADAPTERTYPE));

    if (NT_SUCCESS(EtpQueryAdapterInformation(
        AdapterHandle,
        KMTQAITYPE_ADAPTERTYPE,
        &adapterType,
        sizeof(D3DKMT_ADAPTERTYPE)
        )))
    {
        if (adapterType.SoftwareDevice)
        {
            return TRUE;
        }
    }

    return FALSE;
}

PPH_STRING EtpGetNodeEngineTypeString(
    _In_ D3DKMT_NODEMETADATA NodeMetaData
    )
{
    switch (NodeMetaData.EngineType)
    {
    case DXGK_ENGINE_TYPE_OTHER:
        return PhCreateString(NodeMetaData.FriendlyName);
    case DXGK_ENGINE_TYPE_3D:
        return PhCreateString(L"3D");
    case DXGK_ENGINE_TYPE_VIDEO_DECODE:
        return PhCreateString(L"Video Decode");
    case DXGK_ENGINE_TYPE_VIDEO_ENCODE:
        return PhCreateString(L"Video Encode");
    case DXGK_ENGINE_TYPE_VIDEO_PROCESSING:
        return PhCreateString(L"Video Processing");
    case DXGK_ENGINE_TYPE_SCENE_ASSEMBLY:
        return PhCreateString(L"Scene Assembly");
    case DXGK_ENGINE_TYPE_COPY:
        return PhCreateString(L"Copy");
    case DXGK_ENGINE_TYPE_OVERLAY:
        return PhCreateString(L"Overlay");
    case DXGK_ENGINE_TYPE_CRYPTO:
        return PhCreateString(L"Crypto");
    }

    return PhFormatString(L"ERROR (%lu)", NodeMetaData.EngineType);
}

PPH_STRING EtpQueryDeviceProperty(
    _In_ DEVINST DeviceHandle,
    _In_ CONST DEVPROPKEY *DeviceProperty
    )
{
    CONFIGRET result;
    PBYTE buffer;
    ULONG bufferSize;
    DEVPROPTYPE propertyType;

    bufferSize = 0x80;
    buffer = PhAllocate(bufferSize);
    propertyType = DEVPROP_TYPE_EMPTY;

    if ((result = CM_Get_DevNode_Property(
        DeviceHandle,
        DeviceProperty,
        &propertyType,
        buffer,
        &bufferSize,
        0
        )) != CR_SUCCESS)
    {
        PhFree(buffer);
        buffer = PhAllocate(bufferSize);

        result = CM_Get_DevNode_Property(
            DeviceHandle,
            DeviceProperty,
            &propertyType,
            buffer,
            &bufferSize,
            0
            );
    }

    if (result != CR_SUCCESS)
    {
        PhFree(buffer);
        return NULL;
    }

    switch (propertyType)
    {
    case DEVPROP_TYPE_STRING:
        {
            PPH_STRING string;

            string = PhCreateStringEx((PWCHAR)buffer, bufferSize);
            PhFree(buffer);

            return string;
        }
        break;
    case DEVPROP_TYPE_FILETIME:
        {
            PPH_STRING string;
            FILETIME newFileTime;
            SYSTEMTIME systemTime;

            FileTimeToLocalFileTime((PFILETIME)buffer, &newFileTime);
            FileTimeToSystemTime(&newFileTime, &systemTime);

            string = PhFormatDateTime(&systemTime);
            PhFree(buffer);

            return string;
        }
        break;
    }

    return NULL;
}

PPH_STRING EtpQueryDeviceRegistryProperty(
    _In_ DEVINST DeviceHandle,
    _In_ ULONG DeviceProperty
    )
{
    CONFIGRET result;
    PPH_STRING string;
    ULONG bufferSize;
    DEVPROPTYPE devicePropertyType;

    bufferSize = 0x80;
    string = PhCreateStringEx(NULL, bufferSize);

    if ((result = CM_Get_DevNode_Registry_Property(
        DeviceHandle,
        DeviceProperty,
        &devicePropertyType,
        (PBYTE)string->Buffer,
        &bufferSize,
        0
        )) != CR_SUCCESS)
    {
        PhDereferenceObject(string);
        string = PhCreateStringEx(NULL, bufferSize);

        result = CM_Get_DevNode_Registry_Property(
            DeviceHandle,
            DeviceProperty,
            &devicePropertyType,
            (PBYTE)string->Buffer,
            &bufferSize,
            0
            );
    }

    if (result != CR_SUCCESS)
    {
        PhDereferenceObject(string);
        return NULL;
    }

    PhTrimToNullTerminatorString(string);

    return string;
}

ULONG64 EtpQueryGpuInstalledMemory( // EtpQueryGpuHardwareReservedMemory // HACK
    _In_ DEVINST DeviceHandle
    )
{
    ULONG64 installedMemory = ULLONG_MAX;
    HKEY keyHandle;

    if (CM_Open_DevInst_Key(
        DeviceHandle,
        KEY_READ,
        0,
        RegDisposition_OpenExisting,
        &keyHandle,
        CM_REGISTRY_SOFTWARE
        ) == CR_SUCCESS)
    {
        installedMemory = PhQueryRegistryUlong64(keyHandle, L"HardwareInformation.qwMemorySize");

        if (installedMemory == ULLONG_MAX)
            installedMemory = PhQueryRegistryUlong(keyHandle, L"HardwareInformation.MemorySize");

        NtClose(keyHandle);
    }

    return installedMemory;
}

BOOLEAN EtpQueryDeviceProperties(
    _In_ PWSTR DeviceInterface,
    _Out_ PPH_STRING *Description,
    _Out_ PPH_STRING *DriverDate,
    _Out_ PPH_STRING *DriverVersion,
    _Out_ PPH_STRING *LocationInfo,
    _Out_ PPH_STRING *InstalledMemory
    )
{
    DEVPROPTYPE devicePropertyType;
    DEVINST deviceInstanceHandle;
    ULONG deviceInstanceIdLength = MAX_DEVICE_ID_LEN;
    WCHAR deviceInstanceId[MAX_DEVICE_ID_LEN];

    if (CM_Get_Device_Interface_Property(
        DeviceInterface,
        &DEVPKEY_Device_InstanceId,
        &devicePropertyType,
        (PBYTE)deviceInstanceId,
        &deviceInstanceIdLength,
        0
        ) != CR_SUCCESS)
    {
        return FALSE;
    }

    if (CM_Locate_DevNode(
        &deviceInstanceHandle,
        deviceInstanceId,
        CM_LOCATE_DEVNODE_NORMAL
        ) != CR_SUCCESS)
    {
        return FALSE;
    }

    if (Description)
        *Description = EtpQueryDeviceProperty(deviceInstanceHandle, &DEVPKEY_Device_DeviceDesc); // DEVPKEY_NAME
    if (DriverDate)
        *DriverDate = EtpQueryDeviceProperty(deviceInstanceHandle, &DEVPKEY_Device_DriverDate);
    if (DriverVersion)
        *DriverVersion = EtpQueryDeviceProperty(deviceInstanceHandle, &DEVPKEY_Device_DriverVersion);
    if (LocationInfo)
        *LocationInfo = EtpQueryDeviceProperty(deviceInstanceHandle, &DEVPKEY_Device_LocationInfo);

    //EtpQueryDeviceProperty(deviceInstanceHandle, &DEVPKEY_Device_Manufacturer);
    EtpQueryGpuInstalledMemory(deviceInstanceHandle);

    return TRUE;
}

PETP_GPU_ADAPTER EtpAddDisplayAdapter(
    _In_ D3DKMT_HANDLE AdapterHandle,
    _In_ LUID AdapterLuid, 
    _In_ ULONG NumberOfSegments, 
    _In_ ULONG NumberOfNodes, 
    _In_ PWSTR DeviceInterface
    )
{
    PETP_GPU_ADAPTER adapter;
    SIZE_T sizeNeeded;

    sizeNeeded = FIELD_OFFSET(ETP_GPU_ADAPTER, ApertureBitMapBuffer);
    sizeNeeded += BYTES_NEEDED_FOR_BITS(NumberOfSegments);

    adapter = PhAllocate(sizeNeeded);
    memset(adapter, 0, sizeNeeded);

    adapter->AdapterLuid = AdapterLuid;
    adapter->NodeCount = NumberOfNodes;
    adapter->SegmentCount = NumberOfSegments;
    RtlInitializeBitMap(&adapter->ApertureBitMap, adapter->ApertureBitMapBuffer, NumberOfSegments);

    {
        PPH_STRING description;
        PPH_STRING driverDate;
        PPH_STRING driverVersion;
        PPH_STRING locationInfo;
        PPH_STRING installedMemory;

        if (EtpQueryDeviceProperties(DeviceInterface, &description, &driverDate, &driverVersion, &locationInfo, &installedMemory))
        {
            adapter->Description = description;
        }
    }

    if (WindowsVersion >= WINDOWS_10)
    {
        adapter->NodeNameList = PhCreateList(adapter->NodeCount);

        for (ULONG i = 0; i < adapter->NodeCount; i++)
        {
            D3DKMT_NODEMETADATA nodeMetaData;

            memset(&nodeMetaData, 0, sizeof(D3DKMT_NODEMETADATA));
            nodeMetaData.NodeOrdinal = i;

            if (NT_SUCCESS(EtpQueryAdapterInformation(
                AdapterHandle,
                KMTQAITYPE_NODEMETADATA,
                &nodeMetaData,
                sizeof(D3DKMT_NODEMETADATA)
                )))
            {
                PhAddItemList(adapter->NodeNameList, EtpGetNodeEngineTypeString(nodeMetaData));
            }
            else
            {
                PhAddItemList(adapter->NodeNameList, PhReferenceEmptyString());
            }
        }
    }

    PhAddItemList(EtpGpuAdapterList, adapter);

    return adapter;
}

BOOLEAN EtpInitializeD3DStatistics(
    VOID
    )
{
    PPH_LIST deviceAdapterList;
    PWSTR deviceInterfaceList;
    ULONG deviceInterfaceListLength = 0;
    PWSTR deviceInterface;
    D3DKMT_OPENADAPTERFROMDEVICENAME openAdapterFromDeviceName;
    D3DKMT_QUERYSTATISTICS queryStatistics;

    if (CM_Get_Device_Interface_List_Size(
        &deviceInterfaceListLength,
        (PGUID)&GUID_DISPLAY_DEVICE_ARRIVAL,
        NULL,
        CM_GET_DEVICE_INTERFACE_LIST_PRESENT
        ) != CR_SUCCESS)
    {
        return FALSE;
    }

    deviceInterfaceList = PhAllocate(deviceInterfaceListLength * sizeof(WCHAR));
    memset(deviceInterfaceList, 0, deviceInterfaceListLength * sizeof(WCHAR));

    if (CM_Get_Device_Interface_List(
        (PGUID)&GUID_DISPLAY_DEVICE_ARRIVAL,
        NULL,
        deviceInterfaceList,
        deviceInterfaceListLength,
        CM_GET_DEVICE_INTERFACE_LIST_PRESENT
        ) != CR_SUCCESS)
    {
        PhFree(deviceInterfaceList);
        return FALSE;
    }

    deviceAdapterList = PhCreateList(10);

    for (deviceInterface = deviceInterfaceList; *deviceInterface; deviceInterface += PhCountStringZ(deviceInterface) + 1)
    {
        PhAddItemList(deviceAdapterList, deviceInterface);
    }

    for (ULONG i = 0; i < deviceAdapterList->Count; i++)
    {
        memset(&openAdapterFromDeviceName, 0, sizeof(D3DKMT_OPENADAPTERFROMDEVICENAME));
        openAdapterFromDeviceName.DeviceName = deviceAdapterList->Items[i];

        if (!NT_SUCCESS(D3DKMTOpenAdapterFromDeviceName(&openAdapterFromDeviceName)))
            continue;

        if (WindowsVersion >= WINDOWS_8)
        {
            if (EtpIsGpuSoftwareDevice(openAdapterFromDeviceName.AdapterHandle))
            {
                EtpCloseAdapterHandle(openAdapterFromDeviceName.AdapterHandle);
                continue;
            }
        }

        if (WindowsVersion >= WINDOWS_10_RS3)
        {
            D3DKMT_SEGMENTSIZEINFO segmentInfo;

            memset(&segmentInfo, 0, sizeof(D3DKMT_SEGMENTSIZEINFO));

            if (NT_SUCCESS(EtpQueryAdapterInformation(
                openAdapterFromDeviceName.AdapterHandle,
                KMTQAITYPE_GETSEGMENTSIZE,
                &segmentInfo,
                sizeof(D3DKMT_SEGMENTSIZEINFO)
                )))
            {
                EtGpuDedicatedLimit += segmentInfo.DedicatedVideoMemorySize;
                EtGpuSharedLimit += segmentInfo.SharedSystemMemorySize;
            }
        }

        //{
        //    D3DKMT_ADAPTER_PERFDATA segmentInfo;
        //
        //    memset(&segmentInfo, 0, sizeof(D3DKMT_ADAPTER_PERFDATA));
        //
        //    if (NT_SUCCESS(EtpQueryAdapterInformation(
        //        openAdapterFromDeviceName.AdapterHandle,
        //        KMTQAITYPE_ADAPTERPERFDATA,
        //        &segmentInfo,
        //        sizeof(D3DKMT_ADAPTER_PERFDATA)
        //        )))
        //    {
        //        //_Out_ ULONGLONG MemoryFrequency; // Clock frequency of the memory in hertz
        //        //_Out_ ULONGLONG MaxMemoryFrequency; // Max clock frequency of the memory while not overclocked, represented in hertz.
        //        //_Out_ ULONGLONG MaxMemoryFrequencyOC; // Clock frequency of the memory while overclocked in hertz.
        //        //_Out_ ULONGLONG MemoryBandwidth; // Amount of memory transferred in bytes
        //        //_Out_ ULONGLONG PCIEBandwidth; // Amount of memory transferred over PCI-E in bytes
        //        //_Out_ ULONG FanRPM; // Fan rpm
        //        //_Out_ ULONG Power; // Power draw of the adapter in tenths of a percentage
        //        //_Out_ ULONG Temperature; // Temperature in deci-Celsius 1 = 0.1C
        //        //_Out_ UCHAR PowerStateOverride; // Overrides dxgkrnls power view of linked adapters.
        //
        //        dprintf("Memory: %lu, MemoryBandwidth: %lu, PCIEBandwidth: %lu, FanRPM: %lu, Power: %lu, Temperature: %lu\n", 
        //            segmentInfo.MemoryFrequency, 
        //            segmentInfo.MemoryBandwidth,
        //            segmentInfo.PCIEBandwidth,
        //            segmentInfo.FanRPM,
        //            segmentInfo.Power * 100 / 1000,
        //            segmentInfo.Temperature * 100 / 1000
        //            );
        //    }
        //}



        memset(&queryStatistics, 0, sizeof(D3DKMT_QUERYSTATISTICS));
        queryStatistics.Type = D3DKMT_QUERYSTATISTICS_ADAPTER;
        queryStatistics.AdapterLuid = openAdapterFromDeviceName.AdapterLuid;

        if (NT_SUCCESS(D3DKMTQueryStatistics(&queryStatistics)))
        {
            PETP_GPU_ADAPTER gpuAdapter;
            ULONG ii;

            gpuAdapter = EtpAddDisplayAdapter(
                openAdapterFromDeviceName.AdapterHandle,
                openAdapterFromDeviceName.AdapterLuid, 
                queryStatistics.QueryResult.AdapterInformation.NbSegments,
                queryStatistics.QueryResult.AdapterInformation.NodeCount,
                openAdapterFromDeviceName.DeviceName
                );

            gpuAdapter->FirstNodeIndex = EtGpuNextNodeIndex;
            EtGpuTotalNodeCount += gpuAdapter->NodeCount;
            EtGpuTotalSegmentCount += gpuAdapter->SegmentCount;
            EtGpuNextNodeIndex += gpuAdapter->NodeCount;

            for (ii = 0; ii < gpuAdapter->SegmentCount; ii++)
            {
                memset(&queryStatistics, 0, sizeof(D3DKMT_QUERYSTATISTICS));
                queryStatistics.Type = D3DKMT_QUERYSTATISTICS_SEGMENT;
                queryStatistics.AdapterLuid = gpuAdapter->AdapterLuid;
                queryStatistics.QuerySegment.SegmentId = ii;

                if (NT_SUCCESS(D3DKMTQueryStatistics(&queryStatistics)))
                {
                    ULONG64 commitLimit;
                    ULONG aperture;

                    if (WindowsVersion >= WINDOWS_8)
                    {
                        commitLimit = queryStatistics.QueryResult.SegmentInformation.CommitLimit;
                        aperture = queryStatistics.QueryResult.SegmentInformation.Aperture;
                    }
                    else
                    {
                        commitLimit = queryStatistics.QueryResult.SegmentInformationV1.CommitLimit;
                        aperture = queryStatistics.QueryResult.SegmentInformationV1.Aperture;
                    }

                    if (WindowsVersion < WINDOWS_10_RS3)
                    {
                        if (aperture)
                            EtGpuSharedLimit += commitLimit;
                        else
                            EtGpuDedicatedLimit += commitLimit;
                    }

                    if (aperture)
                        RtlSetBits(&gpuAdapter->ApertureBitMap, ii, 1);
                }
            }
        }

        EtpCloseAdapterHandle(openAdapterFromDeviceName.AdapterHandle);
    }

    PhDereferenceObject(deviceAdapterList);
    PhFree(deviceInterfaceList);

    if (EtGpuTotalNodeCount == 0)
        return FALSE;

    return TRUE;
}

PETP_GPU_ADAPTER EtpAllocateGpuAdapter(
    _In_ ULONG NumberOfSegments
    )
{
    PETP_GPU_ADAPTER adapter;
    SIZE_T sizeNeeded;

    sizeNeeded = FIELD_OFFSET(ETP_GPU_ADAPTER, ApertureBitMapBuffer);
    sizeNeeded += BYTES_NEEDED_FOR_BITS(NumberOfSegments);

    adapter = PhAllocate(sizeNeeded);
    memset(adapter, 0, sizeNeeded);

    return adapter;
}

VOID EtpUpdateProcessSegmentInformation(
    _In_ PET_PROCESS_BLOCK Block
    )
{
    ULONG i;
    ULONG j;
    PETP_GPU_ADAPTER gpuAdapter;
    D3DKMT_QUERYSTATISTICS queryStatistics;
    ULONG64 dedicatedUsage;
    ULONG64 sharedUsage;

    if (!Block->ProcessItem->QueryHandle)
        return;

    dedicatedUsage = 0;
    sharedUsage = 0;

    for (i = 0; i < EtpGpuAdapterList->Count; i++)
    {
        gpuAdapter = EtpGpuAdapterList->Items[i];

        for (j = 0; j < gpuAdapter->SegmentCount; j++)
        {
            memset(&queryStatistics, 0, sizeof(D3DKMT_QUERYSTATISTICS));
            queryStatistics.Type = D3DKMT_QUERYSTATISTICS_PROCESS_SEGMENT;
            queryStatistics.AdapterLuid = gpuAdapter->AdapterLuid;
            queryStatistics.ProcessHandle = Block->ProcessItem->QueryHandle;
            queryStatistics.QueryProcessSegment.SegmentId = j;

            if (NT_SUCCESS(D3DKMTQueryStatistics(&queryStatistics)))
            {
                ULONG64 bytesCommitted;

                if (WindowsVersion >= WINDOWS_8)
                    bytesCommitted = queryStatistics.QueryResult.ProcessSegmentInformation.BytesCommitted;
                else
                    bytesCommitted = queryStatistics.QueryResult.ProcessSegmentInformation.BytesCommitted;

                if (RtlCheckBit(&gpuAdapter->ApertureBitMap, j))
                    sharedUsage += bytesCommitted;
                else
                    dedicatedUsage += bytesCommitted;
            }
        }
    }

    Block->GpuDedicatedUsage = dedicatedUsage;
    Block->GpuSharedUsage = sharedUsage;
}

VOID EtpUpdateSystemSegmentInformation(
    VOID
    )
{
    PETP_GPU_ADAPTER gpuAdapter;
    D3DKMT_QUERYSTATISTICS queryStatistics;
    ULONG64 dedicatedUsage;
    ULONG64 sharedUsage;

    dedicatedUsage = 0;
    sharedUsage = 0;

    for (ULONG i = 0; i < EtpGpuAdapterList->Count; i++)
    {
        gpuAdapter = EtpGpuAdapterList->Items[i];

        for (ULONG j = 0; j < gpuAdapter->SegmentCount; j++)
        {
            memset(&queryStatistics, 0, sizeof(D3DKMT_QUERYSTATISTICS));
            queryStatistics.Type = D3DKMT_QUERYSTATISTICS_SEGMENT;
            queryStatistics.AdapterLuid = gpuAdapter->AdapterLuid;
            queryStatistics.QuerySegment.SegmentId = j;

            if (NT_SUCCESS(D3DKMTQueryStatistics(&queryStatistics)))
            {
                ULONG64 bytesCommitted;

                if (WindowsVersion >= WINDOWS_8)
                    bytesCommitted = queryStatistics.QueryResult.SegmentInformation.BytesResident;
                else
                    bytesCommitted = queryStatistics.QueryResult.SegmentInformationV1.BytesResident;

                if (RtlCheckBit(&gpuAdapter->ApertureBitMap, j))
                    sharedUsage += bytesCommitted;
                else
                    dedicatedUsage += bytesCommitted;
            }
        }
    }

    EtGpuDedicatedUsage = dedicatedUsage;
    EtGpuSharedUsage = sharedUsage;
}

VOID EtpUpdateProcessNodeInformation(
    _In_ PET_PROCESS_BLOCK Block
    )
{
    PETP_GPU_ADAPTER gpuAdapter;
    D3DKMT_QUERYSTATISTICS queryStatistics;
    ULONG64 totalRunningTime;

    if (!Block->ProcessItem->QueryHandle)
        return;

    totalRunningTime = 0;

    for (ULONG i = 0; i < EtpGpuAdapterList->Count; i++)
    {
        gpuAdapter = EtpGpuAdapterList->Items[i];

        for (ULONG j = 0; j < gpuAdapter->NodeCount; j++)
        {
            memset(&queryStatistics, 0, sizeof(D3DKMT_QUERYSTATISTICS));
            queryStatistics.Type = D3DKMT_QUERYSTATISTICS_PROCESS_NODE;
            queryStatistics.AdapterLuid = gpuAdapter->AdapterLuid;
            queryStatistics.ProcessHandle = Block->ProcessItem->QueryHandle;
            queryStatistics.QueryProcessNode.NodeId = j;

            if (NT_SUCCESS(D3DKMTQueryStatistics(&queryStatistics)))
            {
                //ULONG64 runningTime;
                //runningTime = queryStatistics.QueryResult.ProcessNodeInformation.RunningTime.QuadPart;
                //PhUpdateDelta(&Block->GpuTotalRunningTimeDelta[j], runningTime);

                totalRunningTime += queryStatistics.QueryResult.ProcessNodeInformation.RunningTime.QuadPart;
            }
        }
    }

    PhUpdateDelta(&Block->GpuRunningTimeDelta, totalRunningTime);
}

VOID EtpUpdateSystemNodeInformation(
    VOID
    )
{
    PETP_GPU_ADAPTER gpuAdapter;
    D3DKMT_QUERYSTATISTICS queryStatistics;
    LARGE_INTEGER performanceCounter;

    for (ULONG i = 0; i < EtpGpuAdapterList->Count; i++)
    {
        gpuAdapter = EtpGpuAdapterList->Items[i];

        for (ULONG j = 0; j < gpuAdapter->NodeCount; j++)
        {
            memset(&queryStatistics, 0, sizeof(D3DKMT_QUERYSTATISTICS));
            queryStatistics.Type = D3DKMT_QUERYSTATISTICS_NODE;
            queryStatistics.AdapterLuid = gpuAdapter->AdapterLuid;
            queryStatistics.QueryNode.NodeId = j;

            if (NT_SUCCESS(D3DKMTQueryStatistics(&queryStatistics)))
            {
                ULONG64 runningTime;
                //ULONG64 systemRunningTime;

                runningTime = queryStatistics.QueryResult.NodeInformation.GlobalInformation.RunningTime.QuadPart;
                //systemRunningTime = queryStatistics.QueryResult.NodeInformation.SystemInformation.RunningTime.QuadPart;

                PhUpdateDelta(&EtGpuNodesTotalRunningTimeDelta[gpuAdapter->FirstNodeIndex + j], runningTime);
            }
        }
    }

    NtQueryPerformanceCounter(&performanceCounter, &EtClockTotalRunningTimeFrequency);
    PhUpdateDelta(&EtClockTotalRunningTimeDelta, performanceCounter.QuadPart);
}

VOID NTAPI EtGpuProcessesUpdatedCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    static ULONG runCount = 0; // MUST keep in sync with runCount in process provider

    DOUBLE elapsedTime; // total GPU node elapsed time in micro-seconds
    FLOAT tempGpuUsage;
    ULONG i;
    PLIST_ENTRY listEntry;
    FLOAT maxNodeValue = 0;
    PET_PROCESS_BLOCK maxNodeBlock = NULL;

    // Update global statistics.
    EtpUpdateSystemSegmentInformation();
    EtpUpdateSystemNodeInformation();

    // Update global gpu usage. 
    tempGpuUsage = 0;
    elapsedTime = (DOUBLE)EtClockTotalRunningTimeDelta.Delta * 10000000 / EtClockTotalRunningTimeFrequency.QuadPart;

    if (elapsedTime != 0)
    {
        for (i = 0; i < EtGpuTotalNodeCount; i++)
        {
            FLOAT usage;

            usage = (FLOAT)(EtGpuNodesTotalRunningTimeDelta[i].Delta / elapsedTime);

            if (usage > tempGpuUsage)
            {
                tempGpuUsage = usage;
            }
        }

        if (tempGpuUsage > 1)
            tempGpuUsage = 1;
    }

    EtGpuNodeUsage = tempGpuUsage;

    // Update per-process statistics.
    // Note: no lock is needed because we only ever modify the list on this same thread.

    listEntry = EtProcessBlockListHead.Flink;

    while (listEntry != &EtProcessBlockListHead)
    {
        PET_PROCESS_BLOCK block;

        block = CONTAINING_RECORD(listEntry, ET_PROCESS_BLOCK, ListEntry);

        EtpUpdateProcessSegmentInformation(block);
        EtpUpdateProcessNodeInformation(block);

        if (elapsedTime != 0)
        {
            block->GpuNodeUsage = (FLOAT)(block->GpuRunningTimeDelta.Delta / elapsedTime);

            // HACK
            if (block->GpuNodeUsage > EtGpuNodeUsage)
                block->GpuNodeUsage = EtGpuNodeUsage;

            //for (i = 0; i < EtGpuTotalNodeCount; i++)
            //{
            //    FLOAT usage = (FLOAT)(block->GpuTotalRunningTimeDelta[i].Delta / elapsedTime);
            //
            //    if (usage > block->GpuNodeUsage)
            //    {
            //        block->GpuNodeUsage = usage;
            //    }
            //}

            if (block->GpuNodeUsage > 1)
                block->GpuNodeUsage = 1;
        }

        if (maxNodeValue < block->GpuNodeUsage)
        {
            maxNodeValue = block->GpuNodeUsage;
            maxNodeBlock = block;
        }

        listEntry = listEntry->Flink;
    }

    // Update history buffers.

    if (runCount != 0)
    {
        PhAddItemCircularBuffer_FLOAT(&EtGpuNodeHistory, EtGpuNodeUsage);
        PhAddItemCircularBuffer_ULONG(&EtGpuDedicatedHistory, (ULONG)(EtGpuDedicatedUsage / PAGE_SIZE));
        PhAddItemCircularBuffer_ULONG(&EtGpuSharedHistory, (ULONG)(EtGpuSharedUsage / PAGE_SIZE));

        if (elapsedTime != 0)
        {
            for (i = 0; i < EtGpuTotalNodeCount; i++)
            {
                FLOAT usage;

                usage = (FLOAT)(EtGpuNodesTotalRunningTimeDelta[i].Delta / elapsedTime);

                if (usage > 1)
                    usage = 1;

                PhAddItemCircularBuffer_FLOAT(&EtGpuNodesHistory[i], usage);
            }
        }
        else
        {
            for (i = 0; i < EtGpuTotalNodeCount; i++)
                PhAddItemCircularBuffer_FLOAT(&EtGpuNodesHistory[i], 0);
        }

        if (maxNodeBlock)
        {
            PhAddItemCircularBuffer_ULONG(&EtMaxGpuNodeHistory, HandleToUlong(maxNodeBlock->ProcessItem->ProcessId));
            PhAddItemCircularBuffer_FLOAT(&EtMaxGpuNodeUsageHistory, maxNodeBlock->GpuNodeUsage);
            PhReferenceProcessRecordForStatistics(maxNodeBlock->ProcessItem->Record);
        }
        else
        {
            PhAddItemCircularBuffer_ULONG(&EtMaxGpuNodeHistory, 0);
            PhAddItemCircularBuffer_FLOAT(&EtMaxGpuNodeUsageHistory, 0);
        }
    }

    runCount++;
}

ULONG EtGetGpuAdapterCount(
    VOID
    )
{
    return EtpGpuAdapterList->Count;
}

ULONG EtGetGpuAdapterIndexFromNodeIndex(
    _In_ ULONG NodeIndex
    )
{
    ULONG i;
    PETP_GPU_ADAPTER gpuAdapter;

    for (i = 0; i < EtpGpuAdapterList->Count; i++)
    {
        gpuAdapter = EtpGpuAdapterList->Items[i];

        if (NodeIndex >= gpuAdapter->FirstNodeIndex && NodeIndex < gpuAdapter->FirstNodeIndex + gpuAdapter->NodeCount)
            return i;
    }

    return -1;
}

PPH_STRING EtGetGpuAdapterNodeDescription(
    _In_ ULONG Index,
    _In_ ULONG NodeIndex
    )
{
    PETP_GPU_ADAPTER gpuAdapter;

    if (Index >= EtpGpuAdapterList->Count)
        return NULL;

    gpuAdapter = EtpGpuAdapterList->Items[Index];

    if (!gpuAdapter->NodeNameList)
        return NULL;

    return gpuAdapter->NodeNameList->Items[NodeIndex - gpuAdapter->FirstNodeIndex];
}

PPH_STRING EtGetGpuAdapterDescription(
    _In_ ULONG Index
    )
{
    PPH_STRING description;

    if (Index >= EtpGpuAdapterList->Count)
        return NULL;

    description = ((PETP_GPU_ADAPTER)EtpGpuAdapterList->Items[Index])->Description;

    if (description)
    {
        PhReferenceObject(description);
        return description;
    }
    else
    {
        return NULL;
    }
}

VOID EtQueryProcessGpuStatistics(
    _In_ HANDLE ProcessHandle,
    _Out_ PET_PROCESS_GPU_STATISTICS Statistics
    )
{
    ULONG i;
    ULONG j;
    PETP_GPU_ADAPTER gpuAdapter;
    D3DKMT_QUERYSTATISTICS queryStatistics;

    memset(Statistics, 0, sizeof(ET_PROCESS_GPU_STATISTICS));

    for (i = 0; i < EtpGpuAdapterList->Count; i++)
    {
        gpuAdapter = EtpGpuAdapterList->Items[i];

        Statistics->SegmentCount += gpuAdapter->SegmentCount;
        Statistics->NodeCount += gpuAdapter->NodeCount;

        for (j = 0; j < gpuAdapter->SegmentCount; j++)
        {
            memset(&queryStatistics, 0, sizeof(D3DKMT_QUERYSTATISTICS));
            queryStatistics.Type = D3DKMT_QUERYSTATISTICS_PROCESS_SEGMENT;
            queryStatistics.AdapterLuid = gpuAdapter->AdapterLuid;
            queryStatistics.ProcessHandle = ProcessHandle;
            queryStatistics.QueryProcessSegment.SegmentId = j;

            if (NT_SUCCESS(D3DKMTQueryStatistics(&queryStatistics)))
            {
                ULONG64 bytesCommitted;

                if (WindowsVersion >= WINDOWS_8)
                    bytesCommitted = queryStatistics.QueryResult.ProcessSegmentInformation.BytesCommitted;
                else
                    bytesCommitted = queryStatistics.QueryResult.ProcessSegmentInformation.BytesCommitted;

                if (RtlCheckBit(&gpuAdapter->ApertureBitMap, j))
                    Statistics->SharedCommitted += bytesCommitted;
                else
                    Statistics->DedicatedCommitted += bytesCommitted;
            }
        }

        for (j = 0; j < gpuAdapter->NodeCount; j++)
        {
            memset(&queryStatistics, 0, sizeof(D3DKMT_QUERYSTATISTICS));
            queryStatistics.Type = D3DKMT_QUERYSTATISTICS_PROCESS_NODE;
            queryStatistics.AdapterLuid = gpuAdapter->AdapterLuid;
            queryStatistics.ProcessHandle = ProcessHandle;
            queryStatistics.QueryProcessNode.NodeId = j;

            if (NT_SUCCESS(D3DKMTQueryStatistics(&queryStatistics)))
            {
                Statistics->RunningTime += queryStatistics.QueryResult.ProcessNodeInformation.RunningTime.QuadPart;
                Statistics->ContextSwitches += queryStatistics.QueryResult.ProcessNodeInformation.ContextSwitch;
            }
        }

        memset(&queryStatistics, 0, sizeof(D3DKMT_QUERYSTATISTICS));
        queryStatistics.Type = D3DKMT_QUERYSTATISTICS_PROCESS;
        queryStatistics.AdapterLuid = gpuAdapter->AdapterLuid;
        queryStatistics.ProcessHandle = ProcessHandle;

        if (NT_SUCCESS(D3DKMTQueryStatistics(&queryStatistics)))
        {
            Statistics->BytesAllocated += queryStatistics.QueryResult.ProcessInformation.SystemMemory.BytesAllocated;
            Statistics->BytesReserved += queryStatistics.QueryResult.ProcessInformation.SystemMemory.BytesReserved;
            Statistics->WriteCombinedBytesAllocated += queryStatistics.QueryResult.ProcessInformation.SystemMemory.WriteCombinedBytesAllocated;
            Statistics->WriteCombinedBytesReserved += queryStatistics.QueryResult.ProcessInformation.SystemMemory.WriteCombinedBytesReserved;
            Statistics->CachedBytesAllocated += queryStatistics.QueryResult.ProcessInformation.SystemMemory.CachedBytesAllocated;
            Statistics->CachedBytesReserved += queryStatistics.QueryResult.ProcessInformation.SystemMemory.CachedBytesReserved;
            Statistics->SectionBytesAllocated += queryStatistics.QueryResult.ProcessInformation.SystemMemory.SectionBytesAllocated;
            Statistics->SectionBytesReserved += queryStatistics.QueryResult.ProcessInformation.SystemMemory.SectionBytesReserved;
        }
    }
}
