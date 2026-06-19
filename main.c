/**
 * Process Guardian - Multi-Mode Management Tool
 * Modes: Process / Service / Registry / Partition
 * Features: right-click menus, repeated delete, settings, i18n
 */

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdarg.h>
#include <winioctl.h>
#include "registry_tree.h"
#include "partition_edit.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "setupapi.lib")

// ======================== Mode Selection ========================
typedef enum {
    MODE_PROCESS = 0,
    MODE_SERVICE = 1,
    MODE_REGISTRY = 2,
    MODE_PARTITION = 3
} AppMode;

static AppMode g_currentMode = MODE_PROCESS;

// ======================== Inline DLL function defs ========================
// Service manager
typedef struct {
    wchar_t name[256];
    wchar_t displayName[256];
    wchar_t path[512];
    DWORD  status;
    DWORD  startType;
    wchar_t statusStr[64];
    wchar_t startTypeStr[64];
} ServiceEntry;

typedef int  (*FnGetAllServices)(ServiceEntry **);
typedef BOOL (*FnDeleteServiceByName)(const wchar_t *);
typedef BOOL (*FnStopServiceByName)(const wchar_t *);
typedef void (*FnAddSvcRepeated)(const wchar_t *);
typedef void (*FnRemoveSvcRepeated)(const wchar_t *);
typedef BOOL (*FnIsSvcRepeated)(const wchar_t *);

// Registry manager
typedef struct {
    wchar_t name[256];
    wchar_t path[512];
    wchar_t value[1024];
    wchar_t type[64];
    DWORD   dwType;
} RegistryEntry;

typedef int  (*FnGetRegEntries)(HKEY, const wchar_t *, RegistryEntry **);
typedef BOOL (*FnDeleteRegEntry)(HKEY, const wchar_t *, const wchar_t *);
typedef void (*FnAddRegRepeated)(const wchar_t *);
typedef void (*FnRemoveRegRepeated)(const wchar_t *);
typedef BOOL (*FnIsRegRepeated)(const wchar_t *);

// Partition manager
typedef enum { PART_TYPE_MBR = 0, PART_TYPE_GPT = 1 } PartitionTableType;

typedef struct {
    int  diskNumber;
    wchar_t model[256];
    ULONGLONG sizeBytes;
    PartitionTableType tableType;
    int  partitionCount;
} DiskInfo;

typedef struct {
    int  diskNumber;
    int  partitionNumber;
    wchar_t name[256];
    wchar_t location[512];
    wchar_t id[64];
    wchar_t fsType[64];
    wchar_t driveLetter[16];
    ULONGLONG sizeBytes;
    BOOL isBootable;
} PartitionEntry;

typedef int  (*FnGetAllDisks)(DiskInfo **);
typedef int  (*FnGetPartitionsOnDisk)(int, PartitionEntry **);
typedef const wchar_t* (*FnGetPartTableTypeStr)(PartitionTableType);
typedef BOOL (*FnDeletePartition)(int, int);
typedef void (*FnAddPartRepeated)(int, int, const wchar_t *);
typedef void (*FnRemovePartRepeated)(int, int);
typedef BOOL (*FnIsPartRepeated)(int, int);

// Loaded function pointers (from static linking or DLL)
static FnGetAllServices         svcGetAll = NULL;
static FnDeleteServiceByName    svcDelete = NULL;
static FnStopServiceByName     svcStop = NULL;
static FnAddSvcRepeated       svcAddRep = NULL;
static FnRemoveSvcRepeated    svcRemRep = NULL;
static FnIsSvcRepeated       svcIsRep = NULL;

static FnGetRegEntries        regGetAll = NULL;
static FnDeleteRegEntry       regDelete = NULL;
static FnAddRegRepeated      regAddRep = NULL;
static FnRemoveRegRepeated   regRemRep = NULL;
static FnIsRegRepeated       regIsRep = NULL;

static FnGetAllDisks         partGetDisks = NULL;
static FnGetPartitionsOnDisk partGetParts = NULL;
static FnGetPartTableTypeStr partGetTypeStr = NULL;
static FnDeletePartition     partDelete = NULL;
static FnAddPartRepeated     partAddRep = NULL;
static FnRemovePartRepeated  partRemRep = NULL;
static FnIsPartRepeated      partIsRep = NULL;

// ======================== Internationalization ========================
typedef enum { LANG_ZH = 0, LANG_EN = 1 } AppLang;
static AppLang g_lang = LANG_ZH;

typedef struct {
    const wchar_t *wndTitle;
    const wchar_t *settingsTitle;
    const wchar_t *labelRunning;
    const wchar_t *labelProtected;
    const wchar_t *hint;
    const wchar_t *colName;
    const wchar_t *colPID;
    const wchar_t *colMemory;
    const wchar_t *colType;
    // Service columns
    const wchar_t *colSvcName;
    const wchar_t *colSvcDisp;
    const wchar_t *colSvcStatus;
    const wchar_t *colSvcStart;
    // Registry columns
    const wchar_t *colRegName;
    const wchar_t *colRegPath;
    const wchar_t *colRegType;
    const wchar_t *colRegValue;
    // Partition columns
    const wchar_t *colPartName;
    const wchar_t *colPartLoc;
    const wchar_t *colPartId;
    const wchar_t *colPartPart;
    const wchar_t *colPartDisk;
    // Buttons
    const wchar_t *btnRefresh;
    const wchar_t *btnDaemon;
    const wchar_t *btnSettings;
    const wchar_t *btnLang;
    const wchar_t *btnAutoOn;
    const wchar_t *btnAutoOff;
    // Menus
    const wchar_t *menuKill;
    const wchar_t *menuKillTree;
    const wchar_t *menuStopSvc;
    const wchar_t *menuDeleteSvc;
    const wchar_t *menuDeleteReg;
    const wchar_t *menuDeletePart;
    const wchar_t *menuRepKill;
    const wchar_t *menuRepKillTree;
    const wchar_t *menuRepDeleteSvc;
    const wchar_t *menuRepDeleteReg;
    const wchar_t *menuRepDeletePart;
    const wchar_t *menuProtect;
    const wchar_t *menuOpenLoc;
    const wchar_t *menuMonitor;
    const wchar_t *menuInstallSvc;
    const wchar_t *menuCancelRep;
    const wchar_t *menuCancelProt;
    const wchar_t *menuCancelRepSvc;
    const wchar_t *menuCancelRepReg;
    const wchar_t *menuCancelRepPart;
    // Type labels
    const wchar_t *typeRepKill;
    const wchar_t *typeRepKillTree;
    const wchar_t *typeProt;
    const wchar_t *typeRepSvc;
    const wchar_t *typeRepReg;
    const wchar_t *typeRepPart;
    // Messages
    const wchar_t *msgSvcInstalled;
    const wchar_t *msgSvcInstalling;
    const wchar_t *msgNeedAdmin;
    const wchar_t *msgDaemonSystem;
    const wchar_t *msgSvcStartFail;
    const wchar_t *msgDaemonAdmin;
    const wchar_t *msgDaemonStarted;
    const wchar_t *msgSuccess;
    const wchar_t *msgInfo;
    const wchar_t *msgError;
    const wchar_t *msgSvcFail;
    const wchar_t *msgAutoStartSet;
    const wchar_t *msgWatchdogRunning;
    const wchar_t *msgPartDeleteConfirm;
    const wchar_t *msgPartDeleteDone;
    const wchar_t *msgRegDeleteDone;
    const wchar_t *msgSvcDeleteDone;
} LangStrings;

static const LangStrings g_strings[2] = {
    // LANG_ZH
    {
        L"进程守护者",
        L"进程守护者 - 设置",
        L"正在运行的进程：",
        L"已管理的进程：",
        L"提示：右键进程可执行操作",
        L"名称", L"PID", L"内存(KB)", L"类型",
        L"服务名称", L"显示名称", L"状态", L"启动类型",
        L"名称", L"路径", L"类型", L"值",
        L"名称", L"位置", L"ID", L"所属分区", L"所属物理磁盘",
        L"刷新列表", L"启动守护", L"设置", L"EN",
        L"暂停刷新", L"恢复刷新",
        L"结束进程", L"结束进程树",
        L"停止服务", L"删除服务",
        L"删除注册表项",
        L"删除分区",
        L"重复结束进程", L"重复结束进程树",
        L"重复删除服务",
        L"重复删除注册表项",
        L"重复删除分区",
        L"保护进程", L"打开进程位置", L"实时监控", L"安装SYSTEM服务",
        L"取消重复终止", L"取消保护",
        L"取消重复删除服务",
        L"取消重复删除注册表项",
        L"取消重复删除分区",
        L"重复结束", L"重复结束(树)", L"保护",
        L"重复删除", L"重复删除", L"重复删除",
        L"服务已安装并启动！", L"服务安装成功，尝试启动...",
        L"需要管理员权限来安装服务！",
        L"守护进程已以 SYSTEM 权限启动！",
        L"启动服务失败！",
        L"守护进程已以管理员权限启动！",
        L"守护进程已启动并设置开机自启",
        L"成功", L"提示", L"错误",
        L"服务启动失败！\n请以管理员身份运行主程序。",
        L"已通过任务计划程序设置开机自启（无需等待桌面）",
        L"已启动 watchdog 守护（即使主守护被关闭也会自动恢复）",
        L"确定要删除该分区吗？\n此操作不可逆！",
        L"分区已删除",
        L"注册表项已删除",
        L"服务已删除",
    },
    // LANG_EN
    {
        L"Process Guardian",
        L"Process Guardian - Settings",
        L"Running Processes:",
        L"Managed Processes:",
        L"Tip: Right-click a process for options",
        L"Name", L"PID", L"Memory(KB)", L"Type",
        L"Svc Name", L"Display Name", L"Status", L"Start Type",
        L"Name", L"Path", L"Type", L"Value",
        L"Name", L"Location", L"ID", L"Partition", L"Disk",
        L"Refresh", L"Start Daemon", L"Settings", L"中文",
        L"Pause Auto", L"Resume Auto",
        L"Terminate Process", L"Terminate Tree",
        L"Stop Service", L"Delete Service",
        L"Delete Registry Key",
        L"Delete Partition",
        L"Repeated Terminate", L"Repeated Terminate Tree",
        L"Repeated Delete Svc",
        L"Repeated Delete Reg",
        L"Repeated Delete Part",
        L"Protect Process", L"Open Location", L"Live Monitor", L"Install SYSTEM Svc",
        L"Cancel Repeated Term.", L"Cancel Protection",
        L"Cancel Repeated Svc",
        L"Cancel Repeated Reg",
        L"Cancel Repeated Part",
        L"Rep. Term.", L"Rep. Term. (Tree)", L"Protected",
        L"Rep. Delete", L"Rep. Delete", L"Rep. Delete",
        L"Service installed and started!",
        L"Service installed, starting...",
        L"Administrator privileges required to install service!",
        L"Daemon started with SYSTEM privileges!",
        L"Failed to start service!",
        L"Daemon started with Administrator privileges!",
        L"Daemon started and set to auto-run",
        L"Success", L"Info", L"Error",
        L"Failed to start service!\nPlease run as Administrator.",
        L"Auto-start registered via Task Scheduler",
        L"Watchdog started (daemon auto-restarts if killed)",
        L"Are you sure you want to delete this partition?\nThis is IRREVERSIBLE!",
        L"Partition deleted",
        L"Registry key deleted",
        L"Service deleted",
    }
};

#define STR(key) (g_strings[g_lang].key)

// ======================== Data Structures ========================
// Process protected list
typedef struct {
    wchar_t name[260];
    DWORD pid;
    BOOL isTree;
    BOOL isRepeated;
} ProtectedEntry;

