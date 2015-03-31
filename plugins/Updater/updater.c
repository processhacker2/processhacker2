/*
 * Process Hacker Update Checker -
 *   Update Window
 *
 * Copyright (C) 2011-2014 dmex
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker. If not, see <http://www.gnu.org/licenses/>.
 */

#include "updater.h"

static HANDLE UpdateDialogThreadHandle = NULL;
static HWND UpdateDialogHandle = NULL;
static PH_EVENT InitializedEvent = PH_EVENT_INIT;

static HBITMAP LoadImageFromResources(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ PCWSTR Name
    )
{
    UINT width = 0;
    UINT height = 0;
    UINT frameCount = 0;
    BOOLEAN isSuccess = FALSE;
    ULONG resourceLength = 0;
    HGLOBAL resourceHandle = NULL;
    HRSRC resourceHandleSource = NULL;
    WICInProcPointer resourceBuffer = NULL;

    BITMAPINFO bitmapInfo = { 0 };
    HBITMAP bitmapHandle = NULL;
    PBYTE bitmapBuffer = NULL;

    IWICStream* wicStream = NULL;
    IWICBitmapSource* wicBitmapSource = NULL;
    IWICBitmapDecoder* wicDecoder = NULL;
    IWICBitmapFrameDecode* wicFrame = NULL;
    IWICImagingFactory* wicFactory = NULL;
    IWICBitmapScaler* wicScaler = NULL;
    WICPixelFormatGUID pixelFormat;

    WICRect rect = { 0, 0, Width, Height };

    __try
    {
        // Create the ImagingFactory
        if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory1, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, &wicFactory)))
            __leave;

        // Find the resource
        if ((resourceHandleSource = FindResource(PluginInstance->DllBase, Name, L"PNG")) == NULL)
            __leave;

        // Get the resource length
        resourceLength = SizeofResource(PluginInstance->DllBase, resourceHandleSource);

        // Load the resource
        if ((resourceHandle = LoadResource(PluginInstance->DllBase, resourceHandleSource)) == NULL)
            __leave;

        if ((resourceBuffer = (WICInProcPointer)LockResource(resourceHandle)) == NULL)
            __leave;

        // Create the Stream
        if (FAILED(IWICImagingFactory_CreateStream(wicFactory, &wicStream)))
            __leave;

        // Initialize the Stream from Memory
        if (FAILED(IWICStream_InitializeFromMemory(wicStream, resourceBuffer, resourceLength)))
            __leave;

        if (FAILED(IWICImagingFactory_CreateDecoder(wicFactory, &GUID_ContainerFormatPng, NULL, &wicDecoder)))
            __leave;

        if (FAILED(IWICBitmapDecoder_Initialize(wicDecoder, (IStream*)wicStream, WICDecodeMetadataCacheOnDemand)))
            __leave;

        // Get the Frame count
        if (FAILED(IWICBitmapDecoder_GetFrameCount(wicDecoder, &frameCount)) || frameCount < 1)
            __leave;

        // Get the Frame
        if (FAILED(IWICBitmapDecoder_GetFrame(wicDecoder, 0, &wicFrame)))
            __leave;

        // Get the WicFrame image format
        if (SUCCEEDED(IWICBitmapFrameDecode_GetPixelFormat(wicFrame, &pixelFormat)))
        {
            // Check if the image format is supported:
            if (IsEqualGUID(&pixelFormat, &GUID_WICPixelFormat32bppBGRA))
            {
                wicBitmapSource = (IWICBitmapSource*)wicFrame;
            }
            else
            {
                // Convert the image to the correct format:
                if (FAILED(WICConvertBitmapSource(&GUID_WICPixelFormat32bppBGRA, (IWICBitmapSource*)wicFrame, &wicBitmapSource)))
                    __leave;

                IWICBitmapFrameDecode_Release(wicFrame);
            }
        }

        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = rect.Width;
        bitmapInfo.bmiHeader.biHeight = -((LONG)rect.Height);
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        HDC hdc = CreateCompatibleDC(NULL);
        bitmapHandle = CreateDIBSection(hdc, &bitmapInfo, DIB_RGB_COLORS, (PVOID*)&bitmapBuffer, NULL, 0);
        ReleaseDC(NULL, hdc);

        // Check if it's the same rect as the requested size.
        //if (width != rect.Width || height != rect.Height)
        if (FAILED(IWICImagingFactory_CreateBitmapScaler(wicFactory, &wicScaler)))
            __leave;
        if (FAILED(IWICBitmapScaler_Initialize(wicScaler, wicBitmapSource, rect.Width, rect.Height, WICBitmapInterpolationModeFant)))
            __leave;
        if (FAILED(IWICBitmapScaler_CopyPixels(wicScaler, &rect, rect.Width * 4, rect.Width * rect.Height * 4, bitmapBuffer)))
            __leave;

        isSuccess = TRUE;
    }
    __finally
    {
        if (wicScaler)
        {
            IWICBitmapScaler_Release(wicScaler);
        }

        if (wicBitmapSource)
        {
            IWICBitmapSource_Release(wicBitmapSource);
        }

        if (wicStream)
        {
            IWICStream_Release(wicStream);
        }

        if (wicDecoder)
        {
            IWICBitmapDecoder_Release(wicDecoder);
        }

        if (wicFactory)
        {
            IWICImagingFactory_Release(wicFactory);
        }

        if (resourceHandle)
        {
            FreeResource(resourceHandle);
        }
    }

    return bitmapHandle;
}

