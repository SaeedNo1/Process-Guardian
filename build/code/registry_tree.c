#define _WIN32_IE 0x0500
#define UNICODE
#define _UNICODE
#include "registry_tree.h"
#include <commctrl.h>
#include <windowsx.h>

/* Forward declare registry_manager functions */
extern int GetRegistryValues(HKEY rootKey, const wchar_t *subKey,
                              wchar_t ***outNames, DWORD **outTypes,
                              BYTE ***outData, DWORD **outDataSizes);
extern BOOL SetRegistryValue(HKEY rootKey, const wchar_t *subKey,
                              const wchar_t *valueName, DWORD type,
                              const BYTE *data, DWORD dataSize);
extern BOOL CreateRegistryKey(HKEY rootKey, const wchar_t *subKey);
extern HKEY GetRootKeyFromString(const wchar_t *str);
extern BOOL AddToProtectedList(const wchar_t *fullPath);
extern BOOL RemoveFromProtectedList(const wchar_t *fullPath);
extern BOOL IsRegistryProtected(const wchar_t *fullPath);
extern int GetProtectedList(wchar_t ***outPaths);
extern int GetRepeatedDeleteList(wchar_t ***outPaths);
extern BOOL IsRegistryRepeatedDelete(const wchar_t *fullPath);
extern void AddToRepeatedDeleteList(const wchar_t *fullPath);
extern void RemoveFromRepeatedDeleteList(const wchar_t *fullPath);
extern void SaveProtectedConfig(void);
extern void LoadProtectedConfig(void);
extern void SaveRepeatedDeleteConfig(void);
extern void LoadRepeatedDeleteConfig(void);

static HWND g_hTree = NULL;

static const wchar_t *g_rootNames[] = {
    L"HKEY_LOCAL_MACHINE",
    L"HKEY_CURRENT_USER",
    L"HKEY_CLASSES_ROOT",
    L"HKEY_USERS",
    L"HKEY_CURRENT_CONFIG"
};
static HKEY g_rootKeys[] = {
    HKEY_LOCAL_MACHINE,
    HKEY_CURRENT_USER,
    HKEY_CLASSES_ROOT,
    HKEY_USERS,
    HKEY_CURRENT_CONFIG
};
static int g_rootCount = 5;

static void FreeNodeData(HTREEITEM hItem) {
    TVITEMEXW tviex = {0};
    tviex.hItem = hItem;
    tviex.mask = TVIF_PARAM;
    if (TreeView_GetItem(g_hTree, &tviex) && tviex.lParam) {
        RegTreeNodeData *data = (RegTreeNodeData*)tviex.lParam;
        if (data->fullPath) free(data->fullPath);
        free(data);
        tviex.lParam = 0;
        TreeView_SetItem(g_hTree, &tviex);
    }
    HTREEITEM hChild = TreeView_GetChild(g_hTree, hItem);
    while (hChild) {
        HTREEITEM hNext = TreeView_GetNextSibling(g_hTree, hChild);
        FreeNodeData(hChild);
        hChild = hNext;
    }
}

static HKEY GetRootKeyFromPath(const wchar_t *fullPath) {
    wchar_t rootName[64];
    const wchar_t *p = wcschr(fullPath, L'\\');
    int len = p ? (int)(p - fullPath) : (int)wcslen(fullPath);
    if (len > 63) len = 63;
    wcsncpy(rootName, fullPath, len);
    rootName[len] = L'\0';
    for (int i = 0; i < g_rootCount; i++) {
        if (wcscmp(rootName, g_rootNames[i]) == 0)
            return g_rootKeys[i];
    }
    return NULL;
}

static wchar_t *GetSubPath(const wchar_t *fullPath) {
    const wchar_t *p = wcschr(fullPath, L'\\');
    if (!p) return NULL;
    return wcsdup(p + 1);
}

void RT_InitTreeView(HWND hTree) {
    g_hTree = hTree;
    ShowWindow(g_hTree, SW_HIDE);
    TreeView_DeleteAllItems(g_hTree);

    TVINSERTSTRUCTW tvis = {0};
    tvis.hParent = TVI_ROOT;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
    tvis.item.cChildren = 1;

    for (int i = 0; i < g_rootCount; i++) {
        RegTreeNodeData *data = (RegTreeNodeData*)malloc(sizeof(RegTreeNodeData));
        if (!data) continue;
        data->fullPath = wcsdup(g_rootNames[i]);
        if (!data->fullPath) { free(data); continue; }
        data->childrenAdded = FALSE;
        tvis.item.pszText = (wchar_t*)g_rootNames[i];
        tvis.item.lParam = (LPARAM)data;
        TreeView_InsertItem(g_hTree, &tvis);
    }
    ShowWindow(g_hTree, SW_SHOW);
}