#define MAX_PROTECTED 256
static ProtectedEntry g_protectedList[MAX_PROTECTED];
static int g_protectedCount = 0;

// Service repeated-delete list
typedef struct {
    wchar_t name[256];
} SvcRepeatedEntry;

#define MAX_SVC_REPEATED 128
static SvcRepeatedEntry g_svcRepeated[MAX_SVC_REPEATED];
static int g_svcRepeatedCount = 0;

// Registry repeated-delete list
typedef struct {
    wchar_t fullPath[512];  // e.g. "HKLM\Software\..."
} RegRepeatedEntry;

#define MAX_REG_REPEATED 256
static RegRepeatedEntry g_regRepeated[MAX_REG_REPEATED];
static int g_regRepeatedCount = 0;

// Partition repeated-delete list
typedef struct {
    int  diskNumber;
    int  partitionNumber;
    wchar_t location[512];
} PartRepeatedEntry;

#define MAX_PART_REPEATED 64
static PartRepeatedEntry g_partRepeated[MAX_PART_REPEATED];
static int g_partRepeatedCount = 0;

// ======================== Global State ========================
static FILE *g_logFile = NULL;
static CRITICAL_SECTION g_cs;
static HWND hTab;
static HWND hRunningList;
static HWND hProtectedList;
static HWND hLabelRunning;
static HWND hLabelProtected;
static HWND hLabelHint;
static HWND hLabelDiskType;
static HWND hBtnRefresh;
static HWND hBtnDaemon;
static HWND hBtnSettings;
static HWND hBtnLang;
static HWND hBtnAutoRefresh;
static HWND g_hTreeReg = NULL;
static HWND hBtnBootEdit = NULL;     // "Edit MBR/GPT" button (partition mode)
static HWND hBtnBootReload = NULL;   // "重读" button next to hex dump
static HWND hLblBootType = NULL;     // shows MBR/GPT/Unknown + 16-byte hex summary
static HWND hLblBootHex  = NULL;     // static text showing first 16 bytes of sector 0
static HWND hGrpBoot     = NULL;     // BS_GROUPBOX around boot preview area
static HFONT g_hFontMono = NULL;     // monospace font for hex dump
static int  g_bootDiskNum = 0;       // which disk the preview shows
static BYTE g_bootSector[512];       // cached 512 bytes
static BOOL g_bootLoaded = FALSE;
static BOOL g_autoRefresh = TRUE;
static HFONT g_hFont;

// ======================== Forward Declarations ========================
void InitLogger(void);
void Log(const wchar_t *format, ...);
void CloseLogger(void);
void LoadConfig(void);
void SaveConfig(void);
void LoadSvcRepeatedConfig(void);
void SaveSvcRepeatedConfig(void);
void LoadRegRepeatedConfig(void);
void SaveRegRepeatedConfig(void);
void LoadPartRepeatedConfig(void);
void SavePartRepeatedConfig(void);
void AddToRepeatedList(const wchar_t *name);
void AddToRepeatedTreeList(const wchar_t *name);
void AddToProtectedList(const wchar_t *name);
void RemoveFromList(int index);
int  FindInList(const wchar_t *name);
DWORD GetProcessIDByName(const wchar_t *name);
BOOL KillProcessByPID(DWORD pid);
BOOL KillProcessTree(DWORD pid);

/* ============ Boot sector preview (partition mode) ============ */
/* We do NOT render a ListView here any more. The previous ListView caused
 * the perceived "every click = refresh" bug because selecting a row
 * triggered LVN_ITEMCHANGED -> BootPreview_Load -> 32 rows of ListView_SetItem,
 * which re-pumped paint and re-ran the whole paint cycle. We now show a
 * single label with the MBR/GPT/Unknown status and a one-line 16-byte hex
 * summary. The "Edit MBR/GPT" button opens the existing byte-level hex
 * editor (IDD_HEXEDIT) for full read/write. */

static void BootPreview_Load(HWND hwnd) {
    (void)hwnd;
    if (!hLblBootType) return;

    /* Use disk 0 by default, or first selected row's disk */
    int disk = 0;
    int sel = ListView_GetNextItem(hRunningList, -1, LVNI_SELECTED);
    if (sel >= 0) {
        wchar_t buf[32] = {0};
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT; lvi.iItem = sel; lvi.iSubItem = 4;
        lvi.pszText = buf; lvi.cchTextMax = 32;
        if (ListView_GetItem(hRunningList, &lvi)) {
            int d = _wtoi(buf);
            if (d > 0) disk = d;
        }
    }
    g_bootDiskNum = disk;
    g_bootLoaded = FALSE;
    memset(g_bootSector, 0, sizeof(g_bootSector));

    /* Use PE_OpenDrive (same as editor) so error handling is consistent. */
    HANDLE hD = PE_OpenDrive(disk, FALSE);
    if (hD == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (hLblBootHex) {
            if (err == 5 /* ERROR_ACCESS_DENIED */) {
                SetWindowTextW(hLblBootHex, L"  (需要管理员权限才能读取 PhysicalDrive)");
            } else {
                wchar_t tmp[96];
                swprintf(tmp, 96, L"  (无法打开磁盘: GetLastError=%lu)", err);
                SetWindowTextW(hLblBootHex, tmp);
            }
        }
        wchar_t typeLabel[80];
        swprintf(typeLabel, 80, L"引导扇区 (Disk %d) - 访问被拒绝", disk);
        SetWindowTextW(hLblBootType, typeLabel);
        return;
    }
    DWORD br = 0;
    BOOL ok = ReadFile(hD, g_bootSector, 512, &br, NULL);
    CloseHandle(hD);
    if (!ok || br != 512) {
        DWORD err = GetLastError();
        if (hLblBootHex) {
            wchar_t tmp[96];
            swprintf(tmp, 96, L"  (读取失败: GetLastError=%lu, br=%lu)", err, br);
            SetWindowTextW(hLblBootHex, tmp);
        }
        wchar_t typeLabel[80];
        swprintf(typeLabel, 80, L"引导扇区 (Disk %d) - 读取失败", disk);
        SetWindowTextW(hLblBootType, typeLabel);
        return;
    }
    g_bootLoaded = TRUE;

    /* Detect MBR / GPT. MBR = 0x55AA at offset 510. GPT = "EFI PART" at sector 1. */
    BOOL isGPT = FALSE;
    BYTE gptSig[8] = {'E','F','I',' ','P','A','R','T'};
    HANDLE hD2 = PE_OpenDrive(disk, FALSE);
    BYTE sec1[512] = {0};
    if (hD2 != INVALID_HANDLE_VALUE) {
        DWORD br2 = 0;
        if (ReadFile(hD2, sec1, 512, &br2, NULL) && br2 == 512) {
            if (memcmp(sec1, gptSig, 8) == 0) isGPT = TRUE;
        }
        CloseHandle(hD2);
    }
    BOOL isMBR = (g_bootSector[510] == 0x55 && g_bootSector[511] == 0xAA);

    wchar_t typeLabel[80];
    if (isGPT)        swprintf(typeLabel, 80, L"引导扇区 (Disk %d) — GPT  (签名 0x%02X%02X)",
                               disk, g_bootSector[510], g_bootSector[511]);
    else if (isMBR)   swprintf(typeLabel, 80, L"引导扇区 (Disk %d) — MBR  (签名 0x55AA)",
                               disk);
    else              swprintf(typeLabel, 80, L"引导扇区 (Disk %d) — 未知  (签名 0x%02X%02X)",
                               disk, g_bootSector[510], g_bootSector[511]);
    SetWindowTextW(hLblBootType, typeLabel);

    /* Multi-line hex preview: 4 lines x 16 bytes + ASCII column.
     * Shows first 64 bytes of the MBR/GPT sector in monospace.
     * Format per line: "OFFSET  XX XX XX XX XX XX XX XX  XX XX XX XX XX XX XX XX  |ASCII...........|" */
    if (hLblBootHex) {
        wchar_t hex[768];
        wchar_t *p = hex;
        p += swprintf(p, 32, L"扇区 0  前 64 字节 (Hex + ASCII):\r\n");
        for (int row = 0; row < 4; row++) {
            int base = row * 16;
            p += swprintf(p, 16, L"%03X  ", base);
            for (int i = 0; i < 16; i++) {
                p += swprintf(p, 8, L"%02X ", g_bootSector[base + i]);
                if (i == 7) p += swprintf(p, 8, L" ");
            }
            p += swprintf(p, 8, L" |");
            for (int i = 0; i < 16; i++) {
                BYTE b = g_bootSector[base + i];
                p += swprintf(p, 8, L"%lc", (b >= 0x20 && b < 0x7F) ? (wint_t)b : (wint_t)L'.');
            }
            p += swprintf(p, 8, L"|\r\n");
        }
        /* Trailing hint: offset of the 55 AA signature (always 510 for MBR) */
        p += swprintf(p, 96, L"\r\n末两字节 (0x1FE/0x1FF): %02X %02X  =  MBR 签名",
                      g_bootSector[510], g_bootSector[511]);
        SetWindowTextW(hLblBootHex, hex);
    }
}

void RefreshRunningList(HWND hwnd);
void RefreshProtectedList(HWND hwnd);
void RefreshForCurrentMode(HWND hwnd);
void ShowSettingsDialog(HWND hwnd);
void ApplyLanguage(HWND hwnd);
void LoadLangSetting(void);
void SaveLangSetting(void);
void StartDaemon(HWND hwnd, int mode);
static BOOL IsRunAsAdmin(void);
void InstallServiceAsSystem(HWND hwnd);
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
static void GetGuardianExe(wchar_t*, int);
static void GetWrapperExe(wchar_t*, int);
static void GetInstallExe(wchar_t*, int);
static BOOL SpawnGuardDaemon(BOOL);
static void SetAutoStart(BOOL);

// ======================== Logger ========================
void InitLogger(void) {
    InitializeCriticalSection(&g_cs);
    wchar_t logPath[MAX_PATH];
    GetModuleFileNameW(NULL, logPath, MAX_PATH);
    wchar_t *p = wcsrchr(logPath, L'\\');
    if (p) *p = L'\0';
    wcscat(logPath, L"\\data\\process_guardian.log");
    g_logFile = _wfopen(logPath, L"a");
    if (g_logFile) {
        time_t now = time(NULL);
        wchar_t t[64];
        wcsftime(t, 64, L"%Y-%m-%d %H:%M:%S", localtime(&now));
        fwprintf(g_logFile, L"[%s] Guardian started\n", t);
        fflush(g_logFile);
    }
}

void Log(const wchar_t *format, ...) {
    if (!g_logFile) return;
    EnterCriticalSection(&g_cs);
    time_t now = time(NULL);
    wchar_t t[64];
    wcsftime(t, 64, L"%Y-%m-%d %H:%M:%S", localtime(&now));
    fwprintf(g_logFile, L"[%s] ", t);
    va_list args;
    va_start(args, format);
    vfwprintf(g_logFile, format, args);
    va_end(args);
    fwprintf(g_logFile, L"\n");
    fflush(g_logFile);
    LeaveCriticalSection(&g_cs);
}

void CloseLogger(void) {
    if (g_logFile) {
        time_t now = time(NULL);
        wchar_t t[64];
        wcsftime(t, 64, L"%Y-%m-%d %H:%M:%S", localtime(&now));
        fwprintf(g_logFile, L"[%s] Guardian stopped\n", t);
        fclose(g_logFile);
        g_logFile = NULL;
    }
    DeleteCriticalSection(&g_cs);
}