static mxml_type_t QueryXmlDataCallback(
    _In_ mxml_node_t *node
    )
{
    return MXML_OPAQUE;
}

static BOOLEAN ParseVersionString(
    _Inout_ PPH_UPDATER_CONTEXT Context
    )
{
    PH_STRINGREF sr, majorPart, minorPart, revisionPart;
    ULONG64 majorInteger = 0, minorInteger = 0, revisionInteger = 0;

    PhInitializeStringRef(&sr, Context->Version->Buffer);
    PhInitializeStringRef(&revisionPart, Context->RevVersion->Buffer);

    if (PhSplitStringRefAtChar(&sr, '.', &majorPart, &minorPart))
    {
        PhStringToInteger64(&majorPart, 10, &majorInteger);
        PhStringToInteger64(&minorPart, 10, &minorInteger);

        PhStringToInteger64(&revisionPart, 10, &revisionInteger);

        Context->MajorVersion = (ULONG)majorInteger;
        Context->MinorVersion = (ULONG)minorInteger;
        Context->RevisionVersion = (ULONG)revisionInteger;

        return TRUE;
    }

    return FALSE;
}

static BOOLEAN ReadRequestString(
    _In_ HINTERNET Handle,
    _Out_ _Deref_post_z_cap_(*DataLength) PSTR *Data,
    _Out_ ULONG *DataLength
    )
{
    BYTE buffer[PAGE_SIZE];
    PSTR data;
    ULONG allocatedLength;
    ULONG dataLength;
    ULONG returnLength;

    allocatedLength = sizeof(buffer);
    data = (PSTR)PhAllocate(allocatedLength);
    dataLength = 0;

    // Zero the buffer
    memset(buffer, 0, PAGE_SIZE);

    while (WinHttpReadData(Handle, buffer, PAGE_SIZE, &returnLength))
    {
        if (returnLength == 0)
            break;

        if (allocatedLength < dataLength + returnLength)
        {
            allocatedLength *= 2;
            data = (PSTR)PhReAllocate(data, allocatedLength);
        }

        // Copy the returned buffer into our pointer
        memcpy(data + dataLength, buffer, returnLength);
        // Zero the returned buffer for the next loop
        //memset(buffer, 0, returnLength);

        dataLength += returnLength;
    }

    if (allocatedLength < dataLength + 1)
    {
        allocatedLength++;
        data = (PSTR)PhReAllocate(data, allocatedLength);
    }

    // Ensure that the buffer is null-terminated.
    data[dataLength] = 0;

    *DataLength = dataLength;
    *Data = data;

    return TRUE;
}

static PPH_UPDATER_CONTEXT CreateUpdateContext(
    VOID
    )
{
    PPH_UPDATER_CONTEXT context = (PPH_UPDATER_CONTEXT)PhAllocate(
        sizeof(PH_UPDATER_CONTEXT)
        );

    memset(context, 0, sizeof(PH_UPDATER_CONTEXT));

    return context;
}

static VOID FreeUpdateContext(
    _In_ _Post_invalid_ PPH_UPDATER_CONTEXT Context
    )
{
    if (!Context)
        return;

    Context->HaveData = FALSE;
    Context->UpdaterState = PhUpdateMaximum;

    Context->MinorVersion = 0;
    Context->MajorVersion = 0;
    Context->RevisionVersion = 0;
    Context->CurrentMinorVersion = 0;
    Context->CurrentMajorVersion = 0;
    Context->CurrentRevisionVersion = 0;

    PhSwapReference(&Context->UserAgent, NULL);
    PhSwapReference(&Context->Version, NULL);
    PhSwapReference(&Context->RevVersion, NULL);
    PhSwapReference(&Context->RelDate, NULL);
    PhSwapReference(&Context->Size, NULL);
    PhSwapReference(&Context->Hash, NULL);
    PhSwapReference(&Context->ReleaseNotesUrl, NULL);
    PhSwapReference(&Context->SetupFilePath, NULL);
    PhSwapReference(&Context->SetupFileDownloadUrl, NULL);

    if (Context->IconHandle)
    {
        DestroyIcon(Context->IconHandle);
        Context->IconHandle = NULL;
    }

    if (Context->FontHandle)
    {
        DeleteObject(Context->FontHandle);
        Context->FontHandle = NULL;
    }

    if (Context->SourceforgeBitmap)
    {
        DeleteObject(Context->SourceforgeBitmap);
        Context->SourceforgeBitmap = NULL;
    }

    PhFree(Context);
}

