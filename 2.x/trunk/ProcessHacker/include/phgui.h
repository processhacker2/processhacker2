#ifndef PHGUI_H
#define PHGUI_H

#include <ph.h>
#include <commctrl.h>
#define COBJMACROS
#include <shlobj.h>
#include <prsht.h>
#include <aclapi.h>
#include "resource.h"

#define KPH_ERROR_MESSAGE (L"KProcessHacker does not support your operating system " \
            L"or could not be loaded. Make sure Process Hacker is running " \
            L"on a 32-bit system and with administrative privileges.")

// main

typedef struct _PH_STARTUP_PARAMETERS
{
    BOOLEAN NoKph;
    BOOLEAN NoSettings;
    PPH_STRING SettingsFileName;
    BOOLEAN ShowHidden;
    BOOLEAN ShowVisible;

    BOOLEAN CommandMode;
    PPH_STRING CommandType;
    PPH_STRING CommandObject;
    PPH_STRING CommandAction;

    BOOLEAN RunAsServiceMode;
} PH_STARTUP_PARAMETERS, *PPH_STARTUP_PARAMETERS;

INT PhMainMessageLoop();

VOID PhRegisterDialog(
    __in HWND DialogWindowHandle
    );

VOID PhUnregisterDialog(
    __in HWND DialogWindowHandle
    );

VOID PhActivatePreviousInstance();

VOID PhInitializeCommonControls();

VOID PhInitializeFont(
    __in HWND hWnd
    );

VOID PhInitializeKph();

BOOLEAN PhInitializeSystem();

VOID PhInitializeSystemInformation();

ATOM PhRegisterWindowClass();

VOID PhInitializeWindowsVersion();

// guisup

typedef BOOL (WINAPI *_ChangeWindowMessageFilter)(
    __in UINT message,
    __in DWORD dwFlag
    );

#define RFF_NOBROWSE 0x0001
#define RFF_NODEFAULT 0x0002
#define RFF_CALCDIRECTORY 0x0004
#define RFF_NOLABEL 0x0008
#define RFF_NOSEPARATEMEM 0x0020

#define RFN_VALIDATE (-510)

typedef struct _NMRUNFILEDLGW
{
    NMHDR hdr;
    LPCWSTR lpszFile;
    LPCWSTR lpszDirectory; 
    UINT nShow;
} NMRUNFILEDLGW, *LPNMRUNFILEDLGW, *PNMRUNFILEDLGW;

typedef NMRUNFILEDLGW NMRUNFILEDLG;
typedef PNMRUNFILEDLGW PNMRUNFILEDLG;
typedef LPNMRUNFILEDLGW LPNMRUNFILEDLG;

#define RF_OK 0x0000
#define RF_CANCEL 0x0001
#define RF_RETRY 0x0002

typedef BOOL (WINAPI *_RunFileDlg)(
    __in HWND hwndOwner,
    __in_opt HICON hIcon,
    __in_opt LPCWSTR lpszDirectory,
    __in_opt LPCWSTR lpszTitle,
    __in_opt LPCWSTR lpszDescription,
    __in ULONG uFlags
    );

typedef HRESULT (WINAPI *_SetWindowTheme)(
    __in HWND hwnd,
    __in LPCWSTR pszSubAppName,
    __in LPCWSTR pszSubIdList
    );

typedef HRESULT (WINAPI *_SHAutoComplete)(
    __in HWND hwndEdit,
    __in DWORD dwFlags
    );

typedef HRESULT (WINAPI *_SHOpenFolderAndSelectItems)(
    __in PCIDLIST_ABSOLUTE pidlFolder,
    __in UINT cidl,
    __in PCUITEMID_CHILD_ARRAY *apidl,
    __in DWORD dwFlags
    );

typedef HRESULT (WINAPI *_SHParseDisplayName)(
    __in LPCWSTR pszName,
    __in IBindCtx *pbc,
    __out PIDLIST_ABSOLUTE *ppidl,
    __in SFGAOF sfgaoIn,
    __out SFGAOF *psfgaoOut
    );

typedef INT (WINAPI *_StrCmpLogicalW)(
    __in LPCWSTR psz1,
    __in LPCWSTR psz2
    );

typedef HRESULT (WINAPI *_TaskDialogIndirect)(      
    __in const TASKDIALOGCONFIG *pTaskConfig,
    __in int *pnButton,
    __in int *pnRadioButton,
    __in BOOL *pfVerificationFlagChecked
    );