// ======================== Config ========================
void LoadConfig(void) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *p = wcsrchr(path, L'\\');
    if (p) *p = L'\0';
    wcscat(path, L"\\data\\config.dat");
    FILE *f = _wfopen(path, L"rb");
    if (!f) return;
    fread(&g_protectedCount, sizeof(int), 1, f);
    if (g_protectedCount > MAX_PROTECTED) g_protectedCount = MAX_PROTECTED;
    fread(g_protectedList, sizeof(ProtectedEntry), g_protectedCount, f);
    fclose(f);
}

void SaveConfig(void) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *p = wcsrchr(path, L'\\');
    if (p) *p = L'\0';
    wcscat(path, L"\\data\\config.dat");
    FILE *f = _wfopen(path, L"wb");
    if (!f) return;
    fwrite(&g_protectedCount, sizeof(int), 1, f);
    fwrite(g_protectedList, sizeof(ProtectedEntry), g_protectedCount, f);
    fclose(f);
}

// Svc repeated config
void LoadSvcRepeatedConfig(void) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *p = wcsrchr(path, L'\\');
    if (p) *p = L'\0';
    wcscat(path, L"\\data\\svc_repeated.dat");
    FILE *f = _wfopen(path, L"rb");
    if (!f) return;
    fread(&g_svcRepeatedCount, sizeof(int), 1, f);
    if (g_svcRepeatedCount > MAX_SVC_REPEATED) g_svcRepeatedCount = MAX_SVC_REPEATED;
    fread(g_svcRepeated, sizeof(SvcRepeatedEntry), g_svcRepeatedCount, f);
    fclose(f);
}

void SaveSvcRepeatedConfig(void) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *p = wcsrchr(path, L'\\');
    if (p) *p = L'\0';
    wcscat(path, L"\\data\\svc_repeated.dat");
    FILE *f = _wfopen(path, L"wb");
    if (!f) return;
    fwrite(&g_svcRepeatedCount, sizeof(int), 1, f);
    fwrite(g_svcRepeated, sizeof(SvcRepeatedEntry), g_svcRepeatedCount, f);
    fclose(f);
}

// Reg repeated config
void LoadRegRepeatedConfig(void) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *p = wcsrchr(path, L'\\');
    if (p) *p = L'\0';
    wcscat(path, L"\\data\\reg_repeated.dat");
    FILE *f = _wfopen(path, L"rb");
    if (!f) return;
    fread(&g_regRepeatedCount, sizeof(int), 1, f);
    if (g_regRepeatedCount > MAX_REG_REPEATED) g_regRepeatedCount = MAX_REG_REPEATED;
    fread(g_regRepeated, sizeof(RegRepeatedEntry), g_regRepeatedCount, f);
    fclose(f);
}

void SaveRegRepeatedConfig(void) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *p = wcsrchr(path, L'\\');
    if (p) *p = L'\0';
    wcscat(path, L"\\data\\reg_repeated.dat");
    FILE *f = _wfopen(path, L"wb");
    if (!f) return;
    fwrite(&g_regRepeatedCount, sizeof(int), 1, f);
    fwrite(g_regRepeated, sizeof(RegRepeatedEntry), g_regRepeatedCount, f);
    fclose(f);
}

// Part repeated config
void LoadPartRepeatedConfig(void) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *p = wcsrchr(path, L'\\');
    if (p) *p = L'\0';
    wcscat(path, L"\\data\\part_repeated.dat");
    FILE *f = _wfopen(path, L"rb");
    if (!f) return;
    fread(&g_partRepeatedCount, sizeof(int), 1, f);
    if (g_partRepeatedCount > MAX_PART_REPEATED) g_partRepeatedCount = MAX_PART_REPEATED;
    fread(g_partRepeated, sizeof(PartRepeatedEntry), g_partRepeatedCount, f);
    fclose(f);
}

void SavePartRepeatedConfig(void) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *p = wcsrchr(path, L'\\');
    if (p) *p = L'\0';
    wcscat(path, L"\\data\\part_repeated.dat");
    FILE *f = _wfopen(path, L"wb");
    if (!f) return;
    fwrite(&g_partRepeatedCount, sizeof(int), 1, f);
    fwrite(g_partRepeated, sizeof(PartRepeatedEntry), g_partRepeatedCount, f);
    fclose(f);
}

// ======================== Language Persistence ========================
static wchar_t g_settingsPath[MAX_PATH];

void GetSettingsPath(void) {
    if (g_settingsPath[0]) return;
    GetModuleFileNameW(NULL, g_settingsPath, MAX_PATH);
    wchar_t *p = wcsrchr(g_settingsPath, L'\\');
    if (p) *p = L'\0';
    wcscat(g_settingsPath, L"\\data\\settings.ini");
}

void LoadLangSetting(void) {
    GetSettingsPath();
    wchar_t val[16] = L"";
    GetPrivateProfileStringW(L"UI", L"Language", L"zh", val, 16, g_settingsPath);
    g_lang = (_wcsicmp(val, L"en") == 0) ? LANG_EN : LANG_ZH;
}

void SaveLangSetting(void) {
    GetSettingsPath();
    WritePrivateProfileStringW(L"UI", L"Language", (g_lang == LANG_EN) ? L"en" : L"zh", g_settingsPath);
}

// ======================== Process List Management ========================
void AddToRepeatedList(const wchar_t *name) {
    EnterCriticalSection(&g_cs);
    if (g_protectedCount < MAX_PROTECTED && FindInList(name) < 0) {
        wcsncpy(g_protectedList[g_protectedCount].name, name, 259);
        g_protectedList[g_protectedCount].name[259] = L'\0';
        g_protectedList[g_protectedCount].pid = GetProcessIDByName(name);
        g_protectedList[g_protectedCount].isTree = FALSE;
        g_protectedList[g_protectedCount].isRepeated = TRUE;
        g_protectedCount++;
        SaveConfig();
    }
    LeaveCriticalSection(&g_cs);
}

void AddToRepeatedTreeList(const wchar_t *name) {
    EnterCriticalSection(&g_cs);
    if (g_protectedCount < MAX_PROTECTED && FindInList(name) < 0) {
        wcsncpy(g_protectedList[g_protectedCount].name, name, 259);
        g_protectedList[g_protectedCount].name[259] = L'\0';
        g_protectedList[g_protectedCount].pid = GetProcessIDByName(name);
        g_protectedList[g_protectedCount].isTree = TRUE;
        g_protectedList[g_protectedCount].isRepeated = TRUE;
        g_protectedCount++;
        SaveConfig();
    }
    LeaveCriticalSection(&g_cs);
}

void AddToProtectedList(const wchar_t *name) {
    EnterCriticalSection(&g_cs);
    if (g_protectedCount < MAX_PROTECTED && FindInList(name) < 0) {
        wcsncpy(g_protectedList[g_protectedCount].name, name, 259);
        g_protectedList[g_protectedCount].name[259] = L'\0';
        g_protectedList[g_protectedCount].pid = GetProcessIDByName(name);
        g_protectedList[g_protectedCount].isTree = FALSE;
        g_protectedList[g_protectedCount].isRepeated = FALSE;
        g_protectedCount++;
        SaveConfig();
    }
    LeaveCriticalSection(&g_cs);
}

void RemoveFromList(int index) {
    EnterCriticalSection(&g_cs);
    if (index >= 0 && index < g_protectedCount) {
        for (int j = index; j < g_protectedCount - 1; j++)
            g_protectedList[j] = g_protectedList[j + 1];
        g_protectedCount--;
        SaveConfig();
        // Signal daemon to reload
        wchar_t sigPath[MAX_PATH];
        GetModuleFileNameW(NULL, sigPath, MAX_PATH);
        wchar_t *p = wcsrchr(sigPath, L'\\');
        if (p) *p = L'\0';
        wcscat(sigPath, L"\\data\\reload.signal");
        FILE *f = _wfopen(sigPath, L"w");
        if (f) fclose(f);
    }
    LeaveCriticalSection(&g_cs);
}

int FindInList(const wchar_t *name) {
    for (int i = 0; i < g_protectedCount; i++)
        if (_wcsicmp(g_protectedList[i].name, name) == 0) return i;
    return -1;
}

DWORD GetProcessIDByName(const wchar_t *name) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = {0};
    pe.dwSize = sizeof(PROCESSENTRY32W);
    DWORD pid = 0;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return pid;
}

BOOL KillProcessByPID(DWORD pid) {
    if (pid == 0 || pid == 4) return FALSE;
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hProc) {
        HANDLE hToken;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
            TOKEN_PRIVILEGES tp;
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            LookupPrivilegeValueW(NULL, SE_DEBUG_NAME, &tp.Privileges[0].Luid);
            AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, 0);
            CloseHandle(hToken);
        }
        hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    }
    if (!hProc) return FALSE;
    BOOL result = TerminateProcess(hProc, 1);
    CloseHandle(hProc);
    return result;
}

BOOL KillProcessTree(DWORD pid) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {0};
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ParentProcessID == pid)
                    KillProcessTree(pe.th32ProcessID);
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    return KillProcessByPID(pid);
}

// ======================== Service Repeated Management ========================
static int FindSvcRepeated(const wchar_t *name) {
    for (int i = 0; i < g_svcRepeatedCount; i++)
        if (_wcsicmp(g_svcRepeated[i].name, name) == 0) return i;
    return -1;
}

static void AddSvcToRepeated(const wchar_t *name) {
    if (g_svcRepeatedCount < MAX_SVC_REPEATED && FindSvcRepeated(name) < 0) {
        wcsncpy(g_svcRepeated[g_svcRepeatedCount].name, name, 255);
        g_svcRepeated[g_svcRepeatedCount].name[255] = L'\0';
        g_svcRepeatedCount++;
        SaveSvcRepeatedConfig();
    }
}

static void RemoveSvcFromRepeated(int index) {
    if (index >= 0 && index < g_svcRepeatedCount) {
        for (int j = index; j < g_svcRepeatedCount - 1; j++)
            g_svcRepeated[j] = g_svcRepeated[j + 1];
        g_svcRepeatedCount--;
        SaveSvcRepeatedConfig();
    }
}

// ======================== Registry Repeated Management ========================
static int FindRegRepeated(const wchar_t *fullPath) {
    for (int i = 0; i < g_regRepeatedCount; i++)
        if (_wcsicmp(g_regRepeated[i].fullPath, fullPath) == 0) return i;
    return -1;
}

static void AddRegToRepeated(const wchar_t *fullPath) {
    if (g_regRepeatedCount < MAX_REG_REPEATED && FindRegRepeated(fullPath) < 0) {
        wcsncpy(g_regRepeated[g_regRepeatedCount].fullPath, fullPath, 511);
        g_regRepeated[g_regRepeatedCount].fullPath[511] = L'\0';
        g_regRepeatedCount++;
        SaveRegRepeatedConfig();
    }
}

static void RemoveRegFromRepeated(int index) {
    if (index >= 0 && index < g_regRepeatedCount) {
        for (int j = index; j < g_regRepeatedCount - 1; j++)
            g_regRepeated[j] = g_regRepeated[j + 1];
        g_regRepeatedCount--;
        SaveRegRepeatedConfig();
    }
}

// ======================== Partition Repeated Management ========================
static int FindPartRepeated(int diskNumber, int partitionNumber) {
    for (int i = 0; i < g_partRepeatedCount; i++)
        if (g_partRepeated[i].diskNumber == diskNumber &&
            g_partRepeated[i].partitionNumber == partitionNumber) return i;
    return -1;
}