void RT_OnNotify(NMHDR *pnmh) {
    if (pnmh->code == NM_RCLICK) {
        /* TreeView 右键：通过 NM_RCLICK 通知获取屏幕坐标 */
        DWORD dwPos = GetMessagePos();
        POINT pt;
        pt.x = (int)(short)LOWORD(dwPos);
        pt.y = (int)(short)HIWORD(dwPos);
        HWND hParent = GetParent(g_hTree);
        RT_OnContextMenu(hParent, g_hTree, pt, NULL);
        return;
    }
    if (pnmh->code != TVN_ITEMEXPANDINGW) return;
    NMTREEVIEWW *pnmv = (NMTREEVIEWW*)pnmh;
    if (pnmv->action != TVE_EXPAND) return;

    HTREEITEM hItem = pnmv->itemNew.hItem;
    TVITEMEXW tviex = {0};
    tviex.hItem = hItem;
    tviex.mask = TVIF_PARAM;
    if (!TreeView_GetItem(g_hTree, &tviex)) return;
    RegTreeNodeData *data = (RegTreeNodeData*)tviex.lParam;
    if (!data || data->childrenAdded) return;

    HKEY hRoot = GetRootKeyFromPath(data->fullPath);
    if (!hRoot) return;

    wchar_t *subPath = GetSubPath(data->fullPath);
    HKEY hKey = NULL;
    LONG res;
    if (subPath) {
        res = RegOpenKeyExW(hRoot, subPath, 0, KEY_READ, &hKey);
        free(subPath);
        if (res != ERROR_SUCCESS) return;
    } else {
        res = RegOpenKeyExW(hRoot, NULL, 0, KEY_READ, &hKey);
        if (res != ERROR_SUCCESS) return;
    }

    TVINSERTSTRUCTW tvis = {0};
    tvis.hParent = hItem;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
    tvis.item.cChildren = 1;

    DWORD index = 0;
    wchar_t keyName[256];
    DWORD keyNameSize;
    FILETIME ft;
    while (1) {
        keyNameSize = 256;
        res = RegEnumKeyExW(hKey, index, keyName, &keyNameSize, NULL, NULL, NULL, &ft);
        if (res != ERROR_SUCCESS) break;

        wchar_t *childPath = (wchar_t*)malloc(512 * sizeof(wchar_t));
        if (!childPath) { index++; continue; }
        swprintf(childPath, 512, L"%s\\%s", data->fullPath, keyName);

        RegTreeNodeData *childData = (RegTreeNodeData*)malloc(sizeof(RegTreeNodeData));
        if (!childData) { free(childPath); index++; continue; }
        childData->fullPath = childPath;
        childData->childrenAdded = FALSE;

        tvis.item.pszText = keyName;
        tvis.item.lParam = (LPARAM)childData;
        TreeView_InsertItem(g_hTree, &tvis);
        index++;
    }
    RegCloseKey(hKey);
    data->childrenAdded = TRUE;
}

wchar_t *RT_GetSelectedPath(HWND hTree) {
    HTREEITEM hSel = TreeView_GetSelection(hTree);
    if (!hSel) return NULL;
    TVITEMEXW tviex = {0};
    tviex.hItem = hSel;
    tviex.mask = TVIF_PARAM;
    if (!TreeView_GetItem(hTree, &tviex)) return NULL;
    RegTreeNodeData *data = (RegTreeNodeData*)tviex.lParam;
    if (!data) return NULL;
    return wcsdup(data->fullPath);
}

/* ========== Edit Value Dialog ========== */
typedef struct {
    HKEY hRoot;
    wchar_t *subKey;
    wchar_t *valueName;       /* may be empty for default value */
    DWORD type;
    BYTE *data;
    DWORD dataSize;
    BOOL modified;
} EditValueData;