#ifndef GUISUP_PRIVATE
extern _ChangeWindowMessageFilter ChangeWindowMessageFilter_I;
extern _RunFileDlg RunFileDlg;
extern _SHAutoComplete SHAutoComplete_I;
extern _SHOpenFolderAndSelectItems SHOpenFolderAndSelectItems_I;
extern _SHParseDisplayName SHParseDisplayName_I;
extern _StrCmpLogicalW StrCmpLogicalW_I;
extern _TaskDialogIndirect TaskDialogIndirect_I;
#endif

VOID PhGuiSupportInitialization();

VOID PhSetControlTheme(
    __in HWND Handle,
    __in PWSTR Theme
    );

FORCEINLINE VOID PhSetWindowStyle(
    __in HWND Handle,
    __in LONG_PTR Mask,
    __in LONG_PTR Value
    )
{
    LONG_PTR style;

    style = GetWindowLongPtr(Handle, GWL_STYLE);
    style &= ~Mask;
    style |= Value;
    SetWindowLongPtr(Handle, GWL_STYLE, style);
}

#ifndef WM_REFLECT
#define WM_REFLECT 0x2000
#endif

#define REFLECT_MESSAGE(hwnd, msg, wParam, lParam) \
    { \
        LRESULT result_ = PhReflectMessage(hwnd, msg, wParam, lParam); \
        \
        if (result_) \
            return result_; \
    }

#define REFLECT_MESSAGE_DLG(hwndDlg, hwnd, msg, wParam, lParam) \
    { \
        LRESULT result_ = PhReflectMessage(hwnd, msg, wParam, lParam); \
        \
        if (result_) \
        { \
            SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, result_); \
            return TRUE; \
        } \
    }

FORCEINLINE LRESULT PhReflectMessage(
    __in HWND Handle,
    __in UINT Message,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    if (Message == WM_NOTIFY)
    {
        LPNMHDR header = (LPNMHDR)lParam;

        if (header->hwndFrom == Handle)
            return SendMessage(Handle, WM_REFLECT + Message, wParam, lParam);
    }

    return 0;
}

HWND PhCreateListViewControl(
    __in HWND ParentHandle,
    __in INT_PTR Id
    );

FORCEINLINE VOID PhSetListViewStyle(
    __in HWND Handle,
    __in BOOLEAN AllowDragDrop,
    __in BOOLEAN ShowLabelTips
    )
{
    ULONG style;

    style = LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP;

    if (AllowDragDrop)
        style |= LVS_EX_HEADERDRAGDROP;
    if (ShowLabelTips)
        style |= LVS_EX_LABELTIP;

    ListView_SetExtendedListViewStyleEx(
        Handle,
        style,
        -1
        );
}

INT PhAddListViewColumn(
    __in HWND ListViewHandle,
    __in INT Index,
    __in INT DisplayIndex,
    __in INT SubItemIndex,
    __in INT Format,
    __in INT Width,
    __in PWSTR Text
    );

INT PhAddListViewItem(
    __in HWND ListViewHandle,
    __in INT Index,
    __in PWSTR Text,
    __in PVOID Param
    );

INT PhFindListViewItemByFlags(
    __in HWND ListViewHandle,
    __in INT StartIndex,
    __in ULONG Flags
    );

INT PhFindListViewItemByParam(
    __in HWND ListViewHandle,
    __in INT StartIndex,
    __in PVOID Param
    );

LOGICAL PhGetListViewItemParam(
    __in HWND ListViewHandle,
    __in INT Index,
    __out PPVOID Param
    );

VOID PhRemoveListViewItem(
    __in HWND ListViewHandle,
    __in INT Index
    );

VOID PhSetListViewItemImageIndex(
    __in HWND ListViewHandle,
    __in INT Index,
    __in INT ImageIndex
    );

VOID PhSetListViewItemStateImage(
    __in HWND ListViewHandle,
    __in INT Index,
    __in INT StateImage
    );

VOID PhSetListViewSubItem(
    __in HWND ListViewHandle,
    __in INT Index,
    __in INT SubItemIndex,
    __in PWSTR Text
    );

HWND PhCreateTabControl(
    __in HWND ParentHandle
    );

INT PhAddTabControlTab(
    __in HWND TabControlHandle,
    __in INT Index,
    __in PWSTR Text
    );

#define PHA_GET_DLGITEM_TEXT(hwndDlg, id) \
    ((PPH_STRING)PHA_DEREFERENCE(PhGetWindowText(GetDlgItem(hwndDlg, id))))

PPH_STRING PhGetWindowText(
    __in HWND hwnd
    );

PPH_STRING PhGetComboBoxString(
    __in HWND hwnd,
    __in INT Index
    );

VOID PhShowContextMenu(
    __in HWND hwnd,
    __in HWND subHwnd,
    __in HMENU menu,
    __in POINT point
    );