static void AddPartToRepeated(int diskNumber, int partitionNumber, const wchar_t *location) {
    if (g_partRepeatedCount < MAX_PART_REPEATED && FindPartRepeated(diskNumber, partitionNumber) < 0) {
        g_partRepeated[g_partRepeatedCount].diskNumber = diskNumber;
        g_partRepeated[g_partRepeatedCount].partitionNumber = partitionNumber;
        wcsncpy(g_partRepeated[g_partRepeatedCount].location, location, 511);
        g_partRepeated[g_partRepeatedCount].location[511] = L'\0';
        g_partRepeatedCount++;
        SavePartRepeatedConfig();
    }
}

static void RemovePartFromRepeated(int index) {
    if (index >= 0 && index < g_partRepeatedCount) {
        for (int j = index; j < g_partRepeatedCount - 1; j++)
            g_partRepeated[j] = g_partRepeated[j + 1];
        g_partRepeatedCount--;
        SavePartRepeatedConfig();
    }
}

// Helper: get column count of a ListView
static int GetListViewColCount(HWND hLV) {
    HWND hHeader = ListView_GetHeader(hLV);
    if (!hHeader) return 0;
    return Header_GetItemCount(hHeader);
}

// ======================== Helpers ========================
static void GetGuardianExe(wchar_t *out, int outLen) {
    GetModuleFileNameW(NULL, out, outLen);
    wchar_t *p = wcsrchr(out, L'\\');
    if (p) *p = L'\0';
    wcscat(out, L"\\guardiand.exe");
}

static void GetWrapperExe(wchar_t *out, int outLen) {
    GetModuleFileNameW(NULL, out, outLen);
    wchar_t *p = wcsrchr(out, L'\\');
    if (p) *p = L'\0';
    wcscat(out, L"\\service_wrapper.exe");
}

static void GetInstallExe(wchar_t *out, int outLen) {
    GetModuleFileNameW(NULL, out, outLen);
    wchar_t *p = wcsrchr(out, L'\\');
    if (p) *p = L'\0';
    wcscat(out, L"\\install_service.exe");
}

static BOOL SpawnGuardDaemon(BOOL admin) {
    (void)admin;
    wchar_t exePath[MAX_PATH];
    GetGuardianExe(exePath, MAX_PATH);
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {0};
    DWORD flags = CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    BOOL ok = CreateProcessW(exePath, L" --hidden", NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi);
    if (ok) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    return ok;
}

