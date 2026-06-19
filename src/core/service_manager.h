// Service management DLL interface

#ifndef SERVICE_MANAGER_H
#define SERVICE_MANAGER_H

#include <windows.h>

#ifdef BUILD_SERVICE_MANAGER_DLL
#define SVC_API __declspec(dllexport)
#elif defined(USE_SERVICE_MANAGER_DLL)
#define SVC_API __declspec(dllimport)
#else
#define SVC_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    wchar_t name[256];
    wchar_t displayName[256];
    wchar_t path[512];
    DWORD status;
    DWORD startType;
    wchar_t statusStr[64];
    wchar_t startTypeStr[64];
} ServiceEntry;

// Exported functions
SVC_API int GetAllServices(ServiceEntry **outEntries);
SVC_API BOOL StopServiceByName(const wchar_t *serviceName);
SVC_API BOOL StartServiceByName(const wchar_t *serviceName);
SVC_API BOOL DeleteServiceByName(const wchar_t *serviceName);
SVC_API void AddToRepeatedDeleteList(const wchar_t *serviceName);
SVC_API void RemoveFromRepeatedDeleteList(const wchar_t *serviceName);
SVC_API BOOL IsServiceRepeatedDelete(const wchar_t *serviceName);
SVC_API void SaveRepeatedDeleteConfig(void);
SVC_API void LoadRepeatedDeleteConfig(void);

#ifdef __cplusplus
}
#endif

#endif