static BOOLEAN QueryUpdateData(
    _Inout_ PPH_UPDATER_CONTEXT Context
    )
{
    BOOLEAN isSuccess = FALSE;
    HINTERNET httpSessionHandle = NULL;
    HINTERNET httpConnectionHandle = NULL;
    HINTERNET httpRequestHandle = NULL;
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG proxyConfig = { 0 };
    mxml_node_t* xmlNode = NULL;
    ULONG xmlStringBufferLength = 0;
    PSTR xmlStringBuffer = NULL;

    // Get the current Process Hacker version
    PhGetPhVersionNumbers(
        &Context->CurrentMajorVersion,
        &Context->CurrentMinorVersion,
        NULL,
        &Context->CurrentRevisionVersion
        );

    __try
    {
        // Create a user agent string.
        Context->UserAgent = PhFormatString(
            L"PH_%lu.%lu_%lu",
            Context->CurrentMajorVersion,
            Context->CurrentMinorVersion,
            Context->CurrentRevisionVersion
            );
        if (PhIsNullOrEmptyString(Context->UserAgent))
            __leave;

        // Query the current system proxy
        WinHttpGetIEProxyConfigForCurrentUser(&proxyConfig);

        // Open the HTTP session with the system proxy configuration if available
        if (!(httpSessionHandle = WinHttpOpen(
            Context->UserAgent->Buffer,
            proxyConfig.lpszProxy != NULL ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            proxyConfig.lpszProxy,
            proxyConfig.lpszProxyBypass,
            0
            )))
        {
            __leave;
        }

        if (!(httpConnectionHandle = WinHttpConnect(
            httpSessionHandle,
            L"processhacker.sourceforge.net",
            INTERNET_DEFAULT_HTTP_PORT,
            0
            )))
        {
            __leave;
        }

        if (!(httpRequestHandle = WinHttpOpenRequest(
            httpConnectionHandle,
            L"GET",
            L"/update.php",
            NULL,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            0 // WINHTTP_FLAG_REFRESH
            )))
        {
            __leave;
        }

        if (!WinHttpSendRequest(
            httpRequestHandle,
            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0,
            WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH, 0
            ))
        {
            __leave;
        }

        if (!WinHttpReceiveResponse(httpRequestHandle, NULL))
            __leave;

        // Read the resulting xml into our buffer.
        if (!ReadRequestString(httpRequestHandle, &xmlStringBuffer, &xmlStringBufferLength))
            __leave;

        // Check the buffer for valid data.
        if (xmlStringBuffer == NULL || xmlStringBuffer[0] == '\0')
            __leave;

        // Load our XML
        xmlNode = mxmlLoadString(NULL, xmlStringBuffer, QueryXmlDataCallback);
        if (xmlNode == NULL || xmlNode->type != MXML_ELEMENT)
            __leave;

        // Find the version node
        Context->Version = PhGetOpaqueXmlNodeText(
            mxmlFindElement(xmlNode->child, xmlNode, "ver", NULL, NULL, MXML_DESCEND)
            );
        if (PhIsNullOrEmptyString(Context->Version))
            __leave;

        // Find the revision node
        Context->RevVersion = PhGetOpaqueXmlNodeText(
            mxmlFindElement(xmlNode->child, xmlNode, "rev", NULL, NULL, MXML_DESCEND)
            );
        if (PhIsNullOrEmptyString(Context->RevVersion))
            __leave;

        // Find the release date node
        Context->RelDate = PhGetOpaqueXmlNodeText(
            mxmlFindElement(xmlNode->child, xmlNode, "reldate", NULL, NULL, MXML_DESCEND)
            );
        if (PhIsNullOrEmptyString(Context->RelDate))
            __leave;

        // Find the size node
        Context->Size = PhGetOpaqueXmlNodeText(
            mxmlFindElement(xmlNode->child, xmlNode, "size", NULL, NULL, MXML_DESCEND)
            );
        if (PhIsNullOrEmptyString(Context->Size))
            __leave;

        //Find the hash node
        Context->Hash = PhGetOpaqueXmlNodeText(
            mxmlFindElement(xmlNode->child, xmlNode, "sha1", NULL, NULL, MXML_DESCEND)
            );
        if (PhIsNullOrEmptyString(Context->Hash))
            __leave;

        // Find the release notes URL
        Context->ReleaseNotesUrl = PhGetOpaqueXmlNodeText(
            mxmlFindElement(xmlNode->child, xmlNode, "relnotes", NULL, NULL, MXML_DESCEND)
            );
        if (PhIsNullOrEmptyString(Context->ReleaseNotesUrl))
            __leave;

        // Find the installer download URL
        Context->SetupFileDownloadUrl = PhGetOpaqueXmlNodeText(
            mxmlFindElement(xmlNode->child, xmlNode, "setupurl", NULL, NULL, MXML_DESCEND)
            );
        if (PhIsNullOrEmptyString(Context->SetupFileDownloadUrl))
            __leave;

        if (!ParseVersionString(Context))
            __leave;

        isSuccess = TRUE;
    }
    __finally
    {
        if (httpRequestHandle)
            WinHttpCloseHandle(httpRequestHandle);

        if (httpConnectionHandle)
            WinHttpCloseHandle(httpConnectionHandle);

        if (httpSessionHandle)
            WinHttpCloseHandle(httpSessionHandle);

        if (xmlNode)
            mxmlDelete(xmlNode);

        if (xmlStringBuffer)
            PhFree(xmlStringBuffer);
    }

    return isSuccess;
}