VOID PhSetRadioCheckMenuItem(
    __in HMENU Menu,
    __in ULONG Id,
    __in BOOLEAN RadioCheck
    );

VOID PhEnableMenuItem(
    __in HMENU Menu,
    __in ULONG Id,
    __in BOOLEAN Enable
    );

VOID PhEnableAllMenuItems(
    __in HMENU Menu,
    __in BOOLEAN Enable
    );

PVOID PhGetSelectedListViewItemParam(
    __in HWND hWnd
    );

VOID PhGetSelectedListViewItemParams(
    __in HWND hWnd,
    __out PVOID **Items,
    __out PULONG NumberOfItems
    );

VOID PhSetImageListBitmap(
    __in HIMAGELIST ImageList,
    __in INT Index,
    __in HINSTANCE InstanceHandle,
    __in LPCWSTR BitmapName
    );

#define PH_ANCHOR_LEFT 0x1
#define PH_ANCHOR_TOP 0x2
#define PH_ANCHOR_RIGHT 0x4
#define PH_ANCHOR_BOTTOM 0x8
#define PH_ANCHOR_ALL 0xf

#define PH_FORCE_INVALIDATE 0x1000

typedef struct _PH_LAYOUT_ITEM
{
    HWND Handle;
    struct _PH_LAYOUT_ITEM *ParentItem;
    ULONG LayoutNumber;
    ULONG NumberOfChildren;
    HDWP DeferHandle;

    RECT Rect;
    RECT OrigRect;
    RECT Margin;
    ULONG Anchor;
} PH_LAYOUT_ITEM, *PPH_LAYOUT_ITEM;

typedef struct _PH_LAYOUT_MANAGER
{
    PPH_LIST List;
    PH_LAYOUT_ITEM RootItem;

    ULONG LayoutNumber;
} PH_LAYOUT_MANAGER, *PPH_LAYOUT_MANAGER;

VOID PhInitializeLayoutManager(
    __out PPH_LAYOUT_MANAGER Manager,
    __in HWND RootWindowHandle
    );

VOID PhDeleteLayoutManager(
    __inout PPH_LAYOUT_MANAGER Manager
    );

PPH_LAYOUT_ITEM PhAddLayoutItem(
    __inout PPH_LAYOUT_MANAGER Manager,
    __in HWND Handle,
    __in PPH_LAYOUT_ITEM ParentItem,
    __in ULONG Anchor
    );

PPH_LAYOUT_ITEM PhAddLayoutItemEx(
    __inout PPH_LAYOUT_MANAGER Manager,
    __in HWND Handle,
    __in PPH_LAYOUT_ITEM ParentItem,
    __in ULONG Anchor,
    __in RECT Margin
    );

VOID PhLayoutManagerLayout(
    __inout PPH_LAYOUT_MANAGER Manager
    );

FORCEINLINE VOID PhResizingMinimumSize(
    __inout PRECT Rect,
    __in WPARAM Edge,
    __in LONG MinimumWidth,
    __in LONG MinimumHeight
    )
{
    if (Edge == WMSZ_BOTTOMRIGHT || Edge == WMSZ_RIGHT || Edge == WMSZ_TOPRIGHT)
    {
        if (Rect->right - Rect->left < MinimumWidth)
            Rect->right = Rect->left + MinimumWidth;
    }
    else if (Edge == WMSZ_BOTTOMLEFT || Edge == WMSZ_LEFT || Edge == WMSZ_TOPLEFT)
    {
        if (Rect->right - Rect->left < MinimumWidth)
            Rect->left = Rect->right - MinimumWidth;
    }

    if (Edge == WMSZ_BOTTOMRIGHT || Edge == WMSZ_BOTTOM || Edge == WMSZ_BOTTOMLEFT)
    {
        if (Rect->bottom - Rect->top < MinimumHeight)
            Rect->bottom = Rect->top + MinimumHeight;
    }
    else if (Edge == WMSZ_TOPRIGHT || Edge == WMSZ_TOP || Edge == WMSZ_TOPLEFT)
    {
        if (Rect->bottom - Rect->top < MinimumHeight)
            Rect->top = Rect->bottom - MinimumHeight;
    }
}

// extlv

typedef COLORREF (NTAPI *PPH_GET_ITEM_COLOR)(
    __in INT Index,
    __in PVOID Param,
    __in PVOID Context
    );

typedef HFONT (NTAPI *PPH_GET_ITEM_FONT)(
    __in INT Index,
    __in PVOID Param,
    __in PVOID Context
    );

VOID PhSetExtendedListView(
    __in HWND hWnd
    );

// max 1117