static void SetAutoStart(BOOL adminMode) {
    (void)adminMode;
    wchar_t exePath[MAX_PATH];
    GetGuardianExe(exePath, MAX_PATH);
    wchar_t cmd[2048];
    swprintf(cmd, 2048,
        L"schtasks /Create /TN \"GuardianDaemon\" /TR \"\\\"%s\\\" --hidden\" /SC ONSTART /RL HIGHEST /F",
        exePath);
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = INVALID_HANDLE_VALUE;
    si.hStdOutput = INVALID_HANDLE_VALUE;
    si.hStdError = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi = {0};
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    // Fallback: HKCU\Run
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        wchar_t regValue[MAX_PATH + 20];
        swprintf(regValue, MAX_PATH + 20, L"\"%s\" --hidden", exePath);
        RegSetValueExW(hKey, L"GuardianDaemon", 0, REG_SZ,
                       (const BYTE*)regValue,
                       (wcslen(regValue) + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }
}

static BOOL IsRunAsAdmin(void) {
    BOOL f = FALSE;
    SID_IDENTIFIER_AUTHORITY a = SECURITY_NT_AUTHORITY;
    PSID p = NULL;
    if (AllocateAndInitializeSid(&a, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0,0,0,0,0,0, &p)) {
        CheckTokenMembership(NULL, p, &f);
        FreeSid(p);
    }
    return f;
}

// ======================== Permission DLL Loader ========================
static BOOL CallPermissionDll(HWND hwnd, int mode) {
    const wchar_t *dllNames[] = {L"normal.dll", L"admin.dll", L"SYSTEM.dll"};
    if (mode < 0 || mode > 2) return FALSE;
    wchar_t dllPath[MAX_PATH];
    GetModuleFileNameW(NULL, dllPath, MAX_PATH);
    wchar_t *slash = wcsrchr(dllPath, L'\\');
    if (slash) *slash = L'\0';
    wcscat(dllPath, L"\\permission\\");
    wcscat(dllPath, dllNames[mode]);
    HMODULE hDll = LoadLibraryW(dllPath);
    if (!hDll) return FALSE;
    typedef BOOL (*FnRestartDaemon)(HWND);
    FnRestartDaemon fn = (FnRestartDaemon)GetProcAddress(hDll, "RestartDaemon");
    BOOL ok = FALSE;
    if (fn) ok = fn(hwnd);
    FreeLibrary(hDll);
    return ok;
}

void StartDaemon(HWND hwnd, int mode) {
    // Try permission DLL first
    BOOL dllOk = CallPermissionDll(hwnd, mode);
    if (dllOk) {
        if (hwnd) {
            const wchar_t *msg =
                (mode == 2) ? STR(msgDaemonSystem) :
                (mode == 1) ? STR(msgDaemonAdmin) :
                               STR(msgDaemonStarted);
            MessageBoxW(hwnd, msg, STR(msgSuccess), MB_OK);
        }
        if (mode != 2) SetAutoStart(mode == 1);
        Log(L"Daemon restarted via DLL (mode=%d)", mode);
        return;
    }
    // ===== Fallback logic (DLL 不可用时使用) =====
    if (mode == 2) {
        /* SYSTEM 模式 fallback: 通过 install_service.exe 安装并启动服务 */
        wchar_t installExe[MAX_PATH];
        GetInstallExe(installExe, MAX_PATH);
        SHELLEXECUTEINFOW sei = {0};
        sei.cbSize = sizeof(sei);
        sei.lpVerb = L"runas";
        sei.lpFile = installExe;
        sei.lpParameters = L"install";
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        sei.nShow = SW_HIDE;
        sei.hwnd = hwnd;
        if (ShellExecuteExW(&sei)) {
            if (sei.hProcess) {
                WaitForSingleObject(sei.hProcess, 30000);
                CloseHandle(sei.hProcess);
            }
            if (hwnd) MessageBoxW(hwnd, STR(msgDaemonSystem), STR(msgSuccess), MB_OK);
            Log(L"Daemon started via service (SYSTEM fallback)");
        } else {
            if (hwnd) MessageBoxW(hwnd, STR(msgNeedAdmin), STR(msgError), MB_OK);
        }
        return;
    }
    if (mode == 1) {
        /* 管理员模式 fallback: 用 ShellExecuteExW runas 提权启动 guardiand.exe */
        wchar_t daemonPath[MAX_PATH];
        GetGuardianExe(daemonPath, MAX_PATH);
        SHELLEXECUTEINFOW sei = {0};
        sei.cbSize = sizeof(sei);
        sei.lpVerb = L"runas";
        sei.lpFile = daemonPath;
        sei.lpParameters = L"--hidden";
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        sei.nShow = SW_HIDE;
        sei.hwnd = hwnd;
        if (ShellExecuteExW(&sei)) {
            if (sei.hProcess) CloseHandle(sei.hProcess);
            SetAutoStart(TRUE);
            if (hwnd) MessageBoxW(hwnd, STR(msgDaemonAdmin), STR(msgInfo), MB_OK);
            Log(L"Daemon started (Admin fallback)");
        } else {
            if (hwnd) MessageBoxW(hwnd, STR(msgNeedAdmin), STR(msgError), MB_OK);
        }
        return;
    }
    /* 普通模式 fallback */
    if (SpawnGuardDaemon(FALSE)) {
        Log(L"Daemon started (Normal fallback)");
        SetAutoStart(FALSE);
        if (hwnd) MessageBoxW(hwnd, STR(msgDaemonStarted), STR(msgInfo), MB_OK);
    }
}

// ======================== Apply Language ========================
void ApplyLanguage(HWND hwnd) {
    SetWindowTextW(hwnd, STR(wndTitle));
    SetWindowTextW(hLabelRunning, STR(labelRunning));
    SetWindowTextW(hLabelProtected, STR(labelProtected));
    SetWindowTextW(hLabelHint, STR(hint));
    SetWindowTextW(hBtnRefresh, STR(btnRefresh));
    SetWindowTextW(hBtnDaemon, STR(btnDaemon));
    SetWindowTextW(hBtnSettings, STR(btnSettings));
    SetWindowTextW(hBtnLang, STR(btnLang));
    SetWindowTextW(hBtnAutoRefresh, g_autoRefresh ? STR(btnAutoOn) : STR(btnAutoOff));
    // Update columns
    LVCOLUMNW col = {0};
    col.mask = LVCF_TEXT;
    if (g_currentMode == MODE_PROCESS) {
        col.pszText = (wchar_t*)STR(colName);
        ListView_SetColumn(hRunningList, 0, &col);
        col.pszText = (wchar_t*)STR(colPID);
        ListView_SetColumn(hRunningList, 1, &col);
        col.pszText = (wchar_t*)STR(colMemory);
        ListView_SetColumn(hRunningList, 2, &col);
        col.pszText = (wchar_t*)STR(colName);
        ListView_SetColumn(hProtectedList, 0, &col);
        col.pszText = (wchar_t*)STR(colPID);
        ListView_SetColumn(hProtectedList, 1, &col);
        col.pszText = (wchar_t*)STR(colType);
        ListView_SetColumn(hProtectedList, 2, &col);
    }
    RefreshRunningList(hwnd);
    RefreshProtectedList(hwnd);
}

// ======================== GUI ========================
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            // Tab control
            hTab = CreateWindowW(WC_TABCONTROLW, L"",
                                 WS_CHILD | WS_VISIBLE | TCS_FIXEDWIDTH,
                                 10, 8, 380, 28, hwnd,
                                 (HMENU)3001, GetModuleHandle(NULL), NULL);
            SendMessageW(hTab, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            TCITEMW tcItem;
            tcItem.mask = TCIF_TEXT;
            tcItem.pszText = (wchar_t*)STR(labelRunning);
            // Use mode-specific tab labels
            const wchar_t *tabLabels[] = {L"进程管理", L"服务管理", L"注册表管理", L"分区表管理"};
            for (int i = 0; i < 4; i++) {
                tcItem.pszText = (wchar_t*)tabLabels[i];
                TabCtrl_InsertItem(hTab, i, &tcItem);
            }
            // Left label
            hLabelRunning = CreateWindowW(L"static", STR(labelRunning),
                       WS_CHILD | WS_VISIBLE,
                       10, 82, 220, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
            SendMessageW(hLabelRunning, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            // Left list
            hRunningList = CreateWindowExW(0, L"SysListView32", L"",
                                    WS_CHILD | WS_VISIBLE | LVS_REPORT | WS_BORDER,
                                    10, 105, 530, 590, hwnd,
                                    (HMENU)1001, GetModuleHandle(NULL), NULL);
            SendMessageW(hRunningList, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            ListView_SetExtendedListViewStyle(hRunningList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

            // Right label
            hLabelProtected = CreateWindowW(L"static", STR(labelProtected),
                       WS_CHILD | WS_VISIBLE,
                       560, 82, 280, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
            SendMessageW(hLabelProtected, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            // Disk type label
            hLabelDiskType = CreateWindowW(L"static", L"",
                       WS_CHILD,
                       10, 82, 400, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
            SendMessageW(hLabelDiskType, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            ShowWindow(hLabelDiskType, SW_HIDE);
            // Right list
            hProtectedList = CreateWindowExW(0, L"SysListView32", L"",
                                    WS_CHILD | WS_VISIBLE | LVS_REPORT | WS_BORDER,
                                    560, 105, 530, 590, hwnd,
                                    (HMENU)1002, GetModuleHandle(NULL), NULL);
            SendMessageW(hProtectedList, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            ListView_SetExtendedListViewStyle(hProtectedList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            // Registry TreeView (hidden initially, shown in registry mode)
            g_hTreeReg = CreateWindowExW(0, WC_TREEVIEWW, L"",
                                        WS_CHILD | WS_BORDER | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                                        10, 105, 530, 590, hwnd,
                                        (HMENU)1003, GetModuleHandle(NULL), NULL);
            if (g_hTreeReg) {
                SendMessageW(g_hTreeReg, WM_SETFONT, (WPARAM)g_hFont, TRUE);
                ShowWindow(g_hTreeReg, SW_HIDE);
            }

            // Boot sector preview (partition mode)
            // The old 32x17 ListView caused "every click = refresh" because
            // selecting rows pumped LVN_ITEMCHANGED -> re-render. Replaced
            // with a compact label (status) + a 1-line 16-byte hex summary
            // + a single "Edit MBR/GPT" button that opens the byte-level
            // hex editor.
            //
            // Visual: enclosed in a BS_GROUPBOX so it visually belongs to
            // the LEFT panel only (x=10..540), strictly separated from the
            // right "已管理的分区" ListView (x=560..1090) by a 20-px gap.
            hGrpBoot = CreateWindowW(L"button", L"引导扇区 (MBR/GPT)",
                       WS_CHILD | BS_GROUPBOX,
                       10, 340, 530, 280, hwnd, NULL, GetModuleHandle(NULL), NULL);
            SendMessageW(hGrpBoot, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            ShowWindow(hGrpBoot, SW_HIDE);

            hLblBootType = CreateWindowW(L"static", L"引导扇区 (Disk 0) — 等待读取...",
                       WS_CHILD,
                       24, 360, 500, 22, hwnd, NULL, GetModuleHandle(NULL), NULL);
            SendMessageW(hLblBootType, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            ShowWindow(hLblBootType, SW_HIDE);

            /* Monospace font for the hex dump so columns align. */
            g_hFontMono = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
            /* Multi-line hex preview (4 lines of 16 bytes + ASCII + signature).
             * 7 lines * 16px line-height ≈ 112px; give 165px for padding. */
            hLblBootHex = CreateWindowExW(0, L"static", L"  (尚未读取)",
                       WS_CHILD | SS_SUNKEN,
                       24, 386, 500, 165, hwnd, NULL, GetModuleHandle(NULL), NULL);
            SendMessageW(hLblBootHex, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);
            ShowWindow(hLblBootHex, SW_HIDE);

            /* Buttons row (y=560): full-width Edit button + small Reload next to it. */
            hBtnBootEdit = CreateWindowW(L"button", L"编辑 MBR/GPT 字节 (字节级 Hex Editor)...",
                       WS_CHILD | BS_PUSHBUTTON,
                       24, 560, 412, 44, hwnd, (HMENU)1012, GetModuleHandle(NULL), NULL);
            SendMessageW(hBtnBootEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            ShowWindow(hBtnBootEdit, SW_HIDE);

            hBtnBootReload = CreateWindowW(L"button", L"重读",
                       WS_CHILD | BS_PUSHBUTTON,
                       444, 560, 80, 44, hwnd, (HMENU)1013, GetModuleHandle(NULL), NULL);
            SendMessageW(hBtnBootReload, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            ShowWindow(hBtnBootReload, SW_HIDE);

            // Buttons row (below tabs, y=44-78)
            hBtnRefresh = CreateWindowW(L"button", STR(btnRefresh),
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       10, 44, 100, 30, hwnd,
                                       (HMENU)2001, GetModuleHandle(NULL), NULL);
            SendMessageW(hBtnRefresh, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            hBtnDaemon = CreateWindowW(L"button", STR(btnDaemon),
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       120, 44, 130, 30, hwnd,
                                       (HMENU)2005, GetModuleHandle(NULL), NULL);
            SendMessageW(hBtnDaemon, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            hBtnSettings = CreateWindowW(L"button", STR(btnSettings),
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       260, 44, 90, 30, hwnd,
                                       (HMENU)2004, GetModuleHandle(NULL), NULL);
            SendMessageW(hBtnSettings, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            hBtnLang = CreateWindowW(L"button", STR(btnLang),
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       360, 44, 60, 30, hwnd,
                                       (HMENU)2006, GetModuleHandle(NULL), NULL);
            SendMessageW(hBtnLang, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            hBtnAutoRefresh = CreateWindowW(L"button", STR(btnAutoOn),
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       430, 44, 110, 30, hwnd,
                                       (HMENU)2007, GetModuleHandle(NULL), NULL);
            SendMessageW(hBtnAutoRefresh, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            // Hint label on the far right of button row
            hLabelHint = CreateWindowW(L"static", STR(hint),
                       WS_CHILD | WS_VISIBLE | SS_RIGHT,
                       550, 50, 530, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
            SendMessageW(hLabelHint, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            // Setup process mode columns (default)
            LVCOLUMNW col = {0};
            col.mask = LVCF_TEXT | LVCF_WIDTH;
            col.pszText = (wchar_t*)STR(colName); col.cx = 180;
            ListView_InsertColumn(hRunningList, 0, &col);
            col.pszText = (wchar_t*)STR(colPID); col.cx = 70;
            ListView_InsertColumn(hRunningList, 1, &col);
            col.pszText = (wchar_t*)STR(colMemory); col.cx = 80;
            ListView_InsertColumn(hRunningList, 2, &col);
            col.pszText = (wchar_t*)STR(colName); col.cx = 180;
            ListView_InsertColumn(hProtectedList, 0, &col);
            col.pszText = (wchar_t*)STR(colPID); col.cx = 70;
            ListView_InsertColumn(hProtectedList, 1, &col);
            col.pszText = (wchar_t*)STR(colType); col.cx = 100;
            ListView_InsertColumn(hProtectedList, 2, &col);
            // Load all configs
            LoadConfig();
            LoadSvcRepeatedConfig();
            LoadRegRepeatedConfig();
            LoadPartRepeatedConfig();
            LoadLangSetting();
            // Set timer for repeated actions (every 5 seconds)
            SetTimer(hwnd, 1, 5000, NULL);
            RefreshRunningList(hwnd);
            RefreshProtectedList(hwnd);
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == 2001) {  // Refresh
                RefreshForCurrentMode(hwnd);
            } else if (LOWORD(wParam) == 2004) {  // Settings
                ShowSettingsDialog(hwnd);
            } else if (LOWORD(wParam) == 2005) {  // Start daemon
                RECT rc;
                GetWindowRect(GetDlgItem(hwnd, 2005), &rc);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, 1, L"普通权限");
                AppendMenuW(hMenu, MF_STRING, 2, L"管理员权限");
                AppendMenuW(hMenu, MF_STRING, 3, L"SYSTEM权限");
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, rc.left, rc.bottom, 0, hwnd, NULL);
                DestroyMenu(hMenu);
                if (cmd >= 1 && cmd <= 3) StartDaemon(hwnd, cmd - 1);
            } else if (LOWORD(wParam) == 2006) {  // Language
                g_lang = (g_lang == LANG_ZH) ? LANG_EN : LANG_ZH;
                SaveLangSetting();
                ApplyLanguage(hwnd);
            } else if (LOWORD(wParam) == 2007) {  // Toggle auto-refresh
                g_autoRefresh = !g_autoRefresh;
                if (g_autoRefresh) {
                    SetTimer(hwnd, 1, 5000, NULL);
                    SetWindowTextW(hBtnAutoRefresh, STR(btnAutoOn));
                } else {
                    KillTimer(hwnd, 1);
                    SetWindowTextW(hBtnAutoRefresh, STR(btnAutoOff));
                }
            } else if (LOWORD(wParam) == 1012) {  // Boot preview: Edit MBR/GPT
                /* Open the existing byte-level hex editor (IDD_HEXEDIT)
                 * for the currently-previewed disk. We pass the disk
                 * number directly so it skips the disk-picker dialog.
                 * Note: PE_OpenSectorEditorEx already shows a MessageBox
                 * on error (e.g. access denied) — no need to wrap here. */
                PE_OpenSectorEditorEx(hwnd, g_bootDiskNum);
            } else if (LOWORD(wParam) == 1013) {  // Boot preview: Re-read
                BootPreview_Load(hwnd);
            }
            break;
        case WM_CONTEXTMENU: {
            int x = (int)(short)LOWORD(lParam);
            int y = (int)(short)HIWORD(lParam);
            POINT pt = {x, y};
            ScreenToClient(hwnd, &pt);

            // ---- Left panel (running list) context menu ----
            if (ChildWindowFromPoint(hwnd, pt) == hRunningList) {
                int selCount = 0;
                int selItems[100];
                int sel = ListView_GetNextItem(hRunningList, -1, LVNI_SELECTED);
                while (sel >= 0 && selCount < 100) {
                    selItems[selCount++] = sel;
                    sel = ListView_GetNextItem(hRunningList, sel, LVNI_SELECTED);
                }
                if (selCount == 0) break;

                if (g_currentMode == MODE_PROCESS) {
                    // Process mode menu (original functionality preserved)
                    wchar_t names[100][260];
                    for (int i = 0; i < selCount; i++) {
                        LVITEMW lvi = {0};
                        lvi.mask = LVIF_TEXT;
                        lvi.iItem = selItems[i];
                        lvi.pszText = names[i];
                        lvi.cchTextMax = 260;
                        ListView_GetItem(hRunningList, &lvi);
                    }
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, 1, STR(menuKill));
                    AppendMenuW(hMenu, MF_STRING, 2, STR(menuKillTree));
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, 3, STR(menuRepKill));
                    AppendMenuW(hMenu, MF_STRING, 4, STR(menuRepKillTree));
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, 5, STR(menuProtect));
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, 6, STR(menuOpenLoc));
                    AppendMenuW(hMenu, MF_STRING, 7, STR(menuMonitor));
                    AppendMenuW(hMenu, MF_STRING, 8, STR(menuInstallSvc));
                    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, x, y, 0, hwnd, NULL);
                    for (int i = 0; i < selCount; i++) {
                        wchar_t *name = names[i];
                        DWORD pid = GetProcessIDByName(name);
                        switch (cmd) {
                            case 1: KillProcessByPID(pid); Log(L"Kill: %s", name); break;
                            case 2: KillProcessTree(pid); Log(L"KillTree: %s", name); break;
                            case 3: AddToRepeatedList(name); Log(L"RepKill: %s", name); break;
                            case 4: AddToRepeatedTreeList(name); Log(L"RepKillTree: %s", name); break;
                            case 5: AddToProtectedList(name); Log(L"Protect: %s", name); break;
                            case 6: { /* 打开进程位置 */
                                HANDLE hProc6 = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                                if (hProc6) {
                                    wchar_t exePath6[MAX_PATH];
                                    DWORD sz6 = MAX_PATH;
                                    if (QueryFullProcessImageNameW(hProc6, 0, exePath6, &sz6)) {
                                        wchar_t params[MAX_PATH + 32];
                                        swprintf(params, MAX_PATH + 32, L"/select,\"%s\"", exePath6);
                                        ShellExecuteW(NULL, L"open", L"explorer.exe", params, NULL, SW_SHOWNORMAL);
                                    } else {
                                        MessageBoxW(hwnd, L"无法获取进程路径", STR(msgError), MB_OK);
                                    }
                                    CloseHandle(hProc6);
                                } else {
                                    MessageBoxW(hwnd, L"无法打开进程(权限不足)", STR(msgError), MB_OK);
                                }
                                break;
                            }
                            case 7: { /* 实时监控 - 启动 observer.exe */
                                wchar_t observerPath[MAX_PATH];
                                GetModuleFileNameW(NULL, observerPath, MAX_PATH);
                                wchar_t *p7 = wcsrchr(observerPath, L'\\');
                                if (p7) *p7 = L'\0';
                                wcscat(observerPath, L"\\observe\\observer.exe");
                                wchar_t params7[64];
                                swprintf(params7, 64, L"--pid %lu", pid);
                                HINSTANCE hInst7 = ShellExecuteW(NULL, L"open", observerPath, params7, NULL, SW_SHOWNORMAL);
                                if ((INT_PTR)hInst7 <= 32) {
                                    MessageBoxW(hwnd, L"无法启动 observer.exe\n请确保 observe\\observer.exe 存在",
                                               STR(msgError), MB_OK);
                                }
                                break;
                            }
                            case 8: InstallServiceAsSystem(hwnd); break;
                        }
                    }
                    RefreshRunningList(hwnd);
                    RefreshProtectedList(hwnd);
                    DestroyMenu(hMenu);
                }
                else if (g_currentMode == MODE_SERVICE) {
                    // Service mode menu
                    wchar_t names[100][256];
                    for (int i = 0; i < selCount; i++) {
                        LVITEMW lvi = {0};
                        lvi.mask = LVIF_TEXT;
                        lvi.iItem = selItems[i];
                        lvi.pszText = names[i];
                        lvi.cchTextMax = 256;
                        ListView_GetItem(hRunningList, &lvi);
                    }
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, 10, STR(menuStopSvc));
                    AppendMenuW(hMenu, MF_STRING, 11, STR(menuDeleteSvc));
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, 12, STR(menuRepDeleteSvc));
                    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, x, y, 0, hwnd, NULL);
                    for (int i = 0; i < selCount; i++) {
                        wchar_t *name = names[i];
                        switch (cmd) {
                            case 10: if (svcStop) svcStop(name); Log(L"Stop service: %s", name); break;
                            case 11: if (svcDelete) svcDelete(name); Log(L"Delete service: %s", name); break;
                            case 12: AddSvcToRepeated(name); break;
                        }
                    }
                    RefreshForCurrentMode(hwnd);
                    DestroyMenu(hMenu);
                }
                else if (g_currentMode == MODE_REGISTRY) {
                    // Registry mode menu
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, 20, STR(menuDeleteReg));
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, 21, STR(menuRepDeleteReg));
                    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, x, y, 0, hwnd, NULL);
                    // Get selected registry paths
                    for (int i = 0; i < selCount; i++) {
                        wchar_t path[512];
                        LVITEMW lvi = {0};
                        lvi.mask = LVIF_TEXT;
                        lvi.iItem = selItems[i];
                        lvi.iSubItem = 1;
                        lvi.pszText = path;
                        lvi.cchTextMax = 512;
                        ListView_GetItem(hRunningList, &lvi);
                        if (cmd == 20 && regDelete) {
                            // Confirm before deleting registry
                            wchar_t msg[1024];
                            swprintf(msg, 1024, L"确定要删除以下注册表项吗？\n\n%s\n\n此操作不可撤销！", path);
                            int ret = MessageBoxW(hwnd, msg, L"确认删除", MB_YESNO | MB_ICONWARNING);
                            if (ret == IDYES) {
                                // Parse registry path
                                HKEY hRoot = HKEY_LOCAL_MACHINE;
                                const wchar_t *subKey = path;
                                if (wcsncmp(path, L"HKEY_LOCAL_MACHINE\\", 19) == 0) {
                                    hRoot = HKEY_LOCAL_MACHINE; subKey = path + 19;
                                } else if (wcsncmp(path, L"HKEY_CURRENT_USER\\", 18) == 0) {
                                    hRoot = HKEY_CURRENT_USER; subKey = path + 18;
                                } else if (wcsncmp(path, L"HKEY_CLASSES_ROOT\\", 18) == 0) {
                                    hRoot = HKEY_CLASSES_ROOT; subKey = path + 18;
                                }
                                regDelete(hRoot, subKey, NULL);
                                Log(L"Delete registry: %s", path);
                            }
                        }
                        else if (cmd == 21) AddRegToRepeated(path);
                    }
                    if (cmd == 21) RefreshForCurrentMode(hwnd);
                    DestroyMenu(hMenu);
                }
                else if (g_currentMode == MODE_PARTITION) {
                    // Partition mode menu
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, 30, STR(menuDeletePart));
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, 31, STR(menuRepDeletePart));
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, 32, L"Protect Disk (snapshot sector 0)");
                    AppendMenuW(hMenu, MF_STRING, 33, L"Edit Sector 0 (Hex)");
                    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, x, y, 0, hwnd, NULL);
                    // Get selected partition info
                    for (int i = 0; i < selCount; i++) {
                        wchar_t loc[512];
                        LVITEMW lvi = {0};
                        lvi.mask = LVIF_TEXT;
                        lvi.iItem = selItems[i];
                        lvi.iSubItem = 1;
                        lvi.pszText = loc;
                        lvi.cchTextMax = 512;
                        ListView_GetItem(hRunningList, &lvi);
                        if (cmd == 30 && partDelete) {
                            // Get disk and partition number from list
                            wchar_t partStr[64], diskStr[64];
                            LVITEMW lvi2 = {0};
                            lvi2.mask = LVIF_TEXT;
                            lvi2.iItem = selItems[i];
                            lvi2.iSubItem = 3;
                            lvi2.pszText = partStr;
                            lvi2.cchTextMax = 64;
                            ListView_GetItem(hRunningList, &lvi2);
                            lvi2.iSubItem = 4;
                            lvi2.pszText = diskStr;
                            ListView_GetItem(hRunningList, &lvi2);
                            int partNum = 0, diskNum = 0;
                            swscanf(partStr, L"Partition %d", &partNum);
                            swscanf(diskStr, L"Disk %d", &diskNum);
                            partDelete(diskNum, partNum);
                            Log(L"Delete partition: disk=%d, part=%d", diskNum, partNum);
                        }
                        else if (cmd == 31) {
                            // Add to repeated delete list
                            wchar_t partStr[64], diskStr[64];
                            LVITEMW lvi2 = {0};
                            lvi2.mask = LVIF_TEXT;
                            lvi2.iItem = selItems[i];
                            lvi2.iSubItem = 3;
                            lvi2.pszText = partStr;
                            lvi2.cchTextMax = 64;
                            ListView_GetItem(hRunningList, &lvi2);
                            lvi2.iSubItem = 4;
                            lvi2.pszText = diskStr;
                            ListView_GetItem(hRunningList, &lvi2);
                            int partNum = 0, diskNum = 0;
                            swscanf(partStr, L"Partition %d", &partNum);
                            swscanf(diskStr, L"Disk %d", &diskNum);
                            AddPartToRepeated(diskNum, partNum, loc);
                        }
                    }
                    if (cmd == 31) RefreshForCurrentMode(hwnd);
                    else if (cmd == 32) {
                        // Protect selected disk: snapshot sector 0
                        int sel = ListView_GetNextItem(hRunningList, -1, LVNI_SELECTED);
                        if (sel >= 0) {
                            wchar_t buf[32] = {0};
                            LVITEMW lvi = {0};
                            lvi.mask = LVIF_TEXT;
                            lvi.iItem = sel;
                            lvi.iSubItem = 4;
                            lvi.pszText = buf; lvi.cchTextMax = 32;
                            ListView_GetItem(hRunningList, &lvi);
                            int disk = _wtoi(buf);
                            if (PE_AddToProtected(disk)) {
                                MessageBoxW(hwnd, L"Disk protected. guardiand will restore sector 0 when modified.", L"Done", MB_OK);
                            }
                        }
                    } else if (cmd == 33) {
                        PE_OpenSectorEditor(hwnd);
                    }
                    DestroyMenu(hMenu);
                }
            }
            // ---- TreeView (registry mode) context menu ----
            // TreeView sends NM_RCLICK notify (handled in WM_NOTIFY), not WM_CONTEXTMENU,
            // so this block is no longer needed here.
            // ---- Right panel (protected/managed list) context menu ----
            else if (ChildWindowFromPoint(hwnd, pt) == hProtectedList) {
                int sel = ListView_GetNextItem(hProtectedList, -1, LVNI_SELECTED);
                if (sel == -1) break;
                if (g_currentMode == MODE_PROCESS) {
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, 40, STR(menuCancelRep));
                    AppendMenuW(hMenu, MF_STRING, 41, STR(menuCancelProt));
                    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, x, y, 0, hwnd, NULL);
                    if (cmd == 40 || cmd == 41) {
                        RemoveFromList(sel);
                    }
                    RefreshProtectedList(hwnd);
                    DestroyMenu(hMenu);
                }
                else if (g_currentMode == MODE_SERVICE) {
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, 50, STR(menuCancelRepSvc));
                    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, x, y, 0, hwnd, NULL);
                    if (cmd == 50) {
                        wchar_t name[256];
                        LVITEMW lvi = {0};
                        lvi.mask = LVIF_TEXT;
                        lvi.iItem = sel;
                        lvi.pszText = name;
                        lvi.cchTextMax = 256;
                        ListView_GetItem(hProtectedList, &lvi);
                        int idx = FindSvcRepeated(name);
                        if (idx >= 0) RemoveSvcFromRepeated(idx);
                        RefreshForCurrentMode(hwnd);
                    }
                    DestroyMenu(hMenu);
                }
                else if (g_currentMode == MODE_REGISTRY) {
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, 60, STR(menuCancelRepReg));
                    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, x, y, 0, hwnd, NULL);
                    if (cmd == 60) {
                        wchar_t path[512];
                        LVITEMW lvi = {0};
                        lvi.mask = LVIF_TEXT;
                        lvi.iItem = sel;
                        lvi.pszText = path;
                        lvi.cchTextMax = 512;
                        ListView_GetItem(hProtectedList, &lvi);
                        int idx = FindRegRepeated(path);
                        if (idx >= 0) RemoveRegFromRepeated(idx);
                        RefreshForCurrentMode(hwnd);
                    }
                    DestroyMenu(hMenu);
                }
                else if (g_currentMode == MODE_PARTITION) {
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, 70, L"Edit Sector 0 (Hex)");
                    AppendMenuW(hMenu, MF_STRING, 71, L"Protect Disk");
                    AppendMenuW(hMenu, MF_STRING, 72, L"Repeated Delete Selected");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, 73, STR(menuCancelRepPart));
                    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, x, y, 0, hwnd, NULL);
                    if (cmd == 70) {
                        PE_OpenSectorEditor(hwnd);
                    } else if (cmd == 71) {
                        int sel = ListView_GetNextItem(hRunningList, -1, LVNI_SELECTED);
                        int disk = 0;
                        if (sel >= 0) {
                            wchar_t buf[32] = {0};
                            LVITEMW lvi = {0}; lvi.iItem = sel; lvi.iSubItem = 4;
                            lvi.pszText = buf; lvi.cchTextMax = 32;
                            ListView_GetItem(hRunningList, &lvi);
                            disk = _wtoi(buf);
                        }
                        if (PE_AddToProtected(disk)) {
                            MessageBoxW(hwnd, L"Disk protected. guardiand will restore partition table when modified.", L"Done", MB_OK);
                        }
                    } else if (cmd == 72) {
                        int sel = ListView_GetNextItem(hRunningList, -1, LVNI_SELECTED);
                        if (sel >= 0) {
                            wchar_t buf[32] = {0};
                            LVITEMW lvi = {0}; lvi.iItem = sel; lvi.iSubItem = 4;
                            lvi.pszText = buf; lvi.cchTextMax = 32;
                            ListView_GetItem(hRunningList, &lvi);
                            int disk = _wtoi(buf);
                            PE_AddToRepeatedDelete(disk, sel);
                            MessageBoxW(hwnd, L"Added to repeated delete list.", L"Done", MB_OK);
                        }
                    } else if (cmd == 73) {
                        RefreshForCurrentMode(hwnd);
                    }
                    DestroyMenu(hMenu);
                }
            }
            break;
        }
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->hwndFrom == hTab && pnmh->code == TCN_SELCHANGE) {
                int sel = TabCtrl_GetCurSel(hTab);
                g_currentMode = (AppMode)sel;
                RefreshForCurrentMode(hwnd);
            }
            // Handle TreeView notifications (registry mode)
            if (g_hTreeReg && pnmh->hwndFrom == g_hTreeReg)
                RT_OnNotify(pnmh);
            // (Boot preview no longer has a ListView; nothing to handle here.)
            break;
        }
        case WM_TIMER: {
            /* GUI 只负责显示，不执行任何保护逻辑。
             * 所有保护工作由 guardiand.exe 完成。
             * 仅在自动刷新开启时刷新进程列表。 */
            if (g_autoRefresh && g_currentMode == MODE_PROCESS) {
                RefreshRunningList(hwnd);
            }
            break;
        }
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// ======================== List Refresh Functions ========================
void RefreshRunningList(HWND hwnd) {
    (void)hwnd;
    ListView_DeleteAllItems(hRunningList);
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe = {0};
    pe.dwSize = sizeof(PROCESSENTRY32W);
    if (!Process32FirstW(hSnap, &pe)) { CloseHandle(hSnap); return; }
    int i = 0;
    do {
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = pe.szExeFile;
        ListView_InsertItem(hRunningList, &lvi);
        wchar_t pidStr[32];
        swprintf(pidStr, 32, L"%lu", (unsigned long)pe.th32ProcessID);
        lvi.iSubItem = 1;
        lvi.pszText = pidStr;
        ListView_SetItem(hRunningList, &lvi);
        DWORD memKB = 0;
        HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
        if (hProc) {
            PROCESS_MEMORY_COUNTERS pmc;
            if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc)))
                memKB = pmc.WorkingSetSize / 1024;
            CloseHandle(hProc);
        }
        wchar_t memStr[32];
        swprintf(memStr, 32, L"%lu", (unsigned long)memKB);
        lvi.iSubItem = 2;
        lvi.pszText = memStr;
        ListView_SetItem(hRunningList, &lvi);
        i++;
    } while (Process32NextW(hSnap, &pe));
    CloseHandle(hSnap);
}