static BOOL PromptForType(HWND hwnd, DWORD *type) {
    /* Simple dialog: radio buttons for REG_SZ / REG_DWORD / REG_BINARY */
    /* For simplicity, just return type unchanged. */
    return TRUE;
}

static INT_PTR CALLBACK EditValueDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static EditValueData *evd = NULL;
    static HWND hEdit = NULL;

    switch (msg) {
    case WM_INITDIALOG: {
        evd = (EditValueData*)lParam;
        hEdit = GetDlgItem(hDlg, 1001);

        /* Show value name */
        SetDlgItemTextW(hDlg, 1000, evd->valueName ? evd->valueName : L"(Default)");

        wchar_t typeStr[64];
        switch (evd->type) {
            case REG_SZ: wcscpy(typeStr, L"REG_SZ"); break;
            case REG_DWORD: wcscpy(typeStr, L"REG_DWORD"); break;
            case REG_BINARY: wcscpy(typeStr, L"REG_BINARY"); break;
            case REG_MULTI_SZ: wcscpy(typeStr, L"REG_MULTI_SZ"); break;
            case REG_EXPAND_SZ: wcscpy(typeStr, L"REG_EXPAND_SZ"); break;
            default: swprintf(typeStr, 64, L"0x%x", evd->type); break;
        }
        SetDlgItemTextW(hDlg, 1002, typeStr);

        /* Fill edit box with current value */
        if (evd->type == REG_SZ || evd->type == REG_EXPAND_SZ) {
            SetWindowTextW(hEdit, (wchar_t*)evd->data);
        } else if (evd->type == REG_DWORD && evd->dataSize >= 4) {
            wchar_t buf[32];
            swprintf(buf, 32, L"%lu", *(DWORD*)evd->data);
            SetWindowTextW(hEdit, buf);
        } else {
            /* Binary: show as hex */
            wchar_t *hex = (wchar_t*)malloc(evd->dataSize * 3 + 1);
            for (DWORD i = 0; i < evd->dataSize; i++) {
                swprintf(hex + i*3, 4, L"%02x ", evd->data[i]);
            }
            SetWindowTextW(hEdit, hex);
            free(hex);
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[8192];
            GetWindowTextW(hEdit, buf, 8192);
            int len = wcslen(buf);

            if (evd->type == REG_SZ || evd->type == REG_EXPAND_SZ) {
                evd->data = (BYTE*)malloc((len + 1) * 2);
                wcscpy((wchar_t*)evd->data, buf);
                evd->dataSize = (len + 1) * 2;
            } else if (evd->type == REG_DWORD) {
                DWORD v = _wtoi(buf);
                evd->data = (BYTE*)malloc(4);
                *(DWORD*)evd->data = v;
                evd->dataSize = 4;
            } else {
                /* parse hex */
                evd->data = (BYTE*)malloc(4096);
                evd->dataSize = 0;
                wchar_t *p = buf;
                while (*p && evd->dataSize < 4096) {
                    while (*p == L' ') p++;
                    if (!*p) break;
                    int byte;
                    swscanf(p, L"%02x", &byte);
                    evd->data[evd->dataSize++] = (BYTE)byte;
                    while (*p && *p != L' ') p++;
                }
            }
            evd->modified = TRUE;
            EndDialog(hDlg, IDOK);
        } else if (LOWORD(wParam) == IDCANCEL) {
            evd->modified = FALSE;
            EndDialog(hDlg, IDCANCEL);
        }
        break;
    }
    return FALSE;
}

static BOOL EditOneValue(HWND hwndParent, HKEY hRoot, const wchar_t *subKey,
                          const wchar_t *valueName, DWORD type,
                          const BYTE *oldData, DWORD oldSize) {
    EditValueData evd = {0};
    evd.hRoot = hRoot;
    evd.subKey = wcsdup(subKey);
    evd.valueName = wcsdup(valueName ? valueName : L"");
    evd.type = type;
    if (oldData && oldSize > 0) {
        evd.data = (BYTE*)malloc(oldSize);
        memcpy(evd.data, oldData, oldSize);
        evd.dataSize = oldSize;
    }
    evd.modified = FALSE;

    INT_PTR res = DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(101),
                                   hwndParent, EditValueDlgProc, (LPARAM)&evd);
    if (res == IDOK && evd.modified) {
        BOOL ok = SetRegistryValue(hRoot, subKey, valueName, type, evd.data, evd.dataSize);
        free(evd.data);
        free(evd.subKey);
        free(evd.valueName);
        return ok;
    }
    free(evd.data);
    free(evd.subKey);
    free(evd.valueName);
    return FALSE;
}

