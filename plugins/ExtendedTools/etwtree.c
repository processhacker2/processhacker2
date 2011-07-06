/*
 * Process Hacker Extended Tools - 
 *   ETW process tree support
 * 
 * Copyright (C) 2011 wj32
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

#include "exttools.h"

LONG EtpProcessTreeNewSortFunction(
    __in PVOID Node1,
    __in PVOID Node2,
    __in ULONG SubId,
    __in PVOID Context
    );

LONG EtpNetworkTreeNewSortFunction(
    __in PVOID Node1,
    __in PVOID Node2,
    __in ULONG SubId,
    __in PVOID Context
    );

typedef struct _COLUMN_INFO
{
    ULONG SubId;
    PWSTR Text;
    ULONG Width;
    ULONG Alignment;
    ULONG TextFlags;
} COLUMN_INFO, *PCOLUMN_INFO;

VOID EtpAddTreeNewColumn(
    __in PPH_PLUGIN_TREENEW_INFORMATION TreeNewInfo,
    __in ULONG SubId,
    __in PWSTR Text,
    __in ULONG Width,
    __in ULONG Alignment,
    __in ULONG TextFlags,
    __in PPH_PLUGIN_TREENEW_SORT_FUNCTION SortFunction
    )
{
    PH_TREENEW_COLUMN column;

    memset(&column, 0, sizeof(PH_TREENEW_COLUMN));
    column.Text = Text;
    column.Width = Width;
    column.Alignment = Alignment;
    column.TextFlags = TextFlags;

    PhPluginAddTreeNewColumn(
        PluginInstance,
        TreeNewInfo->CmData,
        &column,
        SubId,
        NULL,
        SortFunction
        );
}

VOID EtEtwProcessTreeNewInitializing(
    __in PVOID Parameter
    )
{
    static COLUMN_INFO columns[] =
    {
        { ETPRTNC_DISKREADS, L"Disk Reads", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_DISKWRITES, L"Disk Writes", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_DISKREADBYTES, L"Disk Read Bytes", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_DISKWRITEBYTES, L"Disk Write Bytes", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_DISKTOTALBYTES, L"Disk Total Bytes", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_DISKREADSDELTA, L"Disk Reads Delta", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_DISKWRITESDELTA, L"Disk Writes Delta", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_DISKREADBYTESDELTA, L"Disk Read Bytes Delta", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_DISKWRITEBYTESDELTA, L"Disk Write Bytes Delta", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_DISKTOTALBYTESDELTA, L"Disk Total Bytes Delta", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_NETWORKRECEIVES, L"Network Receives", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_NETWORKSENDS, L"Network Sends", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_NETWORKRECEIVEBYTES, L"Network Receive Bytes", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_NETWORKSENDBYTES, L"Network Send Bytes", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_NETWORKTOTALBYTES, L"Network Total Bytes", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_NETWORKRECEIVESDELTA, L"Network Receives Delta", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_NETWORKSENDSDELTA, L"Network Sends Delta", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_NETWORKRECEIVEBYTESDELTA, L"Network Receive Bytes Delta", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_NETWORKSENDBYTESDELTA, L"Network Send Bytes Delta", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETPRTNC_NETWORKTOTALBYTESDELTA, L"Network Total Bytes Delta", 70, PH_ALIGN_RIGHT, DT_RIGHT }
    };

    PPH_PLUGIN_TREENEW_INFORMATION treeNewInfo = Parameter;
    ULONG i;

    for (i = 0; i < sizeof(columns) / sizeof(COLUMN_INFO); i++)
    {
        EtpAddTreeNewColumn(treeNewInfo, columns[i].SubId, columns[i].Text, columns[i].Width, columns[i].Alignment,
            columns[i].TextFlags, EtpProcessTreeNewSortFunction);
    }
}

VOID EtEtwProcessTreeNewMessage(
    __in PVOID Parameter
    )
{
    PPH_PLUGIN_TREENEW_MESSAGE message = Parameter;

    if (message->Message == TreeNewGetCellText)
    {
        PPH_TREENEW_GET_CELL_TEXT getCellText = message->Parameter1;
        PPH_PROCESS_NODE processNode = (PPH_PROCESS_NODE)getCellText->Node;
        PET_PROCESS_ETW_BLOCK block;
        PPH_STRING text;

        if (!EtEtwEnabled)
            return;

        if (!(block = EtFindProcessEtwBlock(processNode->ProcessItem)))
            return;

        PhAcquireQueuedLockExclusive(&block->TextCacheLock);

        if (block->TextCacheValid[message->SubId])
        {
            if (block->TextCache[message->SubId])
                getCellText->Text = block->TextCache[message->SubId]->sr;
        }
        else
        {
            text = NULL;

            switch (message->SubId)
            {
            case ETPRTNC_DISKREADS:
                if (block->DiskReadCount != 0)
                    text = PhFormatUInt64(block->DiskReadCount, TRUE);
                break;
            case ETPRTNC_DISKWRITES:
                if (block->DiskWriteCount != 0)
                    text = PhFormatUInt64(block->DiskWriteCount, TRUE);
                break;
            case ETPRTNC_DISKREADBYTES:
                if (block->DiskReadRaw != 0)
                    text = PhFormatSize(block->DiskReadRaw, -1);
                break;
            case ETPRTNC_DISKWRITEBYTES:
                if (block->DiskWriteRaw != 0)
                    text = PhFormatSize(block->DiskWriteRaw, -1);
                break;
            case ETPRTNC_DISKTOTALBYTES:
                if (block->DiskReadRaw + block->DiskWriteRaw != 0)
                    text = PhFormatSize(block->DiskReadRaw + block->DiskWriteRaw, -1);
                break;
            case ETPRTNC_DISKREADSDELTA:
                if (block->DiskReadDelta.Delta != 0)
                    text = PhFormatUInt64(block->DiskReadDelta.Delta, TRUE);
                break;
            case ETPRTNC_DISKWRITESDELTA:
                if (block->DiskWriteDelta.Delta != 0)
                    text = PhFormatUInt64(block->DiskWriteDelta.Delta, TRUE);
                break;
            case ETPRTNC_DISKREADBYTESDELTA:
                if (block->DiskReadRawDelta.Delta != 0)
                    text = PhFormatSize(block->DiskReadRawDelta.Delta, -1);
                break;
            case ETPRTNC_DISKWRITEBYTESDELTA:
                if (block->DiskWriteRawDelta.Delta != 0)
                    text = PhFormatSize(block->DiskWriteRawDelta.Delta, -1);
                break;
            case ETPRTNC_DISKTOTALBYTESDELTA:
                if (block->DiskReadRawDelta.Delta + block->DiskWriteRawDelta.Delta != 0)
                    text = PhFormatSize(block->DiskReadRawDelta.Delta + block->DiskWriteRawDelta.Delta, -1);
                break;
            case ETPRTNC_NETWORKRECEIVES:
                if (block->NetworkReceiveCount != 0)
                    text = PhFormatUInt64(block->NetworkReceiveCount, TRUE);
                break;
            case ETPRTNC_NETWORKSENDS:
                if (block->NetworkSendCount != 0)
                    text = PhFormatUInt64(block->NetworkSendCount, TRUE);
                break;
            case ETPRTNC_NETWORKRECEIVEBYTES:
                if (block->NetworkReceiveRaw != 0)
                    text = PhFormatSize(block->NetworkReceiveRaw, -1);
                break;
            case ETPRTNC_NETWORKSENDBYTES:
                if (block->NetworkSendRaw != 0)
                    text = PhFormatSize(block->NetworkSendRaw, -1);
                break;
            case ETPRTNC_NETWORKTOTALBYTES:
                if (block->NetworkReceiveRaw + block->NetworkSendRaw != 0)
                    text = PhFormatSize(block->NetworkReceiveRaw + block->NetworkSendRaw, -1);
                break;
            case ETPRTNC_NETWORKRECEIVESDELTA:
                if (block->NetworkReceiveDelta.Delta != 0)
                    text = PhFormatUInt64(block->NetworkReceiveDelta.Delta, TRUE);
                break;
            case ETPRTNC_NETWORKSENDSDELTA:
                if (block->NetworkSendDelta.Delta != 0)
                    text = PhFormatUInt64(block->NetworkSendDelta.Delta, TRUE);
                break;
            case ETPRTNC_NETWORKRECEIVEBYTESDELTA:
                if (block->NetworkReceiveRawDelta.Delta != 0)
                    text = PhFormatSize(block->NetworkReceiveRawDelta.Delta, -1);
                break;
            case ETPRTNC_NETWORKSENDBYTESDELTA:
                if (block->NetworkSendRawDelta.Delta != 0)
                    text = PhFormatSize(block->NetworkSendRawDelta.Delta, -1);
                break;
            case ETPRTNC_NETWORKTOTALBYTESDELTA:
                if (block->NetworkReceiveRawDelta.Delta + block->NetworkSendRawDelta.Delta != 0)
                    text = PhFormatSize(block->NetworkReceiveRawDelta.Delta + block->NetworkSendRawDelta.Delta, -1);
                break;
            }

            if (text)
            {
                getCellText->Text = text->sr;
            }

            PhSwapReference(&block->TextCache[message->SubId], text);
            block->TextCacheValid[message->SubId] = TRUE;
        }

        PhReleaseQueuedLockExclusive(&block->TextCacheLock);

        EtDereferenceProcessEtwBlock(block);
    }
}

LONG EtpProcessTreeNewSortFunction(
    __in PVOID Node1,
    __in PVOID Node2,
    __in ULONG SubId,
    __in PVOID Context
    )
{
    LONG result;
    PPH_PROCESS_NODE node1 = Node1;
    PPH_PROCESS_NODE node2 = Node2;
    PET_PROCESS_ETW_BLOCK block1;
    PET_PROCESS_ETW_BLOCK block2;

    if (!EtEtwEnabled)
        return 0;

    if (!(block1 = EtFindProcessEtwBlock(node1->ProcessItem)))
    {
        return 0;
    }

    if (!(block2 = EtFindProcessEtwBlock(node2->ProcessItem)))
    {
        EtDereferenceProcessEtwBlock(block1);
        return 0;
    }

    result = 0;

    switch (SubId)
    {
    case ETPRTNC_DISKREADS:
        result = uintcmp(block1->DiskReadCount, block2->DiskReadCount);
        break;
    case ETPRTNC_DISKWRITES:
        result = uintcmp(block1->DiskWriteCount, block2->DiskWriteCount);
        break;
    case ETPRTNC_DISKREADBYTES:
        result = uintcmp(block1->DiskReadRaw, block2->DiskReadRaw);
        break;
    case ETPRTNC_DISKWRITEBYTES:
        result = uintcmp(block1->DiskWriteRaw, block2->DiskWriteRaw);
        break;
    case ETPRTNC_DISKTOTALBYTES:
        result = uintcmp(block1->DiskReadRaw + block1->DiskWriteRaw, block2->DiskReadRaw + block2->DiskWriteRaw);
        break;
    case ETPRTNC_DISKREADSDELTA:
        result = uintcmp(block1->DiskReadDelta.Delta, block2->DiskReadDelta.Delta);
        break;
    case ETPRTNC_DISKWRITESDELTA:
        result = uintcmp(block1->DiskWriteDelta.Delta, block2->DiskWriteDelta.Delta);
        break;
    case ETPRTNC_DISKREADBYTESDELTA:
        result = uintcmp(block1->DiskReadRawDelta.Delta, block2->DiskReadRawDelta.Delta);
        break;
    case ETPRTNC_DISKWRITEBYTESDELTA:
        result = uintcmp(block1->DiskWriteRawDelta.Delta, block2->DiskWriteRawDelta.Delta);
        break;
    case ETPRTNC_DISKTOTALBYTESDELTA:
        result = uintcmp(block1->DiskReadRawDelta.Delta + block1->DiskWriteRawDelta.Delta, block2->DiskReadRawDelta.Delta + block2->DiskWriteRawDelta.Delta);
        break;
    case ETPRTNC_NETWORKRECEIVES:
        result = uintcmp(block1->NetworkReceiveCount, block2->NetworkReceiveCount);
        break;
    case ETPRTNC_NETWORKSENDS:
        result = uintcmp(block1->NetworkSendCount, block2->NetworkSendCount);
        break;
    case ETPRTNC_NETWORKRECEIVEBYTES:
        result = uintcmp(block1->NetworkReceiveRaw, block2->NetworkReceiveRaw);
        break;
    case ETPRTNC_NETWORKSENDBYTES:
        result = uintcmp(block1->NetworkSendRaw, block2->NetworkSendRaw);
        break;
    case ETPRTNC_NETWORKTOTALBYTES:
        result = uintcmp(block1->NetworkReceiveRaw + block1->NetworkSendRaw, block2->NetworkReceiveRaw + block2->NetworkSendRaw);
        break;
    case ETPRTNC_NETWORKRECEIVESDELTA:
        result = uintcmp(block1->NetworkReceiveDelta.Delta, block2->NetworkReceiveDelta.Delta);
        break;
    case ETPRTNC_NETWORKSENDSDELTA:
        result = uintcmp(block1->NetworkSendDelta.Delta, block2->NetworkSendDelta.Delta);
        break;
    case ETPRTNC_NETWORKRECEIVEBYTESDELTA:
        result = uintcmp(block1->NetworkReceiveRawDelta.Delta, block2->NetworkReceiveRawDelta.Delta);
        break;
    case ETPRTNC_NETWORKSENDBYTESDELTA:
        result = uintcmp(block1->NetworkSendRawDelta.Delta, block2->NetworkSendRawDelta.Delta);
        break;
    case ETPRTNC_NETWORKTOTALBYTESDELTA:
        result = uintcmp(block1->NetworkReceiveRawDelta.Delta + block1->NetworkSendRawDelta.Delta, block2->NetworkReceiveRawDelta.Delta + block2->NetworkSendRawDelta.Delta);
        break;
    }

    EtDereferenceProcessEtwBlock(block1);
    EtDereferenceProcessEtwBlock(block2);

    return result;
}

VOID EtEtwNetworkTreeNewInitializing(
    __in PVOID Parameter
    )
{
    static COLUMN_INFO columns[] =
    {
        { ETNETNC_RECEIVES, L"Receives", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETNETNC_SENDS, L"Sends", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETNETNC_RECEIVEBYTES, L"Receive Bytes", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETNETNC_SENDBYTES, L"Send Bytes", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETNETNC_TOTALBYTES, L"Total Bytes", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETNETNC_RECEIVESDELTA, L"Receives Delta", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETNETNC_SENDSDELTA, L"Sends Delta", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETNETNC_RECEIVEBYTESDELTA, L"Receive Bytes Delta", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETNETNC_SENDBYTESDELTA, L"Send Bytes Delta", 70, PH_ALIGN_RIGHT, DT_RIGHT },
        { ETNETNC_TOTALBYTESDELTA, L"Total Bytes Delta", 70, PH_ALIGN_RIGHT, DT_RIGHT }
    };

    PPH_PLUGIN_TREENEW_INFORMATION treeNewInfo = Parameter;
    ULONG i;

    for (i = 0; i < sizeof(columns) / sizeof(COLUMN_INFO); i++)
    {
        EtpAddTreeNewColumn(treeNewInfo, columns[i].SubId, columns[i].Text, columns[i].Width, columns[i].Alignment,
            columns[i].TextFlags, EtpNetworkTreeNewSortFunction);
    }
}

VOID EtEtwNetworkTreeNewMessage(
    __in PVOID Parameter
    )
{
    PPH_PLUGIN_TREENEW_MESSAGE message = Parameter;

    if (message->Message == TreeNewGetCellText)
    {
        PPH_TREENEW_GET_CELL_TEXT getCellText = message->Parameter1;
        PPH_NETWORK_NODE networkNode = (PPH_NETWORK_NODE)getCellText->Node;
        PET_NETWORK_ETW_BLOCK block;
        PPH_STRING text;

        if (!EtEtwEnabled)
            return;

        if (!(block = EtFindNetworkEtwBlock(networkNode->NetworkItem)))
            return;

        PhAcquireQueuedLockExclusive(&block->TextCacheLock);

        if (block->TextCacheValid[message->SubId])
        {
            if (block->TextCache[message->SubId])
                getCellText->Text = block->TextCache[message->SubId]->sr;
        }
        else
        {
            text = NULL;

            switch (message->SubId)
            {
            case ETNETNC_RECEIVES:
                if (block->ReceiveCount != 0)
                    text = PhFormatUInt64(block->ReceiveCount, TRUE);
                break;
            case ETNETNC_SENDS:
                if (block->SendCount != 0)
                    text = PhFormatUInt64(block->SendCount, TRUE);
                break;
            case ETNETNC_RECEIVEBYTES:
                if (block->ReceiveRaw != 0)
                    text = PhFormatSize(block->ReceiveRaw, -1);
                break;
            case ETNETNC_SENDBYTES:
                if (block->SendRaw != 0)
                    text = PhFormatSize(block->SendRaw, -1);
                break;
            case ETNETNC_TOTALBYTES:
                if (block->ReceiveRaw + block->SendRaw != 0)
                    text = PhFormatSize(block->ReceiveRaw + block->SendRaw, -1);
                break;
            case ETNETNC_RECEIVESDELTA:
                if (block->ReceiveDelta.Delta != 0)
                    text = PhFormatUInt64(block->ReceiveDelta.Delta, TRUE);
                break;
            case ETNETNC_SENDSDELTA:
                if (block->SendDelta.Delta != 0)
                    text = PhFormatUInt64(block->SendDelta.Delta, TRUE);
                break;
            case ETNETNC_RECEIVEBYTESDELTA:
                if (block->ReceiveRawDelta.Delta != 0)
                    text = PhFormatSize(block->ReceiveRawDelta.Delta, -1);
                break;
            case ETNETNC_SENDBYTESDELTA:
                if (block->SendRawDelta.Delta != 0)
                    text = PhFormatSize(block->SendRawDelta.Delta, -1);
                break;
            case ETNETNC_TOTALBYTESDELTA:
                if (block->ReceiveRawDelta.Delta + block->SendRawDelta.Delta != 0)
                    text = PhFormatSize(block->ReceiveRawDelta.Delta + block->SendRawDelta.Delta, -1);
                break;
            }

            if (text)
            {
                getCellText->Text = text->sr;
            }

            PhSwapReference(&block->TextCache[message->SubId], text);
            block->TextCacheValid[message->SubId] = TRUE;
        }

        PhReleaseQueuedLockExclusive(&block->TextCacheLock);

        EtDereferenceNetworkEtwBlock(block);
    }
}

LONG EtpNetworkTreeNewSortFunction(
    __in PVOID Node1,
    __in PVOID Node2,
    __in ULONG SubId,
    __in PVOID Context
    )
{
    LONG result;
    PPH_NETWORK_NODE node1 = Node1;
    PPH_NETWORK_NODE node2 = Node2;
    PET_NETWORK_ETW_BLOCK block1;
    PET_NETWORK_ETW_BLOCK block2;

    if (!EtEtwEnabled)
        return 0;

    if (!(block1 = EtFindNetworkEtwBlock(node1->NetworkItem)))
    {
        return 0;
    }

    if (!(block2 = EtFindNetworkEtwBlock(node2->NetworkItem)))
    {
        EtDereferenceNetworkEtwBlock(block1);
        return 0;
    }

    result = 0;

    switch (SubId)
    {
    case ETNETNC_RECEIVES:
        result = uintcmp(block1->ReceiveCount, block2->ReceiveCount);
        break;
    case ETNETNC_SENDS:
        result = uintcmp(block1->SendCount, block2->SendCount);
        break;
    case ETNETNC_RECEIVEBYTES:
        result = uintcmp(block1->ReceiveRaw, block2->ReceiveRaw);
        break;
    case ETNETNC_SENDBYTES:
        result = uintcmp(block1->SendRaw, block2->SendRaw);
        break;
    case ETNETNC_TOTALBYTES:
        result = uintcmp(block1->ReceiveRaw + block1->SendRaw, block2->ReceiveRaw + block2->SendRaw);
        break;
    case ETNETNC_RECEIVESDELTA:
        result = uintcmp(block1->ReceiveDelta.Delta, block2->ReceiveDelta.Delta);
        break;
    case ETNETNC_SENDSDELTA:
        result = uintcmp(block1->SendDelta.Delta, block2->SendDelta.Delta);
        break;
    case ETNETNC_RECEIVEBYTESDELTA:
        result = uintcmp(block1->ReceiveRawDelta.Delta, block2->ReceiveRawDelta.Delta);
        break;
    case ETNETNC_SENDBYTESDELTA:
        result = uintcmp(block1->SendRawDelta.Delta, block2->SendRawDelta.Delta);
        break;
    case ETNETNC_TOTALBYTESDELTA:
        result = uintcmp(block1->ReceiveRawDelta.Delta + block1->SendRawDelta.Delta, block2->ReceiveRawDelta.Delta + block2->SendRawDelta.Delta);
        break;
    }

    EtDereferenceNetworkEtwBlock(block1);
    EtDereferenceNetworkEtwBlock(block2);

    return result;
}
