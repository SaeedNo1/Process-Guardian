#define UNICODE
#define _UNICODE

#include <windows.h>
#include <stdio.h>
#include <tlhelp32.h>

static SERVICE_STATUS g_serviceStatus;
static SERVICE_STATUS_HANDLE g_serviceHandle;

void WINAPI ServiceCtrl(DWORD ctrlCode) {
    if (ctrlCode == SERVICE_CONTROL_STOP) {
        g_serviceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(g_serviceHandle, &g_serviceStatus);
    }
}

void WINAPI ServiceMain(DWORD argc, LPWSTR *argv) {
    (void)argc;
    (void)argv;
    
    g_serviceStatus.dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    g_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_serviceStatus.dwCurrentState = SERVICE_RUNNING;
    g_serviceStatus.dwWin32ExitCode = 0;
    
    g_serviceHandle = RegisterServiceCtrlHandlerW(L"GuardianDaemon", ServiceCtrl);
    if (g_serviceHandle) {
        SetServiceStatus(g_serviceHandle, &g_serviceStatus);
    }
    
    // Run guardiand.exe
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    
    // Get the actual Guardian folder path
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t *p = wcsrchr(exePath, L'\\');
    if (p) *p = L'\0';
    
    // Go up from build\ if needed
    wchar_t *buildPos = wcsstr(exePath, L"\\build");
    if (buildPos) *buildPos = L'\0';
    
    wcscat(exePath, L"\\guardiand.exe");
    
    // Run guardiand with DETACHED_PROCESS to stay in session
    CreateProcessW(exePath, L" --hidden", NULL, NULL, FALSE, 
                 DETACHED_PROCESS, NULL, NULL, &si, &pi);
    
    if (pi.hProcess) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    
    // Keep service running - restart if guardiand exits
    while (TRUE) {
        Sleep(5000);
        
        // Check if guardiand is still running
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = {0};
            pe.dwSize = sizeof(PROCESSENTRY32W);
            BOOL found = FALSE;
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, L"guardiand.exe") == 0) {
                        found = TRUE;
                        break;
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
            
            // Restart if not found
            if (!found) {
                STARTUPINFOW si = {0};
                PROCESS_INFORMATION pi = {0};
                si.cb = sizeof(si);
                CreateProcessW(exePath, L" --hidden", NULL, NULL, FALSE, 
                     DETACHED_PROCESS, NULL, NULL, &si, &pi);
                if (pi.hProcess) {
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                }
            }
        }
    }
    
    g_serviceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_serviceHandle, &g_serviceStatus);
}

int wmain(void) {
    SERVICE_TABLE_ENTRYW ste[] = {
        { L"GuardianDaemon", ServiceMain },
        { NULL, NULL }
    };
    
    StartServiceCtrlDispatcherW(ste);
    return 0;
}