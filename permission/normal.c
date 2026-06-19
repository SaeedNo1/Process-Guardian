/**
 * normal.c — 普通权限重启 guardiand.exe
 *
 * 导出：BOOL RestartDaemon(HWND hwndOwner)
 *   1. 枚举进程，终止所有 guardiand.exe 实例
 *   2. 以当前用户权限（无提权）重新启动 guardiand.exe --hidden
 */

#ifndef UNICODE
#  define UNICODE
#endif
#ifndef _UNICODE
#  define _UNICODE
#endif

#include <windows.h>
#include <tlhelp32.h>
#include <wchar.h>
#include <stdio.h>
#include "permission_iface.h"

/* ---- 内部工具 ---- */

/* 获取本 DLL 所在目录（即 permission\ 的父目录，也就是 guardian.exe 所在目录） */
static void GetInstallDir(wchar_t *out, int outLen) {
    /* DLL 位于 <root>\permission\normal.dll，所以上两级才是 root */
    wchar_t dllPath[MAX_PATH];
    HMODULE hSelf = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)GetInstallDir, &hSelf);
    GetModuleFileNameW(hSelf, dllPath, MAX_PATH);

    /* 去掉文件名 → …\permission */
    wchar_t *p = wcsrchr(dllPath, L'\\');
    if (p) *p = L'\0';
    /* 再去掉 \permission → 根目录 */
    p = wcsrchr(dllPath, L'\\');
    if (p) *p = L'\0';

    wcsncpy(out, dllPath, outLen - 1);
    out[outLen - 1] = L'\0';
}

/* 终止所有名为 guardiand.exe 的进程 */
static void KillAllGuardiand(void) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe = {0};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"guardiand.exe") == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    TerminateProcess(hProc, 1);
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);

    /* 稍等进程真正退出 */
    Sleep(300);
}

/* ---- 导出函数 ---- */

PERM_API BOOL RestartDaemon(HWND hwndOwner) {
    (void)hwndOwner;

    /* 1. 终止旧实例 */
    KillAllGuardiand();

    /* 2. 构造 guardiand.exe 路径 */
    wchar_t root[MAX_PATH];
    GetInstallDir(root, MAX_PATH);

    wchar_t daemonPath[MAX_PATH];
    swprintf(daemonPath, MAX_PATH, L"%s\\guardiand.exe", root);

    /* 3. 以普通（当前用户）权限启动 */
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {0};

    DWORD flags = CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    BOOL ok = CreateProcessW(daemonPath, L" --hidden",
                             NULL, NULL, FALSE, flags,
                             NULL, NULL, &si, &pi);
    if (ok) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return ok;
}

/* DLL 入口（可为空，只需导出函数） */
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    (void)hInst; (void)reason; (void)reserved;
    return TRUE;
}