void RefreshProtectedList(HWND hwnd) {
    (void)hwnd;
    ListView_DeleteAllItems(hProtectedList);
    for (int i = 0; i < g_protectedCount; i++) {
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = g_protectedList[i].name;
        ListView_InsertItem(hProtectedList, &lvi);
        wchar_t pidStr[32];
        DWORD pid = g_protectedList[i].pid;
        if (pid == 0) pid = GetProcessIDByName(g_protectedList[i].name);
        swprintf(pidStr, 32, L"%lu", (unsigned long)pid);
        lvi.iSubItem = 1;
        lvi.pszText = pidStr;
        ListView_SetItem(hProtectedList, &lvi);
        lvi.iSubItem = 2;
        if (g_protectedList[i].isRepeated)
            lvi.pszText = g_protectedList[i].isTree ? (wchar_t*)STR(typeRepKillTree) : (wchar_t*)STR(typeRepKill);
        else
            lvi.pszText = (wchar_t*)STR(typeProt);
        ListView_SetItem(hProtectedList, &lvi);
    }
}

// Refresh UI for current mode
void RefreshForCurrentMode(HWND hwnd) {
    ListView_DeleteAllItems(hRunningList);
    ListView_DeleteAllItems(hProtectedList);
    LVCOLUMNW col = {0};
    col.mask = LVCF_TEXT | LVCF_WIDTH;

    // Show/hide controls based on mode
    if (g_currentMode == MODE_REGISTRY) {
        ShowWindow(hRunningList, SW_HIDE);
        ShowWindow(g_hTreeReg, SW_SHOW);
        RT_InitTreeView(g_hTreeReg);
        // Boot preview only in partition mode
        ShowWindow(hLblBootHex, SW_HIDE);
        ShowWindow(hBtnBootEdit, SW_HIDE);
        ShowWindow(hBtnBootReload, SW_HIDE);
        ShowWindow(hLblBootType, SW_HIDE);
        if (hGrpBoot) ShowWindow(hGrpBoot, SW_HIDE);
        // Full-size TreeView
        MoveWindow(g_hTreeReg, 10, 105, 530, 590, TRUE);
        MoveWindow(hProtectedList, 560, 105, 530, 590, TRUE);
    } else {
        ShowWindow(hRunningList, SW_SHOW);
        ShowWindow(g_hTreeReg, SW_HIDE);
        if (g_currentMode == MODE_PARTITION) {
            // Left column: running list (top, h=190) + GroupBox for boot (bottom, h=280).
            // Running list:    y=105..295  (h=190)
            // Boot GroupBox:   y=340..620  (h=280, x=10..540)
            //   status label:  y=360
            //   hex dump:      y=386 (h=165, multi-line, monospace)
            //   edit + reload: y=560 (h=44, edit full-width minus reload column)
            // Right "已管理" list: y=105..695  (full height, x=560..1090)
            // -> No vertical overlap, no horizontal overlap (gap = 20px).
            MoveWindow(hRunningList, 10, 105, 530, 190, TRUE);
            MoveWindow(hProtectedList, 560, 105, 530, 590, TRUE);
            if (hGrpBoot)        ShowWindow(hGrpBoot,    SW_SHOW);
            ShowWindow(hLblBootType, SW_SHOW);
            ShowWindow(hLblBootHex,  SW_SHOW);
            ShowWindow(hBtnBootEdit, SW_SHOW);
            ShowWindow(hBtnBootReload, SW_SHOW);
            BootPreview_Load(hwnd);
        } else {
            if (hGrpBoot)        ShowWindow(hGrpBoot,    SW_HIDE);
            ShowWindow(hLblBootHex, SW_HIDE);
            ShowWindow(hBtnBootEdit, SW_HIDE);
            ShowWindow(hBtnBootReload, SW_HIDE);
            ShowWindow(hLblBootType, SW_HIDE);
            // Full-size list in other modes
            MoveWindow(hRunningList, 10, 105, 530, 590, TRUE);
            MoveWindow(hProtectedList, 560, 105, 530, 590, TRUE);
        }
    }

    switch (g_currentMode) {
        case MODE_PROCESS:
            SetWindowTextW(hLabelRunning, STR(labelRunning));
            SetWindowTextW(hLabelProtected, STR(labelProtected));
            ShowWindow(hLabelDiskType, SW_HIDE);
            // Reset columns
            while (GetListViewColCount(hRunningList) > 0)
                ListView_DeleteColumn(hRunningList, 0);
            col.pszText = (wchar_t*)STR(colName); col.cx = 180;
            ListView_InsertColumn(hRunningList, 0, &col);
            col.pszText = (wchar_t*)STR(colPID); col.cx = 70;
            ListView_InsertColumn(hRunningList, 1, &col);
            col.pszText = (wchar_t*)STR(colMemory); col.cx = 80;
            ListView_InsertColumn(hRunningList, 2, &col);
            while (GetListViewColCount(hProtectedList) > 0)
                ListView_DeleteColumn(hProtectedList, 0);
            col.pszText = (wchar_t*)STR(colName); col.cx = 180;
            ListView_InsertColumn(hProtectedList, 0, &col);
            col.pszText = (wchar_t*)STR(colPID); col.cx = 70;
            ListView_InsertColumn(hProtectedList, 1, &col);
            col.pszText = (wchar_t*)STR(colType); col.cx = 100;
            ListView_InsertColumn(hProtectedList, 2, &col);
            RefreshRunningList(hwnd);
            RefreshProtectedList(hwnd);
            break;
        case MODE_SERVICE:
            SetWindowTextW(hLabelRunning, L"系统服务：");
            SetWindowTextW(hLabelProtected, L"已管理的服务：");
            ShowWindow(hLabelDiskType, SW_HIDE);
            while (GetListViewColCount(hRunningList) > 0)
                ListView_DeleteColumn(hRunningList, 0);
            col.pszText = (wchar_t*)STR(colSvcName); col.cx = 150;
            ListView_InsertColumn(hRunningList, 0, &col);
            col.pszText = (wchar_t*)STR(colSvcDisp); col.cx = 150;
            ListView_InsertColumn(hRunningList, 1, &col);
            col.pszText = (wchar_t*)STR(colSvcStatus); col.cx = 80;
            ListView_InsertColumn(hRunningList, 2, &col);
            col.pszText = (wchar_t*)STR(colSvcStart); col.cx = 80;
            ListView_InsertColumn(hRunningList, 3, &col);
            // Right panel: managed services
            while (GetListViewColCount(hProtectedList) > 0)
                ListView_DeleteColumn(hProtectedList, 0);
            col.pszText = (wchar_t*)STR(colSvcName); col.cx = 200;
            ListView_InsertColumn(hProtectedList, 0, &col);
            col.pszText = (wchar_t*)STR(colType); col.cx = 100;
            ListView_InsertColumn(hProtectedList, 1, &col);
            // Populate left: services
            {
                ServiceEntry *services = NULL;
                int count = 0;
                if (svcGetAll) count = svcGetAll(&services);
                for (int i = 0; i < count; i++) {
                    LVITEMW lvi = {0};
                    lvi.mask = LVIF_TEXT;
                    lvi.iItem = i;
                    lvi.iSubItem = 0;
                    lvi.pszText = services[i].name;
                    ListView_InsertItem(hRunningList, &lvi);
                    lvi.iSubItem = 1; lvi.pszText = services[i].displayName; ListView_SetItem(hRunningList, &lvi);
                    lvi.iSubItem = 2; lvi.pszText = services[i].statusStr; ListView_SetItem(hRunningList, &lvi);
                    lvi.iSubItem = 3; lvi.pszText = services[i].startTypeStr; ListView_SetItem(hRunningList, &lvi);
                }
                if (services) free(services);
            }
            // Populate right: repeated-delete services
            for (int i = 0; i < g_svcRepeatedCount; i++) {
                LVITEMW lvi = {0};
                lvi.mask = LVIF_TEXT;
                lvi.iItem = i;
                lvi.iSubItem = 0;
                lvi.pszText = g_svcRepeated[i].name;
                ListView_InsertItem(hProtectedList, &lvi);
                lvi.iSubItem = 1;
                lvi.pszText = (wchar_t*)STR(typeRepSvc);
                ListView_SetItem(hProtectedList, &lvi);
            }
            break;
        case MODE_REGISTRY:
            SetWindowTextW(hLabelRunning, L"注册表项：");
            SetWindowTextW(hLabelProtected, L"已管理的注册表项：");
            ShowWindow(hLabelDiskType, SW_HIDE);
            while (GetListViewColCount(hRunningList) > 0)
                ListView_DeleteColumn(hRunningList, 0);
            col.pszText = (wchar_t*)STR(colRegName); col.cx = 150;
            ListView_InsertColumn(hRunningList, 0, &col);
            col.pszText = (wchar_t*)STR(colRegPath); col.cx = 200;
            ListView_InsertColumn(hRunningList, 1, &col);
            col.pszText = (wchar_t*)STR(colRegType); col.cx = 80;
            ListView_InsertColumn(hRunningList, 2, &col);
            col.pszText = (wchar_t*)STR(colRegValue); col.cx = 100;
            ListView_InsertColumn(hRunningList, 3, &col);
            while (GetListViewColCount(hProtectedList) > 0)
                ListView_DeleteColumn(hProtectedList, 0);
            col.pszText = (wchar_t*)STR(colRegPath); col.cx = 300;
            ListView_InsertColumn(hProtectedList, 0, &col);
            col.pszText = (wchar_t*)STR(colType); col.cx = 100;
            ListView_InsertColumn(hProtectedList, 1, &col);
            // Left: registry entries
            {
                RegistryEntry *entries = NULL;
                int count = 0;
                if (regGetAll) count = regGetAll(HKEY_LOCAL_MACHINE, L"Software", &entries);
                for (int i = 0; i < count; i++) {
                    LVITEMW lvi = {0};
                    lvi.mask = LVIF_TEXT;
                    lvi.iItem = i;
                    lvi.iSubItem = 0; lvi.pszText = entries[i].name; ListView_InsertItem(hRunningList, &lvi);
                    lvi.iSubItem = 1; lvi.pszText = entries[i].path; ListView_SetItem(hRunningList, &lvi);
                    lvi.iSubItem = 2; lvi.pszText = entries[i].type; ListView_SetItem(hRunningList, &lvi);
                    lvi.iSubItem = 3; lvi.pszText = entries[i].value; ListView_SetItem(hRunningList, &lvi);
                }
                if (entries) free(entries);
            }
            // Right: repeated-delete registry entries
            for (int i = 0; i < g_regRepeatedCount; i++) {
                LVITEMW lvi = {0};
                lvi.mask = LVIF_TEXT;
                lvi.iItem = i;
                lvi.iSubItem = 0; lvi.pszText = g_regRepeated[i].fullPath; ListView_InsertItem(hProtectedList, &lvi);
                lvi.iSubItem = 1; lvi.pszText = (wchar_t*)STR(typeRepReg); ListView_SetItem(hProtectedList, &lvi);
            }
            break;
        case MODE_PARTITION:
            SetWindowTextW(hLabelRunning, L"磁盘分区：");
            SetWindowTextW(hLabelProtected, L"已管理的分区：");
            ShowWindow(hLabelDiskType, SW_SHOW);
            // Show disk type
            {
                DiskInfo *disks = NULL;
                int diskCount = 0;
                if (partGetDisks) diskCount = partGetDisks(&disks);
                if (diskCount > 0 && partGetTypeStr) {
                    wchar_t buf[128];
                    swprintf(buf, 128, L"磁盘 0 分区表类型：%s", partGetTypeStr(disks[0].tableType));
                    SetWindowTextW(hLabelDiskType, buf);
                }
                if (disks) free(disks);
            }
            while (GetListViewColCount(hRunningList) > 0)
                ListView_DeleteColumn(hRunningList, 0);
            col.pszText = (wchar_t*)STR(colPartName); col.cx = 120;
            ListView_InsertColumn(hRunningList, 0, &col);
            col.pszText = (wchar_t*)STR(colPartLoc); col.cx = 150;
            ListView_InsertColumn(hRunningList, 1, &col);
            col.pszText = (wchar_t*)STR(colPartId); col.cx = 120;
            ListView_InsertColumn(hRunningList, 2, &col);
            col.pszText = (wchar_t*)STR(colPartPart); col.cx = 100;
            ListView_InsertColumn(hRunningList, 3, &col);
            col.pszText = (wchar_t*)STR(colPartDisk); col.cx = 120;
            ListView_InsertColumn(hRunningList, 4, &col);
            while (GetListViewColCount(hProtectedList) > 0)
                ListView_DeleteColumn(hProtectedList, 0);
            col.pszText = (wchar_t*)STR(colPartLoc); col.cx = 250;
            ListView_InsertColumn(hProtectedList, 0, &col);
            col.pszText = (wchar_t*)STR(colType); col.cx = 100;
            ListView_InsertColumn(hProtectedList, 1, &col);
            // Left: partitions
            {
                DiskInfo *disks = NULL;
                int diskCount = 0;
                if (partGetDisks) diskCount = partGetDisks(&disks);
                int idx = 0;
                for (int d = 0; d < diskCount; d++) {
                    PartitionEntry *parts = NULL;
                    int partCount = 0;
                    if (partGetParts) partCount = partGetParts(disks[d].diskNumber, &parts);
                    for (int p = 0; p < partCount; p++) {
                        LVITEMW lvi = {0};
                        lvi.mask = LVIF_TEXT;
                        lvi.iItem = idx++;
                        lvi.iSubItem = 0; lvi.pszText = parts[p].name; ListView_InsertItem(hRunningList, &lvi);
                        lvi.iSubItem = 1; lvi.pszText = parts[p].location; ListView_SetItem(hRunningList, &lvi);
                        lvi.iSubItem = 2; lvi.pszText = parts[p].id; ListView_SetItem(hRunningList, &lvi);
                        wchar_t buf[64];
                        swprintf(buf, 64, L"Partition %d", parts[p].partitionNumber);
                        lvi.iSubItem = 3; lvi.pszText = buf; ListView_SetItem(hRunningList, &lvi);
                        swprintf(buf, 64, L"Disk %d", parts[p].diskNumber);
                        lvi.iSubItem = 4; lvi.pszText = buf; ListView_SetItem(hRunningList, &lvi);
                    }
                    if (parts) free(parts);
                }
                if (disks) free(disks);
            }
            // Right: repeated-delete partitions
            for (int i = 0; i < g_partRepeatedCount; i++) {
                LVITEMW lvi = {0};
                lvi.mask = LVIF_TEXT;
                lvi.iItem = i;
                lvi.iSubItem = 0; lvi.pszText = g_partRepeated[i].location; ListView_InsertItem(hProtectedList, &lvi);
                lvi.iSubItem = 1; lvi.pszText = (wchar_t*)STR(typeRepPart); ListView_SetItem(hProtectedList, &lvi);
            }
            break;
    }
}