#define ELVM_ADDFALLBACKCOLUMN (WM_APP + 1106)
#define ELVM_ADDFALLBACKCOLUMNS (WM_APP + 1109)
#define ELVM_INIT (WM_APP + 1102)
#define ELVM_SETCOMPAREFUNCTION (WM_APP + 1104)
#define ELVM_SETCONTEXT (WM_APP + 1103)
#define ELVM_SETCURSOR (WM_APP + 1114)
#define ELVM_SETITEMCOLORFUNCTION (WM_APP + 1111)
#define ELVM_SETITEMFONTFUNCTION (WM_APP + 1117)
#define ELVM_SETNEWCOLOR (WM_APP + 1112)
#define ELVM_SETREDRAW (WM_APP + 1116)
#define ELVM_SETREMOVINGCOLOR (WM_APP + 1113)
#define ELVM_SETSORT (WM_APP + 1108)
#define ELVM_SETSTATEHIGHLIGHTING (WM_APP + 1110)
#define ELVM_SETTRISTATE (WM_APP + 1107)
#define ELVM_SETTRISTATECOMPAREFUNCTION (WM_APP + 1105)
#define ELVM_SORTITEMS (WM_APP + 1101)
#define ELVM_TICK (WM_APP + 1115)

#define ExtendedListView_AddFallbackColumn(hWnd, Column) \
    SendMessage((hWnd), ELVM_ADDFALLBACKCOLUMN, (WPARAM)(Column), 0)
#define ExtendedListView_AddFallbackColumns(hWnd, NumberOfColumns, Columns) \
    SendMessage((hWnd), ELVM_ADDFALLBACKCOLUMNS, (WPARAM)(NumberOfColumns), (LPARAM)(Columns))
#define ExtendedListView_Init(hWnd) \
    SendMessage((hWnd), ELVM_INIT, 0, 0)
#define ExtendedListView_SetCompareFunction(hWnd, Column, CompareFunction) \
    SendMessage((hWnd), ELVM_SETCOMPAREFUNCTION, (WPARAM)(Column), (LPARAM)(CompareFunction))
#define ExtendedListView_SetContext(hWnd, Context) \
    SendMessage((hWnd), ELVM_SETCONTEXT, 0, (LPARAM)(Context))
#define ExtendedListView_SetCursor(hWnd, Cursor) \
    SendMessage((hWnd), ELVM_SETCURSOR, 0, (LPARAM)(Cursor))
#define ExtendedListView_SetItemColorFunction(hWnd, ItemColorFunction) \
    SendMessage((hWnd), ELVM_SETITEMCOLORFUNCTION, 0, (LPARAM)(ItemColorFunction))
#define ExtendedListView_SetItemFontFunction(hWnd, ItemFontFunction) \
    SendMessage((hWnd), ELVM_SETITEMFONTFUNCTION, 0, (LPARAM)(ItemFontFunction))
#define ExtendedListView_SetNewColor(hWnd, NewColor) \
    SendMessage((hWnd), ELVM_SETNEWCOLOR, (WPARAM)(NewColor), 0)
#define ExtendedListView_SetRedraw(hWnd, Redraw) \
    SendMessage((hWnd), ELVM_SETREDRAW, (WPARAM)(Redraw), 0)
#define ExtendedListView_SetRemovingColor(hWnd, RemovingColor) \
    SendMessage((hWnd), ELVM_SETREMOVINGCOLOR, (WPARAM)(RemovingColor), 0)
#define ExtendedListView_SetSort(hWnd, Column, Order) \
    SendMessage((hWnd), ELVM_SETSORT, (WPARAM)(Column), (LPARAM)(Order))
#define ExtendedListView_SetStateHighlighting(hWnd, StateHighlighting) \
    SendMessage((hWnd), ELVM_SETSTATEHIGHLIGHTING, (WPARAM)(StateHighlighting), 0)
#define ExtendedListView_SetTriState(hWnd, TriState) \
    SendMessage((hWnd), ELVM_SETTRISTATE, (WPARAM)(TriState), 0)
#define ExtendedListView_SetTriStateCompareFunction(hWnd, CompareFunction) \
    SendMessage((hWnd), ELVM_SETTRISTATECOMPAREFUNCTION, 0, (LPARAM)(CompareFunction))
#define ExtendedListView_SortItems(hWnd) \
    SendMessage((hWnd), ELVM_SORTITEMS, 0, 0)
#define ExtendedListView_Tick(hWnd) \
    SendMessage((hWnd), ELVM_TICK, 0, 0)

/**
 * Gets the brightness of a color.
 *
 * \param Color The color.
 *
 * \return A value ranging from 0 to 255, 
 * indicating the brightness of the color.
 */