/* ========== New Key Dialog ========== */
static INT_PTR CALLBACK NewKeyDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static wchar_t *outName = NULL;
    switch (msg) {
    case WM_INITDIALOG:
        outName = (wchar_t*)lParam;
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            GetDlgItemTextW(hDlg, 1000, outName, 256);
            EndDialog(hDlg, IDOK);
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        break;
    }
    return FALSE;
}

static BOOL PromptNewKeyName(HWND hwndParent, wchar_t *outName) {
    return DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(102),
                           hwndParent, NewKeyDlgProc, (LPARAM)outName) == IDOK;
}

/* ========== Main "Edit registry key" dialog ========== */
typedef struct {
    HKEY hRoot;
    wchar_t *fullPath;        /* root\subkey */
    wchar_t *subKey;          /* subkey only */
    wchar_t **valueNames;
    DWORD *valueTypes;
    BYTE **valueData;
    DWORD *valueSizes;
    int valueCount;
} KeyValuesData;

static INT_PTR CALLBACK KeyValuesDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static KeyValuesData *kvd = NULL;
    static HWND hList = NULL;

    switch (msg) {
    case WM_INITDIALOG: {
        kvd = (KeyValuesData*)lParam;
        hList = GetDlgItem(hDlg, 1003);

        /* ListView columns */
        LVCOLUMNW col = {0};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = (wchar_t*)L"Name"; col.cx = 200;
        ListView_InsertColumn(hList, 0, &col);
        col.pszText = (wchar_t*)L"Type"; col.cx = 80;
        ListView_InsertColumn(hList, 1, &col);
        col.pszText = (wchar_t*)L"Value"; col.cx = 350;
        ListView_InsertColumn(hList, 2, &col);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT);

        /* Title */
        wchar_t title[1024];
        swprintf(title, 1024, L"Edit - %s", kvd->fullPath);
        SetWindowTextW(hDlg, title);

        /* Fill values */
        for (int i = 0; i < kvd->valueCount; i++) {
            LVITEMW lvi = {0};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.pszText = kvd->valueNames[i];
            ListView_InsertItem(hList, &lvi);

            wchar_t typeStr[32];
            switch (kvd->valueTypes[i]) {
                case REG_SZ: wcscpy(typeStr, L"REG_SZ"); break;
                case REG_DWORD: wcscpy(typeStr, L"REG_DWORD"); break;
                case REG_BINARY: wcscpy(typeStr, L"REG_BINARY"); break;
                case REG_MULTI_SZ: wcscpy(typeStr, L"REG_MULTI_SZ"); break;
                case REG_EXPAND_SZ: wcscpy(typeStr, L"REG_EXPAND_SZ"); break;
                default: swprintf(typeStr, 32, L"0x%x", kvd->valueTypes[i]); break;
            }
            lvi.iSubItem = 1;
            lvi.pszText = typeStr;
            ListView_SetItem(hList, &lvi);

            wchar_t valStr[2048] = L"";
            if (kvd->valueTypes[i] == REG_SZ || kvd->valueTypes[i] == REG_EXPAND_SZ) {
                wcsncpy(valStr, (wchar_t*)kvd->valueData[i], 2047);
            } else if (kvd->valueTypes[i] == REG_DWORD && kvd->valueSizes[i] >= 4) {
                swprintf(valStr, 32, L"%lu", *(DWORD*)kvd->valueData[i]);
            } else {
                for (DWORD j = 0; j < kvd->valueSizes[i] && j < 32; j++) {
                    wchar_t tmp[8];
                    swprintf(tmp, 8, L"%02x ", kvd->valueData[i][j]);
                    wcscat(valStr, tmp);
                }
            }
            lvi.iSubItem = 2;
            lvi.pszText = valStr;
            ListView_SetItem(hList, &lvi);
        }
        return TRUE;
    }
    case WM_COMMAND: {
        DWORD zero_v = 0;
        if (LOWORD(wParam) == 1004) {
            /* Edit selected */
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < kvd->valueCount) {
                EditOneValue(hDlg, kvd->hRoot, kvd->subKey,
                             kvd->valueNames[sel], kvd->valueTypes[sel],
                             kvd->valueData[sel], kvd->valueSizes[sel]);
                EndDialog(hDlg, IDOK);
            }
        } else if (LOWORD(wParam) == 1005) {
            /* New DWORD */
            wchar_t name[256] = L"NewDWORD";
            if (SetRegistryValue(kvd->hRoot, kvd->subKey, name, REG_DWORD, (BYTE*)&zero_v, 4)) {
                EndDialog(hDlg, IDOK);
            }
        } else if (LOWORD(wParam) == 1006) {
            /* New String */
            wchar_t name[256] = L"NewString";
            wchar_t empty[2] = L"";
            if (SetRegistryValue(kvd->hRoot, kvd->subKey, name, REG_SZ, (BYTE*)empty, 2)) {
                EndDialog(hDlg, IDOK);
            }
        } else if (LOWORD(wParam) == 1007) {
            /* Delete selected */
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < kvd->valueCount) {
                wchar_t fullKey[1024];
                swprintf(fullKey, 1024, L"%s\\%s", kvd->fullPath, kvd->valueNames[sel]);
                extern BOOL DeleteRegistryEntry(HKEY, const wchar_t*, const wchar_t*);
                DeleteRegistryEntry(kvd->hRoot, kvd->subKey, kvd->valueNames[sel]);
                EndDialog(hDlg, IDOK);
            }
        } else if (LOWORD(wParam) == IDCANCEL || LOWORD(wParam) == IDOK) {
            EndDialog(hDlg, IDCANCEL);
        }
        break;
    case WM_NOTIFY: {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->hwndFrom == hList && pnmh->code == NM_DBLCLK) {
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < kvd->valueCount) {
                EditOneValue(hDlg, kvd->hRoot, kvd->subKey,
                             kvd->valueNames[sel], kvd->valueTypes[sel],
                             kvd->valueData[sel], kvd->valueSizes[sel]);
                EndDialog(hDlg, IDOK);
            }
        }
        break;
    }
    }
    return FALSE;
}
    return FALSE;
}