// ======================== Settings Dialog ========================
void ShowSettingsDialog(HWND hwnd) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (p) *p = L'\0';
    wcscat(exePath, L"\\settings.exe");
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;
    CreateProcessW(exePath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

// ======================== Install SYSTEM Service ========================
void InstallServiceAsSystem(HWND hwnd) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (p) *p = L'\0';
    // Delete old service
    {
        SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if (hSCM) {
            SC_HANDLE hSvc = OpenServiceW(hSCM, L"GuardianDaemon", DELETE);
            if (hSvc) { DeleteService(hSvc); CloseServiceHandle(hSvc); }
            CloseServiceHandle(hSCM);
        }
    }
    wcscat(exePath, L"\\service_wrapper.exe");
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hSCM) {
        SC_HANDLE hSvc = CreateServiceW(
            hSCM, L"GuardianDaemon", L"Guardian Daemon",
            SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
            exePath, NULL, NULL, NULL, NULL, NULL);
        if (hSvc) {
            CloseServiceHandle(hSvc);
            hSvc = OpenServiceW(hSCM, L"GuardianDaemon", SERVICE_ALL_ACCESS);
            if (hSvc) {
                StartServiceW(hSvc, 0, NULL);
                CloseServiceHandle(hSvc);
                MessageBoxW(hwnd, STR(msgSvcInstalled), STR(msgSuccess), MB_OK);
                CloseServiceHandle(hSCM);
                return;
            }
        }
        CloseServiceHandle(hSCM);
    }
    MessageBoxW(hwnd, STR(msgSvcFail), STR(msgError), MB_OK);
}