FORCEINLINE ULONG PhGetColorBrightness(
    __in COLORREF Color
    )
{
    ULONG r = Color & 0xff;
    ULONG g = (Color >> 8) & 0xff;
    ULONG b = (Color >> 16) & 0xff;
    ULONG min;
    ULONG max;

    min = r;
    if (g < min) min = g;
    if (b < min) min = b;

    max = r;
    if (g > max) max = g;
    if (b > max) max = b;

    return (min + max) / 2;
}

// support2

VOID PhSearchOnlineString(
    __in HWND hWnd,
    __in PWSTR String
    );

// mainwnd

#ifndef MAINWND_PRIVATE
extern HWND PhMainWndHandle;
extern BOOLEAN PhMainWndExiting;
#endif

#define WM_PH_ACTIVATE (WM_APP + 99)
#define PH_ACTIVATE_REPLY 0x1119

#define WM_PH_SHOW_PROCESS_PROPERTIES (WM_APP + 120)
#define WM_PH_DESTROY (WM_APP + 121)
#define WM_PH_SAVE_ALL_SETTINGS (WM_APP + 122)
#define WM_PH_PREPARE_FOR_EARLY_SHUTDOWN (WM_APP + 123)
#define WM_PH_CANCEL_EARLY_SHUTDOWN (WM_APP + 124)

#define WM_PH_PROCESS_ADDED (WM_APP + 101)
#define WM_PH_PROCESS_MODIFIED (WM_APP + 102)
#define WM_PH_PROCESS_REMOVED (WM_APP + 103)

#define WM_PH_SERVICE_ADDED (WM_APP + 104)
#define WM_PH_SERVICE_MODIFIED (WM_APP + 105)
#define WM_PH_SERVICE_REMOVED (WM_APP + 106)
#define WM_PH_SERVICES_UPDATED (WM_APP + 107)

#define ProcessHacker_ShowProcessProperties(hWnd, ProcessItem) \
    SendMessage(hWnd, WM_PH_SHOW_PROCESS_PROPERTIES, 0, (LPARAM)(ProcessItem))
#define ProcessHacker_Destroy(hWnd) \
    SendMessage(hWnd, WM_PH_DESTROY, 0, 0)
#define ProcessHacker_SaveAllSettings(hWnd) \
    SendMessage(hWnd, WM_PH_SAVE_ALL_SETTINGS, 0, 0)
#define ProcessHacker_PrepareForEarlyShutdown(hWnd) \
    SendMessage(hWnd, WM_PH_PREPARE_FOR_EARLY_SHUTDOWN, 0, 0)
#define ProcessHacker_CancelEarlyShutdown(hWnd) \
    SendMessage(hWnd, WM_PH_CANCEL_EARLY_SHUTDOWN, 0, 0)

BOOLEAN PhMainWndInitialization(
    __in INT ShowCommand
    );

LRESULT CALLBACK PhMainWndProc(      
    __in HWND hWnd,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    );

// procprp

#define PH_PROCESS_PROPCONTEXT_MAXPAGES 20

typedef struct _DLGTEMPLATEEX
{
    WORD dlgVer;
    WORD signature;
    DWORD helpID;
    DWORD exStyle;
    DWORD style;
    WORD cDlgItems;
    short x;
    short y;
    short cx;
    short cy;
} DLGTEMPLATEEX, *PDLGTEMPLATEEX;

typedef struct _PH_PROCESS_PROPCONTEXT
{
    PPH_PROCESS_ITEM ProcessItem;
    HANDLE WindowHandle;
    PH_EVENT CreatedEvent;
    PPH_STRING Title;
    PROPSHEETHEADER PropSheetHeader;
    HPROPSHEETPAGE *PropSheetPages;
} PH_PROCESS_PROPCONTEXT, *PPH_PROCESS_PROPCONTEXT;

typedef struct _PH_PROCESS_PROPPAGECONTEXT
{
    PPH_PROCESS_PROPCONTEXT PropContext;
    PVOID Context;
    PROPSHEETPAGE PropSheetPage;

    BOOLEAN LayoutInitialized;
} PH_PROCESS_PROPPAGECONTEXT, *PPH_PROCESS_PROPPAGECONTEXT;

BOOLEAN PhProcessPropInitialization();

PPH_PROCESS_PROPCONTEXT PhCreateProcessPropContext(
    __in HWND ParentWindowHandle,
    __in PPH_PROCESS_ITEM ProcessItem
    );

VOID PhRefreshProcessPropContext(
    __inout PPH_PROCESS_PROPCONTEXT PropContext
    );

