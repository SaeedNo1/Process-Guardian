/**
 * SYSTEM.c — SYSTEM 权限重启 guardiand.exe（通过 Windows 服务）
 *
 * 导出：BOOL RestartDaemon(HWND hwndOwner)
 *   1. 终止所有 guardiand.exe 实例
 *   2. 通过 ShellExecuteExW("runas") 提权运行 install_service.exe install
 *      install_service.exe 会：
 *        a) 删除旧的 GuardianDaemon 服务（如果存在）
 *        b) 创建新服务（指向 service_wrapper.exe，以 SYSTEM 运行）
 *        c) 启动服务
 *      服务启动后，service_wrapper.exe 以 SYSTEM 权限运行，
 *      它会启动 guardiand.exe --hidden 作为子进程，
 *      guardiand.exe 继承 SYSTEM 权限。
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

/* 获取项目根目录（DLL 位于 <root>\permission\SYSTEM.dll，上两级是 root） */
static void GetInstallDir(wchar_t *out, int outLen) {
    wchar_t dllPath[MAX_PATH];
    HMODULE hSelf = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)GetInstallDir, &hSelf);
    GetModuleFileNameW(hSelf, dllPath, MAX_PATH);

    /* 去掉文件名 -> ...\permission */
    wchar_t *p = wcsrchr(dllPath, L'\\');
    if (p) *p = L'\0';
    /* 再去掉 \permission -> 根目录 */
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
    Sleep(300);
}

/* ---- 导出函数 ---- */

PERM_API BOOL RestartDaemon(HWND hwndOwner) {
    /* 1. 先强制终止可能残留的 guardiand.exe */
    KillAllGuardiand();

    /* 2. 构造 install_service.exe 路径 */
    wchar_t root[MAX_PATH];
    GetInstallDir(root, MAX_PATH);

    wchar_t installPath[MAX_PATH];
    swprintf(installPath, MAX_PATH, L"%s\\install_service.exe", root);

    /* 3. 用 ShellExecuteExW("runas") 提权运行 install_service.exe install
     *    - 如果当前已经是管理员：不会弹 UAC，直接运行
     *    - 如果当前不是管理员：弹 UAC 提示，用户同意后以管理员权限运行
     *
     *    install_service.exe install 会：
     *      a) 停止并删除旧的 GuardianDaemon 服务（如果存在）
     *      b) 创建新服务，指向 service_wrapper.exe，以 LocalSystem 运行
     *      c) 启动服务
     *    服务启动后 service_wrapper.exe 以 SYSTEM 权限运行，
     *    它会启动 guardiand.exe --hidden，guardiand.exe 继承 SYSTEM 权限。
     */
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize       = sizeof(sei);
    sei.lpVerb       = L"runas";
    sei.lpFile       = installPath;
    sei.lpParameters = L"install";
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.nShow        = SW_HIDE;
    sei.hwnd         = hwndOwner;

    if (!ShellExecuteExW(&sei)) {
        return FALSE;
    }

    /* 等待 install_service.exe 完成 */
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 30000);
        CloseHandle(sei.hProcess);
    }

    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    (void)hInst; (void)reason; (void)reserved;
    return TRUE;
}
