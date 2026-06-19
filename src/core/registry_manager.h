// Registry management DLL interface

#ifndef REGISTRY_MANAGER_H
#define REGISTRY_MANAGER_H

#include <windows.h>

#ifdef BUILD_REGISTRY_MANAGER_DLL
#define REG_API __declspec(dllexport)
#elif defined(USE_REGISTRY_MANAGER_DLL)
#define REG_API __declspec(dllimport)
#else
#define REG_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    wchar_t name[256];
    wchar_t path[512];
    wchar_t type[64];
    wchar_t value[1024];
} RegistryEntry;

REG_API int GetRegistryEntries(HKEY rootKey, const wchar_t *subKey, RegistryEntry **outEntries);
REG_API BOOL DeleteRegistryEntry(HKEY rootKey, const wchar_t *subKey, const wchar_t *valueName);
REG_API void AddToRepeatedDeleteList(const wchar_t *fullPath);
REG_API void RemoveFromRepeatedDeleteList(const wchar_t *fullPath);
REG_API BOOL IsRegistryRepeatedDelete(const wchar_t *fullPath);
REG_API void SaveRepeatedDeleteConfig(void);
REG_API void LoadRepeatedDeleteConfig(void);
REG_API HKEY GetRootKeyFromString(const wchar_t *str);

/* Read all values of a registry key (for Edit dialog).
   outValues must be freed by caller. Returns count, 0 on error. */
REG_API int GetRegistryValues(HKEY rootKey, const wchar_t *subKey,
                              wchar_t ***outNames, DWORD **outTypes,
                              BYTE ***outData, DWORD **outDataSizes);

/* Set a registry value. type is REG_SZ, REG_DWORD, REG_BINARY, etc. */
REG_API BOOL SetRegistryValue(HKEY rootKey, const wchar_t *subKey,
                              const wchar_t *valueName, DWORD type,
                              const BYTE *data, DWORD dataSize);

/* Create a new subkey. */
REG_API BOOL CreateRegistryKey(HKEY rootKey, const wchar_t *subKey);

/* Add to protected list. Snapshot saved to data/registry/&lt;hash&gt;.snapshot */
REG_API BOOL AddToProtectedList(const wchar_t *fullPath);

/* Remove from protected list (also deletes snapshot). */
REG_API BOOL RemoveFromProtectedList(const wchar_t *fullPath);

/* Check if path is in protected list. */
REG_API BOOL IsRegistryProtected(const wchar_t *fullPath);

/* Get all protected paths (caller must free each wchar_t* and the array). */
REG_API int GetProtectedList(wchar_t ***outPaths);

/* Load/save protected list (called by daemon at startup/shutdown). */
REG_API void LoadProtectedConfig(void);
REG_API void SaveProtectedConfig(void);

/* Get all repeated-delete paths. */
REG_API int GetRepeatedDeleteList(wchar_t ***outPaths);

#ifdef __cplusplus
}
#endif

#endif