BOOLEAN PhAddProcessPropPage(
    __inout PPH_PROCESS_PROPCONTEXT PropContext,
    __in PPH_PROCESS_PROPPAGECONTEXT PropPageContext
    );

BOOLEAN PhAddProcessPropPage2(
    __inout PPH_PROCESS_PROPCONTEXT PropContext,
    __in HPROPSHEETPAGE PropSheetPageHandle
    );

PPH_PROCESS_PROPPAGECONTEXT PhCreateProcessPropPageContext(
    __in LPCWSTR Template,
    __in DLGPROC DlgProc,
    __in PVOID Context
    );

BOOLEAN PhShowProcessProperties(
    __in PPH_PROCESS_PROPCONTEXT Context
    );

// secedit

typedef NTSTATUS (NTAPI *PPH_GET_OBJECT_SECURITY)(
    __out PSECURITY_DESCRIPTOR *SecurityDescriptor,
    __in SECURITY_INFORMATION SecurityInformation,
    __in PVOID Context
    );

typedef NTSTATUS (NTAPI *PPH_SET_OBJECT_SECURITY)(
    __in PSECURITY_DESCRIPTOR SecurityDescriptor,
    __in SECURITY_INFORMATION SecurityInformation,
    __in PVOID Context
    );

typedef struct _PH_ACCESS_ENTRY
{
    PWSTR Name;
    ACCESS_MASK Access;
    BOOLEAN General;
    BOOLEAN Specific;
    PWSTR ShortName;
} PH_ACCESS_ENTRY, *PPH_ACCESS_ENTRY;

VOID PhSecurityEditorInitialization();

HPROPSHEETPAGE PhCreateSecurityPage(
    __in PWSTR ObjectName,
    __in PPH_GET_OBJECT_SECURITY GetObjectSecurity,
    __in PPH_SET_OBJECT_SECURITY SetObjectSecurity,
    __in PVOID Context,
    __in PPH_ACCESS_ENTRY AccessEntries,
    __in ULONG NumberOfAccessEntries
    );

VOID PhEditSecurity(
    __in HWND hWnd,
    __in PWSTR ObjectName,
    __in PPH_GET_OBJECT_SECURITY GetObjectSecurity,
    __in PPH_SET_OBJECT_SECURITY SetObjectSecurity,
    __in PVOID Context,
    __in PPH_ACCESS_ENTRY AccessEntries,
    __in ULONG NumberOfAccessEntries
    );

typedef NTSTATUS (NTAPI *PPH_OPEN_OBJECT)(
    __out PHANDLE Handle,
    __in ACCESS_MASK DesiredAccess,
    __in PVOID Context
    );

typedef struct _PH_STD_OBJECT_SECURITY
{
    PPH_OPEN_OBJECT OpenObject;
    PWSTR ObjectType;
    PVOID Context;
} PH_STD_OBJECT_SECURITY, *PPH_STD_OBJECT_SECURITY;

FORCEINLINE ACCESS_MASK PhGetAccessForGetSecurity(
    __in SECURITY_INFORMATION SecurityInformation
    )
{
    ACCESS_MASK access = 0;

    if (
        (SecurityInformation & OWNER_SECURITY_INFORMATION) ||
        (SecurityInformation & GROUP_SECURITY_INFORMATION) ||
        (SecurityInformation & DACL_SECURITY_INFORMATION)
        )
    {
        access |= READ_CONTROL;
    }

    if (SecurityInformation & SACL_SECURITY_INFORMATION)
    {
        access |= ACCESS_SYSTEM_SECURITY;
    }

    return access;
}

FORCEINLINE ACCESS_MASK PhGetAccessForSetSecurity(
    __in SECURITY_INFORMATION SecurityInformation
    )
{
    ACCESS_MASK access = 0;

    if (
        (SecurityInformation & OWNER_SECURITY_INFORMATION) ||
        (SecurityInformation & GROUP_SECURITY_INFORMATION)
        )
    {
        access |= WRITE_OWNER;
    }

    if (SecurityInformation & DACL_SECURITY_INFORMATION)
    {
        access |= WRITE_DAC;
    }

    if (SecurityInformation & SACL_SECURITY_INFORMATION)
    {
        access |= ACCESS_SYSTEM_SECURITY;
    }

    return access;
}

__callback NTSTATUS PhStdGetObjectSecurity(
    __out PSECURITY_DESCRIPTOR *SecurityDescriptor,
    __in SECURITY_INFORMATION SecurityInformation,
    __in PVOID Context
    );

__callback NTSTATUS PhStdSetObjectSecurity(
    __in PSECURITY_DESCRIPTOR SecurityDescriptor,
    __in SECURITY_INFORMATION SecurityInformation,
    __in PVOID Context
    );