/* New value name dialog */
static INT_PTR CALLBACK NewValueNameDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static wchar_t *outName = NULL;
    switch (msg) {
    case WM_INITDIALOG:
        outName = (wchar_t*)lParam;
        SetDlgItemTextW(hDlg, 1000, L"");
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            GetDlgItemTextW(hDlg, 1000, outName, 256);
            EndDialog(hDlg, IDOK);
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        break;
    }
    return FALSE;
}

BOOL PromptForNewValueName(HWND hwndParent, wchar_t *outName, int outSize,
                           const wchar_t *title, const wchar_t *defaultName) {
    /* Use a generic dialog with text input */
    outName[0] = 0;
    /* Just use a simple inline dialog */
    /* For now, return FALSE - this will be handled in main flow */
    return FALSE;
}

BOOL RT_EditKey(HWND hwndParent, const wchar_t *fullPath) {
    HKEY hRoot = GetRootKeyFromPath(fullPath);
    if (!hRoot) return FALSE;
    wchar_t *subKey = GetSubPath(fullPath);
    if (!subKey) return FALSE;  /* root key */

    wchar_t **names = NULL;
    DWORD *types = NULL;
    BYTE **datas = NULL;
    DWORD *sizes = NULL;
    int count = GetRegistryValues(hRoot, subKey, &names, &types, &datas, &sizes);

    KeyValuesData kvd = {0};
    kvd.hRoot = hRoot;
    kvd.fullPath = wcsdup(fullPath);
    kvd.subKey = wcsdup(subKey);
    kvd.valueNames = names;
    kvd.valueTypes = types;
    kvd.valueData = datas;
    kvd.valueSizes = sizes;
    kvd.valueCount = count;

    DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(100),
                    hwndParent, KeyValuesDlgProc, (LPARAM)&kvd);

    free(kvd.fullPath);
    free(kvd.subKey);
    for (int i = 0; i < count; i++) {
        free(names[i]);
        free(datas[i]);
    }
    free(names); free(types); free(datas); free(sizes);
    free(subKey);
    return TRUE;
}