// ======================== Load Core DLLs ========================
static void LoadCoreDlls(void) {
    wchar_t basePath[MAX_PATH];
    GetModuleFileNameW(NULL, basePath, MAX_PATH);
    wchar_t *p = wcsrchr(basePath, L'\\');
    if (p) *p = L'\0';

    // service_manager.dll
    {
        wchar_t path[MAX_PATH];
        wcscpy(path, basePath);
        wcscat(path, L"\\service_manager.dll");
        HMODULE h = LoadLibraryW(path);
        if (h) {
            svcGetAll    = (FnGetAllServices)GetProcAddress(h, "GetAllServices");
            svcDelete   = (FnDeleteServiceByName)GetProcAddress(h, "DeleteServiceByName");
            svcStop     = (FnStopServiceByName)GetProcAddress(h, "StopServiceByName");
            svcAddRep   = (FnAddSvcRepeated)GetProcAddress(h, "AddToRepeatedDeleteList");
            svcRemRep   = (FnRemoveSvcRepeated)GetProcAddress(h, "RemoveFromRepeatedDeleteList");
            svcIsRep    = (FnIsSvcRepeated)GetProcAddress(h, "IsServiceRepeatedDelete");
            Log(L"service_manager.dll loaded: %p", h);
        } else {
            Log(L"Failed to load service_manager.dll: %lu", GetLastError());
        }
    }
    // registry_manager.dll
    {
        wchar_t path[MAX_PATH];
        wcscpy(path, basePath);
        wcscat(path, L"\\registry_manager.dll");
        HMODULE h = LoadLibraryW(path);
        if (h) {
            regGetAll  = (FnGetRegEntries)GetProcAddress(h, "GetRegistryEntries");
            regDelete  = (FnDeleteRegEntry)GetProcAddress(h, "DeleteRegistryEntry");
            regAddRep = (FnAddRegRepeated)GetProcAddress(h, "AddToRepeatedDeleteList");
            regRemRep = (FnRemoveRegRepeated)GetProcAddress(h, "RemoveFromRepeatedDeleteList");
            regIsRep  = (FnIsRegRepeated)GetProcAddress(h, "IsRegistryRepeatedDelete");
            Log(L"registry_manager.dll loaded: %p", h);
        } else {
            Log(L"Failed to load registry_manager.dll: %lu", GetLastError());
        }
    }
    // partition_manager.dll
    {
        wchar_t path[MAX_PATH];
        wcscpy(path, basePath);
        wcscat(path, L"\\partition_manager.dll");
        HMODULE h = LoadLibraryW(path);
        if (h) {
            partGetDisks   = (FnGetAllDisks)GetProcAddress(h, "GetAllDisks");
            partGetParts   = (FnGetPartitionsOnDisk)GetProcAddress(h, "GetPartitionsOnDisk");
            partGetTypeStr = (FnGetPartTableTypeStr)GetProcAddress(h, "GetPartitionTableTypeString");
            partDelete     = (FnDeletePartition)GetProcAddress(h, "DeletePartition");
            partAddRep     = (FnAddPartRepeated)GetProcAddress(h, "AddToRepeatedDeleteList");
            partRemRep     = (FnRemovePartRepeated)GetProcAddress(h, "RemoveFromRepeatedDeleteList");
            partIsRep      = (FnIsPartRepeated)GetProcAddress(h, "IsPartitionRepeatedDelete");
            Log(L"partition_manager.dll loaded: %p", h);
        } else {
            Log(L"Failed to load partition_manager.dll: %lu", GetLastError());
        }
    }
}

// ======================== Main ========================
int wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
    CreateDirectoryW(L"data", NULL);
    InitLogger();
    InitializeCriticalSection(&g_cs);
    LoadLangSetting();
    LoadCoreDlls();  // Load service/registry/partition DLLs
    LoadConfig();

    INITCOMMONCONTROLSEX icex = {sizeof(icex), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"GuardianApp";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND hMainWindow = CreateWindowExW(0, L"GuardianApp", STR(wndTitle),
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                1100, 800, NULL, NULL, hInstance, NULL);
    if (!hMainWindow) return 1;
    ShowWindow(hMainWindow, SW_SHOW);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CloseLogger();
    return (int)msg.wParam;
}
