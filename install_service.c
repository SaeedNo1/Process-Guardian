#define UNICODE
#define _UNICODE

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>

// Self-elevate to admin if not already admin
static BOOL IsRunAsAdmin(void) {
    BOOL fIsRunAsAdmin = FALSE;
    PSID pAdministratorsGroup = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                  &pAdministratorsGroup)) {
        CheckTokenMembership(NULL, pAdministratorsGroup, &fIsRunAsAdmin);
        FreeSid(pAdministratorsGroup);
    }
    return fIsRunAsAdmin;
}

static void SelfElevate(int argc, wchar_t *argv[]) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    wchar_t params[1024] = {0};
    for (int i = 1; i < argc; i++) {
        if (i > 1) wcscat(params, L" ");
        wcscat(params, L"\"");
        wcscat(params, argv[i]);
        wcscat(params, L"\"");
    }
    
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = params[0] ? params : NULL;
    sei.nShow = SW_HIDE;
    if (ShellExecuteExW(&sei)) {
        // Elevated child started, exit this one
        ExitProcess(0);
    } else {
        wprintf(L"ERROR: UAC elevation failed or cancelled. Error=%lu\n", GetLastError());
        ExitProcess(1);
    }
}

static int DoInstall(void) {
    // service_wrapper.exe path
    wchar_t wrapperPath[MAX_PATH];
    GetModuleFileNameW(NULL, wrapperPath, MAX_PATH);
    wchar_t *p = wcsrchr(wrapperPath, L'\\');
    if (p) *p = L'\0';
    // Go up from \build\ if necessary
    wchar_t *buildPos = wcsstr(wrapperPath, L"\\build");
    if (buildPos) *buildPos = L'\0';
    wcscat(wrapperPath, L"\\service_wrapper.exe");
    
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) {
        wprintf(L"ERROR: Cannot open SCM. Error=%lu\n", GetLastError());
        return 1;
    }
    
    // Remove old service if exists
    SC_HANDLE hOld = OpenServiceW(hSCM, L"GuardianDaemon", DELETE | SERVICE_STOP);
    if (hOld) {
        SERVICE_STATUS st;
        ControlService(hOld, SERVICE_CONTROL_STOP, &st);
        Sleep(500);
        DeleteService(hOld);
        CloseServiceHandle(hOld);
    }
    
    // Create new service
    SC_HANDLE hSvc = CreateServiceW(
        hSCM, L"GuardianDaemon", L"Guardian Daemon",
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        wrapperPath, NULL, NULL, NULL, NULL, NULL);
    
    if (!hSvc) {
        DWORD err = GetLastError();
        wprintf(L"ERROR: CreateService failed. Error=%lu\n", err);
        CloseServiceHandle(hSCM);
        return 1;
    }
    
    // Set service description
    SERVICE_DESCRIPTIONW desc = {0};
    desc.lpDescription = L"Process Guardian background daemon. Protects/repeatedly terminates configured processes.";
    ChangeServiceConfig2W(hSvc, SERVICE_CONFIG_DESCRIPTION, &desc);
    
    CloseServiceHandle(hSvc);
    
    // Start service
    hSvc = OpenServiceW(hSCM, L"GuardianDaemon", SERVICE_ALL_ACCESS);
    if (hSvc) {
        if (StartServiceW(hSvc, 0, NULL)) {
            wprintf(L"OK: Service installed and started.\n");
        } else {
            wprintf(L"WARN: Service installed but failed to start. Error=%lu\n", GetLastError());
        }
        CloseServiceHandle(hSvc);
    }
    
    CloseServiceHandle(hSCM);
    return 0;
}

static int DoRemove(void) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) {
        wprintf(L"ERROR: Cannot open SCM. Error=%lu\n", GetLastError());
        return 1;
    }
    SC_HANDLE hSvc = OpenServiceW(hSCM, L"GuardianDaemon", SERVICE_ALL_ACCESS);
    if (hSvc) {
        SERVICE_STATUS st;
        ControlService(hSvc, SERVICE_CONTROL_STOP, &st);
        Sleep(500);
        if (DeleteService(hSvc)) {
            wprintf(L"OK: Service removed.\n");
        } else {
            wprintf(L"WARN: DeleteService failed. Error=%lu\n", GetLastError());
        }
        CloseServiceHandle(hSvc);
    } else {
        wprintf(L"Service not found.\n");
    }
    CloseServiceHandle(hSCM);
    return 0;
}

static int DoStart(void) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) return 1;
    SC_HANDLE hSvc = OpenServiceW(hSCM, L"GuardianDaemon", SERVICE_ALL_ACCESS);
    if (!hSvc) { CloseServiceHandle(hSCM); return 1; }
    int r = StartServiceW(hSvc, 0, NULL) ? 0 : 1;
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return r;
}

static int DoStop(void) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) return 1;
    SC_HANDLE hSvc = OpenServiceW(hSCM, L"GuardianDaemon", SERVICE_ALL_ACCESS);
    if (!hSvc) { CloseServiceHandle(hSCM); return 1; }
    SERVICE_STATUS st;
    int r = ControlService(hSvc, SERVICE_CONTROL_STOP, &st) ? 0 : 1;
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return r;
}

int wmain(int argc, wchar_t *argv[]) {
    if (argc < 2) {
        wprintf(L"Usage:\n");
        wprintf(L"  install_service.exe install  - install and start service as SYSTEM\n");
        wprintf(L"  install_service.exe remove   - stop and remove service\n");
        wprintf(L"  install_service.exe start    - start service\n");
        wprintf(L"  install_service.exe stop     - stop service\n");
        return 1;
    }
    
    // Self-elevate to admin if needed (only for install/remove, not start/stop)
    if (_wcsicmp(argv[1], L"install") == 0 || _wcsicmp(argv[1], L"remove") == 0) {
        if (!IsRunAsAdmin()) {
            SelfElevate(argc, argv);
        }
    }
    
    if (_wcsicmp(argv[1], L"install") == 0) return DoInstall();
    if (_wcsicmp(argv[1], L"remove") == 0) return DoRemove();
    if (_wcsicmp(argv[1], L"start") == 0) return DoStart();
    if (_wcsicmp(argv[1], L"stop") == 0) return DoStop();
    
    wprintf(L"Unknown command: %s\n", argv[1]);
    return 1;
}