NTSTATUS PhGetSeObjectSecurity(
    __in HANDLE Handle,
    __in SE_OBJECT_TYPE ObjectType,
    __in SECURITY_INFORMATION SecurityInformation,
    __out PSECURITY_DESCRIPTOR *SecurityDescriptor
    );

NTSTATUS PhSetSeObjectSecurity(
    __in HANDLE Handle,
    __in SE_OBJECT_TYPE ObjectType,
    __in SECURITY_INFORMATION SecurityInformation,
    __in PSECURITY_DESCRIPTOR SecurityDescriptor
    );

// secdata

BOOLEAN PhGetAccessEntries(
    __in PWSTR Type,
    __out PPH_ACCESS_ENTRY *AccessEntries,
    __out PULONG NumberOfAccessEntries
    );

PPH_STRING PhGetAccessString(
    __in ACCESS_MASK Access,
    __in PPH_ACCESS_ENTRY AccessEntries,
    __in ULONG NumberOfAccessEntries
    );

// actions

BOOLEAN PhUiTerminateProcesses(
    __in HWND hWnd,
    __in PPH_PROCESS_ITEM *Processes,
    __in ULONG NumberOfProcesses
    );

BOOLEAN PhUiSuspendProcesses(
    __in HWND hWnd,
    __in PPH_PROCESS_ITEM *Processes,
    __in ULONG NumberOfProcesses
    );

BOOLEAN PhUiResumeProcesses(
    __in HWND hWnd,
    __in PPH_PROCESS_ITEM *Processes,
    __in ULONG NumberOfProcesses
    );

BOOLEAN PhUiRestartProcess(
    __in HWND hWnd,
    __in PPH_PROCESS_ITEM Process
    );

BOOLEAN PhUiReduceWorkingSetProcesses(
    __in HWND hWnd,
    __in PPH_PROCESS_ITEM *Processes,
    __in ULONG NumberOfProcesses
    );

BOOLEAN PhUiSetVirtualizationProcess(
    __in HWND hWnd,
    __in PPH_PROCESS_ITEM Process,
    __in BOOLEAN Enable
    );

BOOLEAN PhUiDetachFromDebuggerProcess(
    __in HWND hWnd,
    __in PPH_PROCESS_ITEM Process
    );

BOOLEAN PhUiInjectDllProcess(
    __in HWND hWnd,
    __in PPH_PROCESS_ITEM Process
    );

BOOLEAN PhUiSetIoPriorityProcess(
    __in HWND hWnd,
    __in PPH_PROCESS_ITEM Process,
    __in ULONG IoPriority
    );

BOOLEAN PhUiSetPriorityProcess(
    __in HWND hWnd,
    __in PPH_PROCESS_ITEM Process,
    __in ULONG PriorityClassWin32
    );

BOOLEAN PhUiStartService(
    __in HWND hWnd,
    __in PPH_SERVICE_ITEM Service
    );

BOOLEAN PhUiContinueService(
    __in HWND hWnd,
    __in PPH_SERVICE_ITEM Service
    );

BOOLEAN PhUiPauseService(
    __in HWND hWnd,
    __in PPH_SERVICE_ITEM Service
    );

BOOLEAN PhUiStopService(
    __in HWND hWnd,
    __in PPH_SERVICE_ITEM Service
    );

BOOLEAN PhUiDeleteService(
    __in HWND hWnd,
    __in PPH_SERVICE_ITEM Service
    );

BOOLEAN PhUiTerminateThreads(
    __in HWND hWnd,
    __in PPH_THREAD_ITEM *Threads,
    __in ULONG NumberOfThreads
    );

BOOLEAN PhUiForceTerminateThreads(
    __in HWND hWnd,
    __in HANDLE ProcessId,
    __in PPH_THREAD_ITEM *Threads,
    __in ULONG NumberOfThreads
    );

BOOLEAN PhUiSuspendThreads(
    __in HWND hWnd,
    __in PPH_THREAD_ITEM *Threads,
    __in ULONG NumberOfThreads
    );

BOOLEAN PhUiResumeThreads(
    __in HWND hWnd,
    __in PPH_THREAD_ITEM *Threads,
    __in ULONG NumberOfThreads
    );

BOOLEAN PhUiSetPriorityThread(
    __in HWND hWnd,
    __in PPH_THREAD_ITEM Thread,
    __in ULONG ThreadPriorityWin32
    );

BOOLEAN PhUiSetIoPriorityThread(
    __in HWND hWnd,
    __in PPH_THREAD_ITEM Thread,
    __in ULONG IoPriority
    );