static NTSTATUS UpdateCheckSilentThread(
    _In_ PVOID Parameter
    )
{
    PPH_UPDATER_CONTEXT context = NULL;
    ULONGLONG currentVersion = 0;
    ULONGLONG latestVersion = 0;

    context = CreateUpdateContext();

    __try
    {
        if (!QueryUpdateData(context))
            __leave;

        currentVersion = MAKEDLLVERULL(
            context->CurrentMajorVersion,
            context->CurrentMinorVersion,
            0,
            context->CurrentRevisionVersion
            );

#ifdef DEBUG_UPDATE
        latestVersion = MAKEDLLVERULL(
            9999,
            9999,
            0,
            9999
            );
#else
        latestVersion = MAKEDLLVERULL(
            context->MajorVersion,
            context->MinorVersion,
            0,
            context->RevisionVersion
            );
#endif

        // Compare the current version against the latest available version
        if (currentVersion < latestVersion)
        {
            // Don't spam the user the second they open PH, delay dialog creation for 3 seconds.
            Sleep(3000);

            if (!UpdateDialogHandle)
            {
                // We have data we're going to cache and pass into the dialog
                context->HaveData = TRUE;

                // Show the dialog asynchronously on a new thread.
                ShowUpdateDialog(context);
            }
        }
    }
    __finally
    {
        // Check the dialog doesn't own the window context...
        if (context->HaveData == FALSE)
            FreeUpdateContext(context);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS UpdateCheckThread(
    _In_ PVOID Parameter
    )
{
    PPH_UPDATER_CONTEXT context = NULL;
    ULONGLONG currentVersion = 0;
    ULONGLONG latestVersion = 0;

    context = (PPH_UPDATER_CONTEXT)Parameter;
    
    // Check if we have cached update data
    if (!context->HaveData)
        context->HaveData = QueryUpdateData(context);

    // sanity check
    if (!context->HaveData)
    {
        PostMessage(context->DialogHandle, PH_UPDATEISERRORED, 0, 0);
        return STATUS_SUCCESS;
    }

    currentVersion = MAKEDLLVERULL(
        context->CurrentMajorVersion,
        context->CurrentMinorVersion,
        0,
        context->CurrentRevisionVersion
        );

#ifdef DEBUG_UPDATE
    latestVersion = MAKEDLLVERULL(
        9999,
        9999,
        0,
        9999
        );
#else
    latestVersion = MAKEDLLVERULL(
        context->MajorVersion,
        context->MinorVersion,
        0,
        context->RevisionVersion
        );
#endif

    if (currentVersion == latestVersion)
    {
        // User is running the latest version
        PostMessage(context->DialogHandle, PH_UPDATEISCURRENT, 0, 0);
    }
    else if (currentVersion > latestVersion)
    {
        // User is running a newer version
        PostMessage(context->DialogHandle, PH_UPDATENEWER, 0, 0);
    }
    else
    {
        // User is running an older version
        PostMessage(context->DialogHandle, PH_UPDATEAVAILABLE, 0, 0);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS UpdateDownloadThread(
    _In_ PVOID Parameter
    )
{
    BOOLEAN downloadSuccess = FALSE;
    BOOLEAN hashSuccess = FALSE;
    BOOLEAN verifySuccess = FALSE;
    HANDLE tempFileHandle = NULL;
    HINTERNET httpSessionHandle = NULL;
    HINTERNET httpConnectionHandle = NULL;
    HINTERNET httpRequestHandle = NULL;
    PPH_STRING setupTempPath = NULL;
    PPH_STRING downloadHostPath = NULL;
    PPH_STRING downloadUrlPath = NULL;
    URL_COMPONENTS httpUrlComponents = { sizeof(URL_COMPONENTS) };
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG proxyConfig = { 0 };

    PPH_UPDATER_CONTEXT context = (PPH_UPDATER_CONTEXT)Parameter;

    __try
    {
        // Allocate the GetTempPath buffer
        setupTempPath = PhCreateStringEx(NULL, GetTempPath(0, NULL) * sizeof(WCHAR));
        if (PhIsNullOrEmptyString(setupTempPath))
            __leave;

        // Get the temp path
        if (GetTempPath((ULONG)setupTempPath->Length / sizeof(WCHAR), setupTempPath->Buffer) == 0)
            __leave;
        if (PhIsNullOrEmptyString(setupTempPath))
            __leave;

        // Append the tempath to our string: %TEMP%processhacker-%u.%u-setup.exe
        // Example: C:\\Users\\dmex\\AppData\\Temp\\processhacker-2.90-setup.exe
        context->SetupFilePath = PhFormatString(
            L"%sprocesshacker-%lu.%lu-setup.exe",
            setupTempPath->Buffer,
            context->MajorVersion,
            context->MinorVersion
            );
        if (PhIsNullOrEmptyString(context->SetupFilePath))
            __leave;

        // Create output file
        if (!NT_SUCCESS(PhCreateFileWin32(
            &tempFileHandle,
            context->SetupFilePath->Buffer,
            FILE_GENERIC_READ | FILE_GENERIC_WRITE,
            FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_TEMPORARY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            FILE_OVERWRITE_IF,
            FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
            )))
        {
            __leave;
        }

        // Set lengths to non-zero enabling these params to be cracked.
        httpUrlComponents.dwSchemeLength = (ULONG)-1;
        httpUrlComponents.dwHostNameLength = (ULONG)-1;
        httpUrlComponents.dwUrlPathLength = (ULONG)-1;

        if (!WinHttpCrackUrl(
            context->SetupFileDownloadUrl->Buffer,
            (ULONG)context->SetupFileDownloadUrl->Length, 
            0, 
            &httpUrlComponents
            ))
        {
            __leave;
        }

        // Create the Host string.
        downloadHostPath = PhCreateStringEx(
            httpUrlComponents.lpszHostName,
            httpUrlComponents.dwHostNameLength * sizeof(WCHAR)
            );
        if (PhIsNullOrEmptyString(downloadHostPath))
            __leave;

        // Create the Path string.
        downloadUrlPath = PhCreateStringEx(
            httpUrlComponents.lpszUrlPath,
            httpUrlComponents.dwUrlPathLength * sizeof(WCHAR)
            );
        if (PhIsNullOrEmptyString(downloadUrlPath))
            __leave;

        SetDlgItemText(context->DialogHandle, IDC_STATUS, L"Connecting...");

        // Query the current system proxy
        WinHttpGetIEProxyConfigForCurrentUser(&proxyConfig);

        // Open the HTTP session with the system proxy configuration if available
        if (!(httpSessionHandle = WinHttpOpen(
            context->UserAgent->Buffer,
            proxyConfig.lpszProxy != NULL ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            proxyConfig.lpszProxy,
            proxyConfig.lpszProxyBypass,
            0
            )))
        {
            __leave;
        }

        if (!(httpConnectionHandle = WinHttpConnect(
            httpSessionHandle,
            downloadHostPath->Buffer,
            INTERNET_DEFAULT_HTTP_PORT,
            0
            )))
        {
            __leave;
        }

        if (!(httpRequestHandle = WinHttpOpenRequest(
            httpConnectionHandle,
            L"GET",
            downloadUrlPath->Buffer,
            NULL,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_REFRESH
            )))
        {
            __leave;
        }

        SetDlgItemText(context->DialogHandle, IDC_STATUS, L"Sending request...");

        if (!WinHttpSendRequest(
            httpRequestHandle,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH,
            0
            ))
        {
            __leave;
        }

        SetDlgItemText(context->DialogHandle, IDC_STATUS, L"Waiting for response...");

        if (WinHttpReceiveResponse(httpRequestHandle, NULL))
        {
            BYTE buffer[PAGE_SIZE];
            BYTE hashBuffer[20];
            ULONG bytesDownloaded = 0;
            ULONG downloadedBytes = 0;
            ULONG contentLengthSize = sizeof(ULONG);
            ULONG contentLength = 0;

            PH_HASH_CONTEXT hashContext;
            IO_STATUS_BLOCK isb;

            if (!WinHttpQueryHeaders(
                httpRequestHandle,
                WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &contentLength,
                &contentLengthSize,
                0
                ))
            {
                __leave;
            }

            // Initialize hash algorithm.
            PhInitializeHash(&hashContext, Sha1HashAlgorithm);

            // Zero the buffer.
            memset(buffer, 0, PAGE_SIZE);

            // Download the data.
            while (WinHttpReadData(httpRequestHandle, buffer, PAGE_SIZE, &bytesDownloaded))
            {
                // If we get zero bytes, the file was uploaded or there was an error
                if (bytesDownloaded == 0)
                    break;

                // If the dialog was closed, just cleanup and exit
                //if (context->UpdaterState == PhUpdateMaximum)
                if (!UpdateDialogThreadHandle)
                    __leave;

                // Update the hash of bytes we downloaded.
                PhUpdateHash(&hashContext, buffer, bytesDownloaded);

                // Write the downloaded bytes to disk.
                if (!NT_SUCCESS(NtWriteFile(
                    tempFileHandle,
                    NULL,
                    NULL,
                    NULL,
                    &isb,
                    buffer,
                    bytesDownloaded,
                    NULL,
                    NULL
                    )))
                {
                    __leave;
                }

                downloadedBytes += (DWORD)isb.Information;

                // Check the number of bytes written are the same we downloaded.
                if (bytesDownloaded != isb.Information)
                    __leave;

                // Update the GUI progress.
                // TODO: Update on GUI thread.
                {
                    //int percent = MulDiv(100, downloadedBytes, contentLength);
                    FLOAT percent = ((FLOAT)downloadedBytes / contentLength * 100);
                    PPH_STRING totalDownloaded = PhFormatSize(downloadedBytes, -1);
                    PPH_STRING totalLength = PhFormatSize(contentLength, -1);

                    PPH_STRING dlLengthString = PhFormatString(
                        L"%s of %s (%.0f%%)",
                        totalDownloaded->Buffer,
                        totalLength->Buffer,
                        percent
                        );

                    // Update the progress bar position
                    SendMessage(context->ProgressHandle, PBM_SETPOS, (ULONG)percent, 0);
                    Static_SetText(context->StatusHandle, dlLengthString->Buffer);

                    PhDereferenceObject(dlLengthString);
                    PhDereferenceObject(totalDownloaded);
                    PhDereferenceObject(totalLength);
                }
            }

            // Compute hash result (will fail if file not downloaded correctly).
            if (PhFinalHash(&hashContext, &hashBuffer, 20, NULL))
            {
                // Allocate our hash string, hex the final hash result in our hashBuffer.
                PPH_STRING hexString = PhBufferToHexString(hashBuffer, 20);

                if (PhEqualString(hexString, context->Hash, TRUE))
                {
                    hashSuccess = TRUE;
                }

                PhDereferenceObject(hexString);
            }
        }

        downloadSuccess = TRUE;
    }
    __finally
    {
        if (tempFileHandle)
            NtClose(tempFileHandle);

        if (httpRequestHandle)
            WinHttpCloseHandle(httpRequestHandle);

        if (httpConnectionHandle)
            WinHttpCloseHandle(httpConnectionHandle);
               
        if (httpSessionHandle)
            WinHttpCloseHandle(httpSessionHandle);

        PhSwapReference(&setupTempPath, NULL);
        PhSwapReference(&downloadHostPath, NULL);
        PhSwapReference(&downloadUrlPath, NULL);
    }

    if (context->SetupFilePath && PhVerifyFile(context->SetupFilePath->Buffer, NULL) == VrTrusted)
    {
        verifySuccess = TRUE;
    }

    if (downloadSuccess & hashSuccess && verifySuccess)
    {
        PostMessage(context->DialogHandle, PH_HASHSUCCESS, 0, 0);
    }
    else if (downloadSuccess)
    {
        PostMessage(context->DialogHandle, PH_HASHFAILURE, verifySuccess, hashSuccess);
    }
    else
    {
        PostMessage(context->DialogHandle, PH_UPDATEISERRORED, 0, 0);
    }

    return STATUS_SUCCESS;
}

static INT_PTR CALLBACK UpdaterWndProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PPH_UPDATER_CONTEXT context = NULL;

    if (uMsg == WM_INITDIALOG)
    {
        context = (PPH_UPDATER_CONTEXT)lParam;
        SetProp(hwndDlg, L"Context", (HANDLE)context);
    }
    else
    {
        context = (PPH_UPDATER_CONTEXT)GetProp(hwndDlg, L"Context");

        if (uMsg == WM_NCDESTROY)
        {
            RemoveProp(hwndDlg, L"Context");
            FreeUpdateContext(context);
            context = NULL;
        }
    }

    if (context == NULL)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            LOGFONT headerFont;
            HWND parentWindow = GetParent(hwndDlg);

            memset(&headerFont, 0, sizeof(LOGFONT));
            headerFont.lfHeight = -15;
            headerFont.lfWeight = FW_MEDIUM;
            headerFont.lfQuality = CLEARTYPE_QUALITY | ANTIALIASED_QUALITY;

            context->DialogHandle = hwndDlg;
            context->StatusHandle = GetDlgItem(hwndDlg, IDC_STATUS);
            context->ProgressHandle = GetDlgItem(hwndDlg, IDC_PROGRESS);

            // Create the font handle
            context->FontHandle = CreateFontIndirect(&headerFont);

            // Load the Process Hacker icon.
            context->IconHandle = (HICON)LoadImage(
                GetModuleHandle(NULL),
                MAKEINTRESOURCE(PHAPP_IDI_PROCESSHACKER),
                IMAGE_ICON,
                GetSystemMetrics(SM_CXICON),
                GetSystemMetrics(SM_CYICON),
                LR_SHARED
                );

            context->SourceforgeBitmap = LoadImageFromResources(48, 48, MAKEINTRESOURCE(IDB_SF_PNG));

            // Set the window icons
            if (context->IconHandle)
                SendMessage(hwndDlg, WM_SETICON, ICON_SMALL, (LPARAM)context->IconHandle);
            // Set the text font
            if (context->FontHandle)
                SendMessage(GetDlgItem(hwndDlg, IDC_MESSAGE), WM_SETFONT, (WPARAM)context->FontHandle, FALSE);

            if (context->SourceforgeBitmap)
                SendMessage(GetDlgItem(hwndDlg, IDC_UPDATEICON), STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)context->SourceforgeBitmap);

            // Center the update window on PH if it's visible else we center on the desktop.
            PhCenterWindow(hwndDlg, (IsWindowVisible(parentWindow) && !IsIconic(parentWindow)) ? parentWindow : NULL);

            // Show new version info (from the background update check)
            if (context->HaveData)
            {
                HANDLE updateCheckThread = NULL;

                // Create the update check thread.
                if (updateCheckThread = PhCreateThread(0, UpdateCheckThread, context))
                    NtClose(updateCheckThread);
            }
        }
        break;
    case WM_SHOWDIALOG:
        {
            if (IsIconic(hwndDlg))
                ShowWindow(hwndDlg, SW_RESTORE);
            else
                ShowWindow(hwndDlg, SW_SHOW);

            SetForegroundWindow(hwndDlg);
        }
        break;
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
        {
            HDC hDC = (HDC)wParam;
            HWND hwndChild = (HWND)lParam;

            // Check for our static label and change the color.
            if (GetDlgCtrlID(hwndChild) == IDC_MESSAGE)
            {
                SetTextColor(hDC, RGB(19, 112, 171));
            }

            // Set a transparent background for the control backcolor.
            SetBkMode(hDC, TRANSPARENT);

            // set window background color.
            return (INT_PTR)GetSysColorBrush(COLOR_WINDOW);
        }
        break;
    case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDCANCEL:
            case IDOK:
                {
                    PostQuitMessage(0);
                }
                break;
            case IDC_DOWNLOAD:
                {
                    switch (context->UpdaterState)
                    {
                    case PhUpdateDefault:
                        {
                            HANDLE updateCheckThread = NULL;

                            SetDlgItemText(hwndDlg, IDC_MESSAGE, L"Checking for new releases...");
                            SetDlgItemText(hwndDlg, IDC_RELDATE, L"");
                            Button_Enable(GetDlgItem(hwndDlg, IDC_DOWNLOAD), FALSE);

                            if (updateCheckThread = PhCreateThread(0, UpdateCheckThread, context))
                                NtClose(updateCheckThread);
                        }
                        break;
                    case PhUpdateDownload:
                        {
                            if (PhInstalledUsingSetup())
                            {
                                HANDLE downloadThreadHandle = NULL;

                                // Disable the download button
                                Button_Enable(GetDlgItem(hwndDlg, IDC_DOWNLOAD), FALSE);

                                // Reset the progress bar (might be a download retry)
                                SendDlgItemMessage(hwndDlg, IDC_PROGRESS, PBM_SETPOS, 0, 0);

                                if (WindowsVersion > WINDOWS_XP)
                                    SendDlgItemMessage(hwndDlg, IDC_PROGRESS, PBM_SETSTATE, PBST_NORMAL, 0);

                                // Start our Downloader thread
                                if (downloadThreadHandle = PhCreateThread(0, (PUSER_THREAD_START_ROUTINE)UpdateDownloadThread, context))
                                    NtClose(downloadThreadHandle);
                            }
                            else
                            {
                                // Let the user handle non-setup installation, show the homepage and close this dialog.
                                PhShellExecute(hwndDlg, L"http://processhacker.sourceforge.net/downloads.php", NULL);
                                PostQuitMessage(0);
                            }
                        }
                        break;
                    case PhUpdateInstall:
                        {
                            SHELLEXECUTEINFO info = { sizeof(SHELLEXECUTEINFO) };

                            if (PhIsNullOrEmptyString(context->SetupFilePath))
                                break;

                            info.lpFile = context->SetupFilePath->Buffer;
                            info.lpVerb = PhElevated ? NULL : L"runas";
                            info.nShow = SW_SHOW;
                            info.hwnd = hwndDlg;

                            ProcessHacker_PrepareForEarlyShutdown(PhMainWndHandle);

                            if (!ShellExecuteEx(&info))
                            {
                                // Install failed, cancel the shutdown.
                                ProcessHacker_CancelEarlyShutdown(PhMainWndHandle);

                                // Set button text for next action
                                Button_SetText(GetDlgItem(hwndDlg, IDC_DOWNLOAD), L"Retry");
                            }
                            else
                            {
                                ProcessHacker_Destroy(PhMainWndHandle);
                            }
                        }
                        break;
                    }
                }
                break;
            }
            break;
        }
        break;
    case PH_UPDATEAVAILABLE:
        {
            PPH_STRING summaryText = PhFormatString(
                L"Process Hacker %u.%u (r%u)",
                context->MajorVersion,
                context->MinorVersion,
                context->RevisionVersion
                );
            PPH_STRING releaseDateText = PhFormatString(
                L"Released: %s",
                context->RelDate->Buffer
                );
            PPH_STRING releaseSizeText = PhFormatString(
                L"Size: %s",
                context->Size->Buffer
                );

            // Set updater state
            context->UpdaterState = PhUpdateDownload;

            // Set the UI text
            SetDlgItemText(hwndDlg, IDC_MESSAGE, summaryText->Buffer);
            SetDlgItemText(hwndDlg, IDC_RELDATE, releaseDateText->Buffer);
            SetDlgItemText(hwndDlg, IDC_STATUS, releaseSizeText->Buffer);
            Button_SetText(GetDlgItem(hwndDlg, IDC_DOWNLOAD), L"Download");

            // Enable the controls
            Button_Enable(GetDlgItem(hwndDlg, IDC_DOWNLOAD), TRUE);
            Control_Visible(GetDlgItem(hwndDlg, IDC_PROGRESS), TRUE);
            Control_Visible(GetDlgItem(hwndDlg, IDC_INFOSYSLINK), TRUE);

            // free text
            PhDereferenceObject(releaseSizeText);
            PhDereferenceObject(releaseDateText);
            PhDereferenceObject(summaryText);
        }
        break;
    case PH_UPDATEISCURRENT:
        {
            PPH_STRING versionText = PhFormatString(
                L"Stable release build: v%u.%u (r%u)",
                context->CurrentMajorVersion,
                context->CurrentMinorVersion,
                context->CurrentRevisionVersion
                );

            // Set updater state
            context->UpdaterState = PhUpdateMaximum;

            // Set the UI text
            SetDlgItemText(hwndDlg, IDC_MESSAGE, L"You're running the latest version.");
            SetDlgItemText(hwndDlg, IDC_RELDATE, versionText->Buffer);
            Control_Visible(GetDlgItem(hwndDlg, IDC_INFOSYSLINK), TRUE);

            // Disable the download button
            Button_Enable(GetDlgItem(hwndDlg, IDC_DOWNLOAD), FALSE);

            PhDereferenceObject(versionText);
        }
        break;
    case PH_UPDATENEWER:
        {
            PPH_STRING versionText = PhFormatString(
                L"SVN release build: v%u.%u (r%u)",
                context->CurrentMajorVersion,
                context->CurrentMinorVersion,
                context->CurrentRevisionVersion
                );

            context->UpdaterState = PhUpdateMaximum;

            // Set the UI text
            SetDlgItemText(hwndDlg, IDC_MESSAGE, L"You're running a newer version!");
            SetDlgItemText(hwndDlg, IDC_RELDATE, versionText->Buffer);

            // Disable the download button
            Button_Enable(GetDlgItem(hwndDlg, IDC_DOWNLOAD), FALSE);
            Control_Visible(GetDlgItem(hwndDlg, IDC_INFOSYSLINK), TRUE);

            // free text
            PhDereferenceObject(versionText);
        }
        break;
    case PH_HASHSUCCESS:
        {
            context->UpdaterState = PhUpdateInstall;

            // If PH is not elevated, set the UAC shield for the install button as the setup requires elevation.
            if (!PhElevated)
                SendMessage(GetDlgItem(hwndDlg, IDC_DOWNLOAD), BCM_SETSHIELD, 0, TRUE);

            // Set the download result, don't include hash status since it succeeded.
            SetDlgItemText(hwndDlg, IDC_STATUS, L"Click Install to continue update...");

            // Set button text for next action
            Button_SetText(GetDlgItem(hwndDlg, IDC_DOWNLOAD), L"Install");
            // Enable the Download/Install button so the user can install the update
            Button_Enable(GetDlgItem(hwndDlg, IDC_DOWNLOAD), TRUE);
        }
        break;
    case PH_HASHFAILURE:
        {
            context->UpdaterState = PhUpdateDefault;

            if (WindowsVersion > WINDOWS_XP)
                SendDlgItemMessage(hwndDlg, IDC_PROGRESS, PBM_SETSTATE, PBST_ERROR, 0);

            SetDlgItemText(hwndDlg, IDC_MESSAGE, L"Please check for updates again...");
            SetDlgItemText(hwndDlg, IDC_RELDATE, L"An error was encountered while checking for updates.");

            if ((BOOLEAN)wParam)
                SetDlgItemText(hwndDlg, IDC_STATUS, L"Hash check failed.");
            else if ((BOOLEAN)lParam)
                SetDlgItemText(hwndDlg, IDC_STATUS, L"Signature check failed.");

            // Set button text for next action
            Button_SetText(GetDlgItem(hwndDlg, IDC_DOWNLOAD), L"Retry");
            // Enable the Install button
            Button_Enable(GetDlgItem(hwndDlg, IDC_DOWNLOAD), TRUE);
            // Hash failed, reset state to downloading so user can redownload the file.
        }
        break;
    case PH_UPDATEISERRORED:
        {
            context->UpdaterState = PhUpdateDefault;

            SetDlgItemText(hwndDlg, IDC_MESSAGE, L"Please check for updates again...");
            SetDlgItemText(hwndDlg, IDC_RELDATE, L"An error was encountered while checking for updates.");

            Button_SetText(GetDlgItem(hwndDlg, IDC_DOWNLOAD), L"Retry");
            Button_Enable(GetDlgItem(hwndDlg, IDC_DOWNLOAD), TRUE);
        }
        break;
    case WM_NOTIFY:
        {
            switch (((LPNMHDR)lParam)->code)
            {
            case NM_CLICK: // Mouse
            case NM_RETURN: // Keyboard
                {
                    // Launch the ReleaseNotes URL (if it exists) with the default browser
                    if (!PhIsNullOrEmptyString(context->ReleaseNotesUrl))
                        PhShellExecute(hwndDlg, context->ReleaseNotesUrl->Buffer, NULL);
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}

static NTSTATUS ShowUpdateDialogThread(
    _In_ PVOID Parameter
    )
{
    BOOL result;
    MSG message;
    PH_AUTO_POOL autoPool;
    PPH_UPDATER_CONTEXT context = NULL;

    if (Parameter)
        context = (PPH_UPDATER_CONTEXT)Parameter;
    else
        context = CreateUpdateContext();

    PhInitializeAutoPool(&autoPool);

    UpdateDialogHandle = CreateDialogParam(
        (HINSTANCE)PluginInstance->DllBase,
        MAKEINTRESOURCE(IDD_UPDATE),
        PhMainWndHandle,
        UpdaterWndProc,
        (LPARAM)context
        );

    PhSetEvent(&InitializedEvent);

    while (result = GetMessage(&message, NULL, 0, 0))
    {
        if (result == -1)
            break;

        if (!IsDialogMessage(UpdateDialogHandle, &message))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        PhDrainAutoPool(&autoPool);
    }

    PhDeleteAutoPool(&autoPool);
    PhResetEvent(&InitializedEvent);

    if (UpdateDialogHandle)
    {
        DestroyWindow(UpdateDialogHandle);
        UpdateDialogHandle = NULL;
    }

    if (UpdateDialogThreadHandle)
    {
        NtClose(UpdateDialogThreadHandle);
        UpdateDialogThreadHandle = NULL;
    }

    return STATUS_SUCCESS;
}

VOID ShowUpdateDialog(
    _In_opt_ PPH_UPDATER_CONTEXT Context
    )
{
    if (!UpdateDialogThreadHandle)
    {
        if (!(UpdateDialogThreadHandle = PhCreateThread(0, ShowUpdateDialogThread, Context)))
        {
            PhShowStatus(PhMainWndHandle, L"Unable to create the updater window.", 0, GetLastError());
            return;
        }

        PhWaitForEvent(&InitializedEvent, NULL);
    }

    PostMessage(UpdateDialogHandle, WM_SHOWDIALOG, 0, 0);
}

VOID StartInitialCheck(
    VOID
    )
{
    HANDLE silentCheckThread = NULL;

    if (silentCheckThread = PhCreateThread(0, UpdateCheckSilentThread, NULL))
        NtClose(silentCheckThread);
}