BOOL RT_DeleteKey(HWND hwndParent, const wchar_t *fullPath) {
    HKEY hRoot = GetRootKeyFromPath(fullPath);
    if (!hRoot) return FALSE;
    wchar_t *subKey = GetSubPath(fullPath);
    if (!subKey) {
        MessageBoxW(hwndParent, L"Cannot delete root key", L"Error", MB_OK);
        return FALSE;
    }

    wchar_t msg[1024];
    swprintf(msg, 1024, L"Delete this key and all its subkeys?\n\n%s", fullPath);
    if (MessageBoxW(hwndParent, msg, L"Confirm Delete", MB_YESNO | MB_ICONWARNING) != IDYES) {
        free(subKey);
        return FALSE;
    }
    extern BOOL DeleteRegistryEntry(HKEY, const wchar_t*, const wchar_t*);
    BOOL ok = DeleteRegistryEntry(hRoot, subKey, NULL);
    free(subKey);
    return ok;
}

BOOL RT_AddToRepeated(HWND hwndParent, const wchar_t *fullPath) {
    AddToRepeatedDeleteList(fullPath);
    MessageBoxW(hwndParent, L"Added to repeated delete list. guardiand will delete it whenever it appears.",
                L"Done", MB_OK);
    return TRUE;
}

BOOL RT_AddToProtected(HWND hwndParent, const wchar_t *fullPath) {
    /* If already protected, treat as unprotect */
    if (IsRegistryProtected(fullPath)) {
        if (MessageBoxW(hwndParent, L"This key is already protected. Unprotect it?",
                        L"Confirm", MB_YESNO) == IDYES) {
            RemoveFromProtectedList(fullPath);
            MessageBoxW(hwndParent, L"Removed from protected list.", L"Done", MB_OK);
        }
        return TRUE;
    }
    if (!AddToProtectedList(fullPath)) {
        MessageBoxW(hwndParent, L"Failed to save snapshot (check permissions).",
                    L"Error", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    MessageBoxW(hwndParent, L"Protected. guardiand will restore it whenever modified.",
                L"Done", MB_OK);
    return TRUE;
}

/* ========== Right-click menu ========== */
BOOL RT_OnContextMenu(HWND hwndParent, HWND hTree, POINT pt,
                      void (*addRegRepeated)(const wchar_t*)) {
    HTREEITEM hSel = TreeView_GetSelection(hTree);
    if (!hSel) return FALSE;
    wchar_t *regPath = RT_GetSelectedPath(hTree);
    if (!regPath) return FALSE;

    /* If user-supplied callback (legacy) is provided, use old menu */
    if (addRegRepeated) {
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, 25, L"Edit");
        AppendMenuW(hMenu, MF_STRING, 30, L"Protect");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, 20, L"Delete");
        AppendMenuW(hMenu, MF_STRING, 21, L"Repeated Delete");
        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, pt.x, pt.y, 0, hwndParent, NULL);
        if (cmd == 25) {
            RT_EditKey(hwndParent, regPath);
        } else if (cmd == 30) {
            RT_AddToProtected(hwndParent, regPath);
        } else if (cmd == 20) {
            RT_DeleteKey(hwndParent, regPath);
        } else if (cmd == 21) {
            addRegRepeated(regPath);
        }
        DestroyMenu(hMenu);
    } else {
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, 25, L"Edit");
        AppendMenuW(hMenu, MF_STRING, 30, L"Protect");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, 20, L"Delete");
        AppendMenuW(hMenu, MF_STRING, 21, L"Repeated Delete");
        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, pt.x, pt.y, 0, hwndParent, NULL);
        if (cmd == 25) {
            RT_EditKey(hwndParent, regPath);
        } else if (cmd == 30) {
            RT_AddToProtected(hwndParent, regPath);
        } else if (cmd == 20) {
            RT_DeleteKey(hwndParent, regPath);
        } else if (cmd == 21) {
            RT_AddToRepeated(hwndParent, regPath);
        }
        DestroyMenu(hMenu);
    }

    free(regPath);
    return TRUE;
}

void RT_Show(BOOL show) {
    if (g_hTree) ShowWindow(g_hTree, show ? SW_SHOW : SW_HIDE);
}

void RT_Free(void) {
    if (!g_hTree) return;
    HTREEITEM hRoot = TreeView_GetRoot(g_hTree);
    while (hRoot) {
        HTREEITEM hNext = TreeView_GetNextSibling(g_hTree, hRoot);
        FreeNodeData(hRoot);
        hRoot = hNext;
    }
    TreeView_DeleteAllItems(g_hTree);
}
