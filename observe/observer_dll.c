/**
 * observer_dll.c - 进程监控数据采集DLL
 *
 * 导出函数: GetProcessStatsEx(pid, &cpu, &gpu, &memMB, &vramMB)
 *
 * CPU   : GetProcessTimes + 增量计算
 * GPU   : PDH GPU Engine 计数器(按PID过滤)
 * Memory: GetProcessMemoryInfo (Working Set)
 * VRAM  : PDH GPU Adapter Memory (Dedicated Usage, 系统总量)
 */

#define UNICODE
#define _UNICODE
#define BUILDING_DLL

#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pdh.h>
#include <pdhmsg.h>

#include "observer_dll.h"

/* ======================== CPU 状态跟踪 ======================== */
typedef struct {
    ULONGLONG lastTime;
    ULONGLONG lastKernel;
    ULONGLONG lastUser;
    BOOL initialized;
} CpuState;

static CpuState g_cpuState = {0};

/* ======================== GPU PDH 状态 ======================== */
typedef struct {
    HQUERY    hQuery;
    HCOUNTER *counters;
    int       counterCount;
    DWORD     pid;
    BOOL      initialized;
    BOOL      available;
} GpuState;

static GpuState g_gpuState = {0};

/* ======================== VRAM PDH 状态 ======================== */
typedef struct {
    HQUERY   hQuery;
    HCOUNTER hCounter;
    BOOL     initialized;
    BOOL     available;
} VramState;

static VramState g_vramState = {0};

/* ======================== 本地化计数器名称查找 ========================
 * PDH 计数器路径需要使用系统本地化名称，不能硬编码英文。
 * 通过读取注册表 Perflib\009 (English) 找到索引，再用
 * PdhLookupPerfNameByIndexW 获取本地化名称。
 */
static DWORD FindPerfIndex(const wchar_t *englishName) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Perflib\\009",
            0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return 0;

    DWORD type = 0, size = 0;
    if (RegQueryValueExW(hKey, L"Counter", NULL, &type, NULL, &size) != ERROR_SUCCESS || size == 0) {
        RegCloseKey(hKey);
        return 0;
    }

    wchar_t *data = (wchar_t *)malloc(size);
    if (!data) { RegCloseKey(hKey); return 0; }

    if (RegQueryValueExW(hKey, L"Counter", NULL, &type, (BYTE *)data, &size) != ERROR_SUCCESS) {
        free(data);
        RegCloseKey(hKey);
        return 0;
    }
    RegCloseKey(hKey);

    /* 多字符串格式: "index\0name\0index\0name\0...\0\0" */
    DWORD index = 0;
    wchar_t *p = data;
    wchar_t *end = data + size / sizeof(wchar_t);

    while (p < end && *p) {
        DWORD idx = (DWORD)_wtoi(p);
        p += wcslen(p) + 1;
        if (p >= end || !*p) break;
        if (_wcsicmp(p, englishName) == 0) {
            index = idx;
            break;
        }
        p += wcslen(p) + 1;
    }

    free(data);
    return index;
}

/* 获取本地化的计数器对象名和计数器名 */
static BOOL GetLocalizedNames(const wchar_t *engObj, const wchar_t *engCtr,
                               wchar_t *locObj, int locObjSz,
                               wchar_t *locCtr, int locCtrSz) {
    DWORD objIdx = FindPerfIndex(engObj);
    DWORD ctrIdx = FindPerfIndex(engCtr);
    if (objIdx == 0 || ctrIdx == 0) return FALSE;

    DWORD sz1 = locObjSz, sz2 = locCtrSz;
    if (PdhLookupPerfNameByIndexW(NULL, objIdx, locObj, &sz1) != ERROR_SUCCESS) return FALSE;
    if (PdhLookupPerfNameByIndexW(NULL, ctrIdx, locCtr, &sz2) != ERROR_SUCCESS) return FALSE;
    return TRUE;
}

/* ======================== GPU 监控初始化 ======================== */
static void InitGpuMonitoring(DWORD pid) {
    g_gpuState.pid = pid;
    g_gpuState.initialized = TRUE;
    g_gpuState.available = FALSE;

    wchar_t locObj[256], locCtr[256];
    if (!GetLocalizedNames(L"GPU Engine", L"Utilization Percentage",
                           locObj, 256, locCtr, 256)) {
        /* 回退: 尝试直接用英文名 */
        wcscpy(locObj, L"GPU Engine");
        wcscpy(locCtr, L"Utilization Percentage");
    }

    /* 构建通配符路径: \GPU Engine(*pid_<pid>_*)\Utilization Percentage */
    wchar_t pattern[512];
    swprintf(pattern, 512, L"\\%s(*pid_%lu_*)\\%s", locObj, pid, locCtr);

    /* 展开通配符，找到所有匹配的实例 */
    DWORD dwSize = 0;
    PDH_STATUS st = PdhExpandWildCardPathW(NULL, pattern, NULL, &dwSize, 0);
    if (st != PDH_MORE_DATA || dwSize == 0) return;

    wchar_t *paths = (wchar_t *)malloc(dwSize * sizeof(wchar_t));
    if (!paths) return;

    st = PdhExpandWildCardPathW(NULL, pattern, paths, &dwSize, 0);
    if (st != ERROR_SUCCESS) { free(paths); return; }

    /* 统计匹配数量 */
    int count = 0;
    wchar_t *p = paths;
    while (*p) { count++; p += wcslen(p) + 1; }

    if (count == 0) { free(paths); return; }

    /* 创建 PDH 查询 */
    if (PdhOpenQueryW(NULL, 0, &g_gpuState.hQuery) != ERROR_SUCCESS) {
        free(paths);
        return;
    }

    g_gpuState.counters = (HCOUNTER *)malloc(count * sizeof(HCOUNTER));
    if (!g_gpuState.counters) {
        PdhCloseQuery(g_gpuState.hQuery);
        g_gpuState.hQuery = NULL;
        free(paths);
        return;
    }

    /* 添加所有匹配的计数器 */
    p = paths;
    int idx = 0;
    while (*p && idx < count) {
        if (PdhAddCounterW(g_gpuState.hQuery, p, 0, &g_gpuState.counters[idx]) == ERROR_SUCCESS)
            idx++;
        p += wcslen(p) + 1;
    }
    g_gpuState.counterCount = idx;
    g_gpuState.available = (idx > 0);

    free(paths);
}

