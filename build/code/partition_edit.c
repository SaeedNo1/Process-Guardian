#include "partition_edit.h"
#include <stdio.h>

#define SECTOR_SIZE 512

HANDLE PE_OpenDrive(int driveNum, BOOL writable) {
    wchar_t path[64];
    swprintf(path, 64, L"\\\\.\\PhysicalDrive%d", driveNum);
    HANDLE h = CreateFileW(path,
                           writable ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    return h;
}

BOOL PE_ReadSector(HANDLE hDrive, ULONG64 sectorNum, BYTE *buffer) {
    LARGE_INTEGER li;
    li.QuadPart = sectorNum * SECTOR_SIZE;
    OVERLAPPED ov = {0};
    ov.Offset = li.LowPart;
    ov.OffsetHigh = li.HighPart;
    DWORD read = 0;
    return ReadFile(hDrive, buffer, SECTOR_SIZE, &read, &ov) && read == SECTOR_SIZE;
}

BOOL PE_WriteSector(HANDLE hDrive, ULONG64 sectorNum, BYTE *buffer) {
    LARGE_INTEGER li;
    li.QuadPart = sectorNum * SECTOR_SIZE;
    OVERLAPPED ov = {0};
    ov.Offset = li.LowPart;
    ov.OffsetHigh = li.HighPart;
    DWORD written = 0;
    return WriteFile(hDrive, buffer, SECTOR_SIZE, &written, &ov) && written == SECTOR_SIZE;
}

void PE_CloseDrive(HANDLE hDrive) {
    if (hDrive != INVALID_HANDLE_VALUE) CloseHandle(hDrive);
}

int PE_GetPartitionTableType(HANDLE hDrive) {
    BYTE buf[SECTOR_SIZE] = {0};
    if (!PE_ReadSector(hDrive, 0, buf)) return 0;
    if (buf[510] == 0x55 && buf[511] == 0xAA) {
        MBR *mbr = (MBR*)buf;
        if (mbr->partitions[0].partitionType == 0xEE) {
            BYTE buf2[SECTOR_SIZE] = {0};
            if (PE_ReadSector(hDrive, 1, buf2)) {
                if (memcmp(buf2, "EFI PART", 8) == 0) return 2;
            }
        }
        return 1;
    }
    return 0;
}

BOOL PE_ReadMBR(HANDLE hDrive, MBR *mbr) {
    return PE_ReadSector(hDrive, 0, (BYTE*)mbr);
}

BOOL PE_WriteMBR(HANDLE hDrive, MBR *mbr) {
    return PE_WriteSector(hDrive, 0, (BYTE*)mbr);
}

BOOL PE_ReadGPTHeader(HANDLE hDrive, GPTHeader *gpt) {
    return PE_ReadSector(hDrive, 1, (BYTE*)gpt);
}

static DWORD Crc32(const BYTE *data, int len) {
    DWORD crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
    }
    return ~crc;
}

BOOL PE_WriteGPTHeader(HANDLE hDrive, GPTHeader *gpt) {
    gpt->headerCRC32 = 0;
    gpt->headerCRC32 = Crc32((BYTE*)gpt, gpt->headerSize);
    return PE_WriteSector(hDrive, 1, (BYTE*)gpt);
}

BOOL PE_ConvertMBRToGPT(HANDLE hDrive) {
    MBR mbr;
    if (!PE_ReadMBR(hDrive, &mbr)) return FALSE;
    BYTE buf[SECTOR_SIZE] = {0};
    GPTHeader *gpt = (GPTHeader*)buf;
    memcpy(gpt->signature, "EFI PART", 8);
    gpt->revision = 0x00010000U;
    gpt->headerSize = 92;
    gpt->currentLBA = 1;
    gpt->firstUsableLBA = 34;
    memset(gpt->reserved2, 0, sizeof(gpt->reserved2));
    return PE_WriteSector(hDrive, 1, buf);
}

BOOL PE_ConvertGPTToMBR(HANDLE hDrive) {
    GPTHeader gpt;
    if (!PE_ReadGPTHeader(hDrive, &gpt)) return FALSE;
    MBR mbr = {0};
    mbr.signature = 0xAA55;
    mbr.partitions[0].partitionType = 0xEE;
    mbr.partitions[0].startLBA = 1;
    memset(mbr.code, 0, sizeof(mbr.code));
    return PE_WriteMBR(hDrive, &mbr);
}

BOOL PE_GetDiskSize(HANDLE hDrive, ULONG64 *totalSectors) {
    DWORD bytesRet = 0;
    DISK_GEOMETRY_EX geo;
    if (!DeviceIoControl(hDrive, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                         NULL, 0, &geo, sizeof(geo), &bytesRet, NULL)) {
        *totalSectors = 0;
        return FALSE;
    }
    *totalSectors = geo.DiskSize.QuadPart / SECTOR_SIZE;
    return TRUE;
}