BOOLEAN PhUiUnloadModule(
    __in HWND hWnd,
    __in HANDLE ProcessId,
    __in PPH_MODULE_ITEM Module
    );

BOOLEAN PhUiCloseHandles(
    __in HWND hWnd,
    __in HANDLE ProcessId,
    __in PPH_HANDLE_ITEM *Handles,
    __in ULONG NumberOfHandles,
    __in BOOLEAN Warn
    );

BOOLEAN PhUiSetAttributesHandle(
    __in HWND hWnd,
    __in HANDLE ProcessId,
    __in PPH_HANDLE_ITEM Handle,
    __in ULONG Attributes
    );

// cmdmode

NTSTATUS PhCommandModeStart();

// anawait

VOID PhUiAnalyzeWaitThread(
    __in HWND hWnd,
    __in HANDLE ProcessId,
    __in HANDLE ThreadId,
    __in PPH_SYMBOL_PROVIDER SymbolProvider
    );

// mdump

BOOLEAN PhUiCreateDumpFileProcess(
    __in HWND hWnd,
    __in PPH_PROCESS_ITEM Process
    );

// about

VOID PhShowAboutDialog(
    __in HWND ParentWindowHandle
    );

PPH_STRING PhGetDiagnosticsString();

// findobj

VOID PhShowFindObjectsDialog();

// heapinfo

VOID PhShowProcessHeapsDialog(
    __in HWND ParentWindowHandle,
    __in PPH_PROCESS_ITEM ProcessItem
    );

NTSTATUS PhGetProcessDefaultHeap(
    __in HANDLE ProcessHandle,
    __out PPVOID Heap
    );

// hidnproc

VOID PhShowHiddenProcessesDialog();

// hndlprp

VOID PhShowHandleProperties(
    __in HWND ParentWindowHandle,
    __in HANDLE ProcessId,
    __in PPH_HANDLE_ITEM HandleItem
    );

// infodlg

VOID PhShowInformationDialog(
    __in HWND ParentWindowHandle,
    __in PWSTR String
    );

// pagfiles

VOID PhShowPagefilesDialog(
    __in HWND ParentWindowHandle
    );

// runas

VOID PhShowRunAsDialog(
    __in HWND ParentWindowHandle,
    __in_opt HANDLE ProcessId
    );

NTSTATUS PhRunAsCommandStart(
    __in PWSTR ServiceCommandLine,
    __in PWSTR ServiceName
    );

NTSTATUS PhRunAsCommandStart2(
    __in HWND hWnd,
    __in PWSTR Program,
    __in_opt PWSTR UserName,
    __in_opt PWSTR Password,
    __in_opt ULONG LogonType,
    __in_opt HANDLE ProcessIdWithToken,
    __in ULONG SessionId
    );

VOID PhRunAsServiceStart();

NTSTATUS PhCreateProcessAsUser(
    __in_opt PWSTR ApplicationName,
    __in_opt PWSTR CommandLine,
    __in_opt PWSTR CurrentDirectory,
    __in_opt PVOID Environment,
    __in_opt PWSTR DomainName,
    __in_opt PWSTR UserName,
    __in_opt PWSTR Password,
    __in_opt ULONG LogonType,
    __in_opt HANDLE ProcessIdWithToken,
    __in ULONG SessionId,
    __out_opt PHANDLE ProcessHandle,
    __out_opt PHANDLE ThreadHandle
    );

// srvprp

VOID PhShowServiceProperties(
    __in HWND ParentWindowHandle,
    __in PPH_SERVICE_ITEM ServiceItem
    );

// termator

VOID PhShowProcessTerminatorDialog(
    __in HWND ParentWindowHandle,
    __in PPH_PROCESS_ITEM ProcessItem
    );

// thrdstk

VOID PhShowThreadStackDialog(
    __in HWND ParentWindowHandle,
    __in HANDLE ProcessId,
    __in HANDLE ThreadId,
    __in PPH_SYMBOL_PROVIDER SymbolProvider
    );

// tokprp

PPH_STRING PhGetGroupAttributesString(
    __in ULONG Attributes
    );

PWSTR PhGetPrivilegeAttributesString(
    __in ULONG Attributes
    );

VOID PhShowTokenProperties(
    __in HWND ParentWindowHandle,
    __in PPH_OPEN_OBJECT OpenObject,
    __in PVOID Context,
    __in_opt PWSTR Title
    );

HPROPSHEETPAGE PhCreateTokenPage(
    __in PPH_OPEN_OBJECT OpenObject,
    __in PVOID Context,
    __in_opt DLGPROC HookProc
    );

#endif