/* ======================== VRAM 监控初始化 ======================== */
static void InitVramMonitoring(void) {
    g_vramState.initialized = TRUE;
    g_vramState.available = FALSE;

    wchar_t locObj[256], locCtr[256];
    if (!GetLocalizedNames(L"GPU Adapter Memory", L"Dedicated Usage",
                           locObj, 256, locCtr, 256)) {
        wcscpy(locObj, L"GPU Adapter Memory");
        wcscpy(locCtr, L"Dedicated Usage");
    }

    wchar_t counterPath[512];
    swprintf(counterPath, 512, L"\\%s(*)\\%s", locObj, locCtr);

    if (PdhOpenQueryW(NULL, 0, &g_vramState.hQuery) != ERROR_SUCCESS) return;

    if (PdhAddCounterW(g_vramState.hQuery, counterPath, 0, &g_vramState.hCounter) != ERROR_SUCCESS) {
        PdhCloseQuery(g_vramState.hQuery);
        g_vramState.hQuery = NULL;
        return;
    }

    g_vramState.available = TRUE;
}

/* ======================== 导出函数 ======================== */
OBSERVER_API void GetProcessStatsEx(DWORD pid, double *cpu, double *gpu,
                                     double *memMB, double *vramMB) {
    *cpu = 0;
    *gpu = 0;
    *memMB = 0;
    *vramMB = 0;

    /* === CPU + Memory === */
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (hProc) {
        /* Memory: Working Set */
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc)))
            *memMB = pmc.WorkingSetSize / (1024.0 * 1024.0);

        /* CPU: 增量计算 */
        FILETIME ftCreation, ftExit, ftKernel, ftUser;
        if (GetProcessTimes(hProc, &ftCreation, &ftExit, &ftKernel, &ftUser)) {
            ULONGLONG kernel = ((ULONGLONG)ftKernel.dwHighDateTime << 32) | ftKernel.dwLowDateTime;
            ULONGLONG user   = ((ULONGLONG)ftUser.dwHighDateTime << 32) | ftUser.dwLowDateTime;
            ULONGLONG now    = GetTickCount64();

            if (g_cpuState.initialized && now > g_cpuState.lastTime) {
                ULONGLONG timeDiff = now - g_cpuState.lastTime;
                ULONGLONG cpuDiff  = (kernel - g_cpuState.lastKernel) + (user - g_cpuState.lastUser);
                if (timeDiff > 0) {
                    *cpu = (double)cpuDiff / (timeDiff * 10000.0) * 100.0;
                    if (*cpu > 100.0) *cpu = 100.0;
                }
            }

            g_cpuState.lastTime    = now;
            g_cpuState.lastKernel  = kernel;
            g_cpuState.lastUser    = user;
            g_cpuState.initialized = TRUE;
        }

        CloseHandle(hProc);
    }

    /* === GPU === */
    if (!g_gpuState.initialized || g_gpuState.pid != pid)
        InitGpuMonitoring(pid);

    if (g_gpuState.available && g_gpuState.hQuery) {
        PdhCollectQueryData(g_gpuState.hQuery);

        double totalGpu = 0;
        for (int i = 0; i < g_gpuState.counterCount; i++) {
            PDH_FMT_COUNTERVALUE val;
            if (PdhGetFormattedCounterValue(g_gpuState.counters[i],
                    PDH_FMT_DOUBLE, NULL, &val) == ERROR_SUCCESS) {
                totalGpu += val.doubleValue;
            }
        }
        if (totalGpu > 100.0) totalGpu = 100.0;
        *gpu = totalGpu;
    }

    /* === VRAM (系统总量) === */
    if (!g_vramState.initialized)
        InitVramMonitoring();

    if (g_vramState.available && g_vramState.hQuery) {
        PdhCollectQueryData(g_vramState.hQuery);
        PDH_FMT_COUNTERVALUE val;
        if (PdhGetFormattedCounterValue(g_vramState.hCounter,
                PDH_FMT_DOUBLE, NULL, &val) == ERROR_SUCCESS) {
            *vramMB = val.doubleValue / (1024.0 * 1024.0);
        }
    }
}

/* ======================== DLL 入口 ======================== */
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)hinstDLL;
    (void)lpvReserved;
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            break;
        case DLL_PROCESS_DETACH:
            /* 清理 PDH 资源 */
            if (g_gpuState.counters) free(g_gpuState.counters);
            if (g_gpuState.hQuery)   PdhCloseQuery(g_gpuState.hQuery);
            if (g_vramState.hQuery)  PdhCloseQuery(g_vramState.hQuery);
            break;
    }
    return TRUE;
}