/* =================== Hex Editor Dialog =================== */
#include <windowsx.h>
#include <commctrl.h>

typedef struct {
    int diskNumber;
    BYTE *sectorData;       /* 512 bytes (caller-managed) */
    ULONG64 sectorNum;
    BOOL modified;
    HWND hList;             /* ListView to display hex */
    HWND hEdit;             /* Edit box for byte value */
    int selectedIndex;      /* selected byte index in sector (0-511), -1 = none */
} HexEditData;

static void UpdateHexView(HexEditData *hd) {
    /* Clear and refill ListView */
    ListView_DeleteAllItems(hd->hList);
    /* Insert 32 rows of 16 bytes each */
    for (int row = 0; row < 32; row++) {
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = row;
        lvi.iSubItem = 0;

        wchar_t offset[16];
        swprintf(offset, 16, L"%04X", row * 16);
        lvi.pszText = offset;
        ListView_InsertItem(hd->hList, &lvi);

        for (int col = 0; col < 16; col++) {
            int idx = row * 16 + col;
            wchar_t hex[8];
            swprintf(hex, 8, L"%02X", hd->sectorData[idx]);
            lvi.iSubItem = 1 + col;
            lvi.pszText = hex;
            ListView_SetItem(hd->hList, &lvi);
        }
        /* ASCII column (col 17) */
        wchar_t ascii[17] = {0};
        for (int col = 0; col < 16; col++) {
            BYTE b = hd->sectorData[row * 16 + col];
            ascii[col] = (b >= 32 && b < 127) ? (wchar_t)b : L'.';
        }
        lvi.iSubItem = 17;
        lvi.pszText = ascii;
        ListView_SetItem(hd->hList, &lvi);
    }
}

