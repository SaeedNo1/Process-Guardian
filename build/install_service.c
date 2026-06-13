#define UNICODE
#define _UNICODE

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static SERVICE_STATUS g_serviceStatus;
static SERVICE_STATUS_HANDLE g_serviceHandle;
static HANDLE g_stopEvent;

void WINAPI ServiceCtrl(DWORD ctrlCode) {
    if (ctrlCode == SERVICE_CONTROL_STOP) {
        g_serviceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(g_serviceHandle, &g_serviceStatus);
        SetEvent(g_stopEvent);
    }
}

void WINAPI ServiceMain(DWORD argc, LPWSTR *argv) {
    (void)argc;
    (void)argv;
    
    g_serviceStatus.dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    g_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_serviceStatus.dwCurrentState = SERVICE_RUNNING;
    g_serviceStatus.dwWin32ExitCode = 0;
    g_serviceStatus.dwServiceSpecificExitCode = 0;
    
    g_serviceHandle = RegisterServiceCtrlHandlerW(L"GuardianDaemon", ServiceCtrl);
    if (g_serviceHandle) {
        SetServiceStatus(g_serviceHandle, &g_serviceStatus);
    }
    
    // Launch guardiand.exe
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (p) *p = L'\0';
    wcscat(exePath, L"\\guardiand.exe");
    
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    CreateProcessW(exePath, L" --hidden", NULL, NULL, FALSE, 
                 CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi);
    
    if (pi.hProcess) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    
    g_serviceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_serviceHandle, &g_serviceStatus);
}

int InstallService(void) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) return -1;
    
    SC_HANDLE hService = CreateServiceW(
        hSCM, L"GuardianDaemon", L"Guardian Daemon",
        SERVICE_ALL_ACCESS, SERVICE_WIN32_SHARE_PROCESS,
        SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        exePath, NULL, NULL, NULL, NULL, NULL);
    
    if (hService) {
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        return 0;
    }
    
    CloseServiceHandle(hSCM);
    return -1;
}

int RemoveService(void) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) return -1;
    
    SC_HANDLE hService = OpenServiceW(hSCM, L"GuardianDaemon", DELETE);
    if (hService) {
        DeleteService(hService);
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        return 0;
    }
    
    CloseServiceHandle(hSCM);
    return -1;
}

int wmain(int argc, wchar_t *argv[]) {
    if (argc < 2) {
        wprintf(L"Usage: install_service.exe <install|remove|start|stop>\n");
        return 1;
    }
    
    if (wcscmp(argv[1], L"install") == 0) {
        if (InstallService() == 0) {
            wprintf(L"Service installed!\n");
            return 0;
        } else {
            wprintf(L"Service install failed!\n");
            return 1;
        }
    } else if (wcscmp(argv[1], L"remove") == 0) {
        if (RemoveService() == 0) {
            wprintf(L"Service removed!\n");
            return 0;
        } else {
            wprintf(L"Service remove failed!\n");
            return 1;
        }
    }
    
    wprintf(L"Unknown command: %s\n", argv[1]);
    return 1;
}