/**
 * admin.c — 管理员权限重启 guardiand.exe
 *
 * 导出：BOOL RestartDaemon(HWND hwndOwner)
 *   1. 枚举进程，终止所有 guardiand.exe 实例
 *   2. 检查当前是否已具有管理员权限
 *      - 若已是管理员：直接以提升令牌启动 guardiand.exe --hidden
 *      - 若非管理员：通过 ShellExecuteExW runas 提权后启动
 *        guardian.exe --hidden-launch（该模式在 main.c 中已实现）
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

static void GetInstallDir(wchar_t *out, int outLen) {
    wchar_t dllPath[MAX_PATH];
    HMODULE hSelf = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)GetInstallDir, &hSelf);
    GetModuleFileNameW(hSelf, dllPath, MAX_PATH);

    wchar_t *p = wcsrchr(dllPath, L'\\');
    if (p) *p = L'\0';
    p = wcsrchr(dllPath, L'\\');
    if (p) *p = L'\0';

    wcsncpy(out, dllPath, outLen - 1);
    out[outLen - 1] = L'\0';
}

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
    Sleep(300);
}

static BOOL IsAdmin(void) {
    BOOL result = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuth, 2,
                                  SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &result);
        FreeSid(adminGroup);
    }
    return result;
}

/* ---- 导出函数 ---- */

PERM_API BOOL RestartDaemon(HWND hwndOwner) {
    /* 1. 终止旧实例 */
    KillAllGuardiand();

    wchar_t root[MAX_PATH];
    GetInstallDir(root, MAX_PATH);

    wchar_t daemonPath[MAX_PATH];
    swprintf(daemonPath, MAX_PATH, L"%s\\guardiand.exe", root);

    /* 2. 已是管理员 → 直接启动 */
    if (IsAdmin()) {
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

    /* 3. 非管理员 → 用 ShellExecuteExW("runas") 提权启动 guardiand.exe --hidden
     *    UAC 弹窗，用户同意后以管理员权限运行 guardiand.exe */
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize       = sizeof(sei);
    sei.lpVerb       = L"runas";
    sei.lpFile       = daemonPath;
    sei.lpParameters = L"--hidden";
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.nShow        = SW_HIDE;
    sei.hwnd         = hwndOwner;

    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) CloseHandle(sei.hProcess);
        return TRUE;
    }
    return FALSE;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    (void)hInst; (void)reason; (void)reserved;
    return TRUE;
}