static INT_PTR CALLBACK HexEditDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    HexEditData *hd = (HexEditData*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
    HWND hList = NULL;

    if (msg == WM_INITDIALOG) {
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)lParam);
        hd = (HexEditData*)lParam;
        hList = GetDlgItem(hDlg, 1001);
        hd->hList = hList;
        hd->selectedIndex = -1;

        /* Setup ListView columns: offset, 16 hex columns, ASCII */
        LVCOLUMNW col = {0};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.fmt = LVCFMT_LEFT;
        col.pszText = (wchar_t*)L"Offset"; col.cx = 60;
        ListView_InsertColumn(hList, 0, &col);
        for (int i = 0; i < 16; i++) {
            wchar_t label[4];
            swprintf(label, 4, L"%X", i);
            col.pszText = label; col.cx = 24;
            ListView_InsertColumn(hList, 1 + i, &col);
        }
        col.pszText = (wchar_t*)L"ASCII"; col.cx = 130;
        ListView_InsertColumn(hList, 17, &col);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        wchar_t title[256];
        swprintf(title, 256, L"Sector 0 Editor - Disk %d (512 bytes)", hd->diskNumber);
        SetWindowTextW(hDlg, title);

        UpdateHexView(hd);
        return TRUE;
    }

    if (hd == NULL) return FALSE;
    hList = hd->hList;

    switch (msg) {
    case WM_NOTIFY: {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->hwndFrom == hList && pnmh->code == NM_CLICK) {
            LPNMLISTVIEW pnmlv = (LPNMLISTVIEW)pnmh;
            int row = pnmlv->iItem;
            int col = pnmlv->iSubItem;
            if (row >= 0 && col >= 1 && col <= 16) {
                int idx = row * 16 + (col - 1);
                hd->selectedIndex = idx;
                wchar_t hex[8];
                swprintf(hex, 8, L"%02X", hd->sectorData[idx]);
                SetDlgItemTextW(hDlg, 1002, hex);
            }
        }
        break;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == 1003) {
            /* Apply button: write the byte value */
            if (hd->selectedIndex >= 0) {
                wchar_t hex[8];
                GetDlgItemTextW(hDlg, 1002, hex, 8);
                DWORD v;
                if (swscanf(hex, L"%x", &v) == 1) {
                    hd->sectorData[hd->selectedIndex] = (BYTE)(v & 0xFF);
                    hd->modified = TRUE;
                    UpdateHexView(hd);
                }
            }
        } else if (LOWORD(wParam) == 1004) {
            /* Save to disk */
            wchar_t msg[256];
            swprintf(msg, 256, L"Write modified sector 0 to disk %d?\n\nTHIS IS EXTREMELY DANGEROUS - may render the disk unbootable!",
                     hd->diskNumber);
            if (MessageBoxW(hDlg, msg, L"Confirm Write", MB_YESNO | MB_ICONWARNING) == IDYES) {
                HANDLE hD = PE_OpenDrive(hd->diskNumber, TRUE);
                if (hD != INVALID_HANDLE_VALUE) {
                    if (PE_WriteSector(hD, hd->sectorNum, hd->sectorData)) {
                        MessageBoxW(hDlg, L"Sector written successfully.", L"Done", MB_OK);
                    } else {
                        MessageBoxW(hDlg, L"Write failed (check permissions / run as admin).", L"Error", MB_OK | MB_ICONERROR);
                    }
                    PE_CloseDrive(hD);
                } else {
                    MessageBoxW(hDlg, L"Cannot open disk for writing. Run as Administrator.", L"Error", MB_OK | MB_ICONERROR);
                }
            }
        } else if (LOWORD(wParam) == 1005) {
            /* Reload from disk */
            HANDLE hD = PE_OpenDrive(hd->diskNumber, FALSE);
            if (hD != INVALID_HANDLE_VALUE) {
                if (PE_ReadSector(hD, hd->sectorNum, hd->sectorData)) {
                    hd->modified = FALSE;
                    UpdateHexView(hd);
                }
                PE_CloseDrive(hD);
            }
        } else if (LOWORD(wParam) == 1006) {
            /* MBR -> GPT */
            wchar_t msg[256];
            swprintf(msg, 256, L"Convert disk %d from MBR to GPT?\n\nTHIS WILL OVERWRITE THE GPT HEADER AND MAY CORRUPT YOUR PARTITION TABLE!",
                     hd->diskNumber);
            if (MessageBoxW(hDlg, msg, L"Confirm Conversion", MB_YESNO | MB_ICONWARNING) == IDYES) {
                HANDLE hD = PE_OpenDrive(hd->diskNumber, TRUE);
                if (hD != INVALID_HANDLE_VALUE) {
                    if (PE_ConvertMBRToGPT(hD)) {
                        MessageBoxW(hDlg, L"Converted. Reload to see changes.", L"Done", MB_OK);
                    } else {
                        MessageBoxW(hDlg, L"Conversion failed.", L"Error", MB_OK | MB_ICONERROR);
                    }
                    PE_CloseDrive(hD);
                }
            }
        } else if (LOWORD(wParam) == 1007) {
            /* GPT -> MBR */
            wchar_t msg[256];
            swprintf(msg, 256, L"Convert disk %d from GPT to MBR?\n\nTHIS WILL OVERWRITE THE MBR AND DESTROY ALL GPT PARTITION INFO!",
                     hd->diskNumber);
            if (MessageBoxW(hDlg, msg, L"Confirm Conversion", MB_YESNO | MB_ICONWARNING) == IDYES) {
                HANDLE hD = PE_OpenDrive(hd->diskNumber, TRUE);
                if (hD != INVALID_HANDLE_VALUE) {
                    if (PE_ConvertGPTToMBR(hD)) {
                        MessageBoxW(hDlg, L"Converted. Reload to see changes.", L"Done", MB_OK);
                    } else {
                        MessageBoxW(hDlg, L"Conversion failed.", L"Error", MB_OK | MB_ICONERROR);
                    }
                    PE_CloseDrive(hD);
                }
            }
        } else if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam) == IDOK ? IDOK : IDCANCEL);
        }
        break;
    }
    }
    return FALSE;
}

/* Disk picker dialog */
typedef struct {
    int selectedDisk;
    int diskCount;
} DiskPickData;

static INT_PTR CALLBACK DiskPickDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static DiskPickData *dpd = NULL;
    static HWND hList = NULL;
    switch (msg) {
    case WM_INITDIALOG: {
        dpd = (DiskPickData*)lParam;
        hList = GetDlgItem(hDlg, 1001);
        LVCOLUMNW col = {0};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = (wchar_t*)L"Disk"; col.cx = 60;
        ListView_InsertColumn(hList, 0, &col);
        col.pszText = (wchar_t*)L"Type"; col.cx = 80;
        ListView_InsertColumn(hList, 1, &col);
        col.pszText = (wchar_t*)L"Size (sectors)"; col.cx = 150;
        ListView_InsertColumn(hList, 2, &col);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT);

        for (int i = 0; i < 8; i++) {
            HANDLE hD = PE_OpenDrive(i, FALSE);
            if (hD == INVALID_HANDLE_VALUE) continue;
            int ptType = PE_GetPartitionTableType(hD);
            ULONG64 sectors = 0;
            PE_GetDiskSize(hD, &sectors);
            PE_CloseDrive(hD);

            LVITEMW lvi = {0};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            wchar_t num[8]; swprintf(num, 8, L"%d", i);
            lvi.iSubItem = 0; lvi.pszText = num;
            ListView_InsertItem(hList, &lvi);
            wchar_t type[16];
            wcscpy(type, ptType == 2 ? L"GPT" : (ptType == 1 ? L"MBR" : L"Unknown"));
            lvi.iSubItem = 1; lvi.pszText = type;
            ListView_SetItem(hList, &lvi);
            wchar_t sz[32];
            swprintf(sz, 32, L"%llu", sectors);
            lvi.iSubItem = 2; lvi.pszText = sz;
            ListView_SetItem(hList, &lvi);
            dpd->diskCount++;
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel >= 0) {
                dpd->selectedDisk = sel;
                EndDialog(hDlg, IDOK);
            } else {
                MessageBoxW(hDlg, L"Please select a disk first.", L"Info", MB_OK);
            }
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        break;
    case WM_NOTIFY: {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->hwndFrom == hList && pnmh->code == NM_DBLCLK) {
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel >= 0) {
                dpd->selectedDisk = sel;
                EndDialog(hDlg, IDOK);
            }
        }
        break;
    }
    }
    return FALSE;
}

BOOL PE_OpenSectorEditorEx(HWND hwndParent, int diskNumber) {
    int chosenDisk = diskNumber;

    if (chosenDisk < 0) {
        /* Step 1: pick disk */
        DiskPickData dpd = {0};
        dpd.selectedDisk = -1;
        if (DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(200),
                            hwndParent, DiskPickDlgProc, (LPARAM)&dpd) != IDOK)
            return FALSE;
        if (dpd.selectedDisk < 0) return FALSE;
        chosenDisk = dpd.selectedDisk;
    }

    /* Step 2: read sector 0 and open editor */
    HANDLE hD = PE_OpenDrive(chosenDisk, FALSE);
    if (hD == INVALID_HANDLE_VALUE) {
        wchar_t msg[128];
        swprintf(msg, 128, L"Cannot open disk %d. Run as Administrator.", chosenDisk);
        MessageBoxW(hwndParent, msg, L"Error", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    BYTE *sector = (BYTE*)calloc(1, SECTOR_SIZE);
    PE_ReadSector(hD, 0, sector);
    PE_CloseDrive(hD);

    HexEditData hd = {0};
    hd.diskNumber = chosenDisk;
    hd.sectorData = sector;
    hd.sectorNum = 0;
    hd.modified = FALSE;
    hd.selectedIndex = -1;

    INT_PTR rc = DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(201),
                    hwndParent, HexEditDlgProc, (LPARAM)&hd);

    /* If user pressed Save (IDOK is used by buttons via WM_COMMAND, but Save button
     * returns IDCANCEL here). We handle saving inside the dlg proc. */
    free(sector);
    (void)rc;
    return TRUE;
}

BOOL PE_OpenSectorEditor(HWND hwndParent) {
    return PE_OpenSectorEditorEx(hwndParent, -1);
}

/* =================== Part Repeated / Protect =================== */
static void GetDataDir(wchar_t *out, int outSize) {
    extern HRESULT SHGetFolderPathW(HWND, int, HANDLE, DWORD, LPWSTR);
    wchar_t progData[MAX_PATH];
    if (SHGetFolderPathW(NULL, 0x0023 /*CSIDL_COMMON_APPDATA*/, NULL, 0, progData) == 0) {
        swprintf(out, outSize, L"%s\\ProcessGuardian", progData);
    } else {
        wcscpy(out, L".\\data");
    }
}

BOOL PE_AddToRepeatedDelete(int disk, int partition) {
    wchar_t base[MAX_PATH], partDir[MAX_PATH], file[MAX_PATH];
    GetDataDir(base, MAX_PATH);
    CreateDirectoryW(base, NULL);
    swprintf(partDir, MAX_PATH, L"%s\\partitions", base);
    CreateDirectoryW(partDir, NULL);
    swprintf(file, MAX_PATH, L"%s\\part_repeated.txt", base);
    FILE *f = _wfopen(file, L"a, ccs=UTF-8");
    if (!f) return FALSE;
    fwprintf(f, L"%d %d\n", disk, partition);
    fclose(f);
    return TRUE;
}

BOOL PE_AddToProtected(int disk) {
    wchar_t base[MAX_PATH], partDir[MAX_PATH], file[MAX_PATH];
    GetDataDir(base, MAX_PATH);
    CreateDirectoryW(base, NULL);
    swprintf(partDir, MAX_PATH, L"%s\\partitions", base);
    CreateDirectoryW(partDir, NULL);
    swprintf(file, MAX_PATH, L"%s\\protected_part.txt", base);
    FILE *f = _wfopen(file, L"a, ccs=UTF-8");
    if (!f) return FALSE;
    fwprintf(f, L"%d\n", disk);
    fclose(f);
    return TRUE;
}
