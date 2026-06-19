#include "registry_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <shlobj.h>

/* ========== Protected paths list ========== */
static wchar_t **g_protectedPaths = NULL;
static int g_protectedCount = 0;

#define MAX_REPEATED 256
#define MAX_PROTECTED 256
static wchar_t *g_repeatedPaths[MAX_REPEATED] = {0};
static int g_repeatedCount = 0;

/* Protected registry paths - do NOT attempt to delete these */
static const wchar_t *PROTECTED_PATHS[] = {
    L"HKEY_LOCAL_MACHINE\\SAM",
    L"HKEY_LOCAL_MACHINE\\SECURITY",
    L"HKEY_LOCAL_MACHINE\\SYSTEM",
    L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT",
    L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion",
    L"HKEY_CURRENT_USER\\Software\\Microsoft",
    NULL
};

static BOOL IsProtectedPath(const wchar_t *path) {
    for (int i = 0; PROTECTED_PATHS[i] != NULL; i++) {
        if (wcsstr(path, PROTECTED_PATHS[i]) != NULL) {
            return TRUE;
        }
    }
    /* Also check user-added protected list */
    for (int i = 0; i < g_protectedCount; i++) {
        if (wcsicmp(g_protectedPaths[i], path) == 0) return TRUE;
    }
    return FALSE;
}

/* ========== Data directory helpers ========== */
static void GetDataDir(wchar_t *out, int outSize) {
    /* Use %ProgramData%\\ProcessGuardian as base */
    wchar_t progData[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, progData) == S_OK) {
        swprintf(out, outSize, L"%s\\ProcessGuardian", progData);
    } else {
        wcscpy(out, L".\\data");
    }
}

static void EnsureDataDirs(void) {
    wchar_t base[MAX_PATH], sub[MAX_PATH];
    GetDataDir(base, MAX_PATH);
    CreateDirectoryW(base, NULL);
    swprintf(sub, MAX_PATH, L"%s\\registry", base);
    CreateDirectoryW(sub, NULL);
    swprintf(sub, MAX_PATH, L"%s\\partitions", base);
    CreateDirectoryW(sub, NULL);
    swprintf(sub, MAX_PATH, L"%s\\commands", base);
    CreateDirectoryW(sub, NULL);
    swprintf(sub, MAX_PATH, L"%s\\results", base);
    CreateDirectoryW(sub, NULL);
}

static void HashToHex(DWORD hash, wchar_t *out) {
    swprintf(out, 16, L"%08x", hash);
}

static DWORD HashPath(const wchar_t *path) {
    DWORD h = 5381;
    while (*path) {
        h = ((h << 5) + h) + (DWORD)towlower(*path);
        path++;
    }
    return h;
}

static void GetSnapshotPath(const wchar_t *regPath, wchar_t *out, int outSize) {
    wchar_t base[MAX_PATH], hex[16];
    GetDataDir(base, MAX_PATH);
    HashToHex(HashPath(regPath), hex);
    swprintf(out, outSize, L"%s\\registry\\%s.snapshot", base, hex);
}

/* ========== Snapshot save/restore ========== */
/* Snapshot format (binary):
   DWORD  magic = 0x52455350 ('RESP')
   DWORD  version = 1
   WCHAR  regPath[512]   (null-terminated)
   DWORD  subkeyCount
   For each subkey:
     WCHAR name[256]
     DWORD valueCount
     For each value:
       WCHAR name[256]  (or empty for default)
       DWORD type
       DWORD dataSize
       BYTE data[max 8192]
*/
static BOOL SaveRegistrySnapshot(const wchar_t *regPath) {
    HKEY root = GetRootKeyFromString(regPath);
    const wchar_t *subKey = wcschr(regPath, L'\\');
    if (!subKey) return FALSE;
    subKey++;

    HKEY hKey;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return FALSE;

    wchar_t path[MAX_PATH];
    GetSnapshotPath(regPath, path, MAX_PATH);

    FILE *f = _wfopen(path, L"wb");
    if (!f) { RegCloseKey(hKey); return FALSE; }

    DWORD magic = 0x52455350, ver = 1;
    fwrite(&magic, 4, 1, f);
    fwrite(&ver, 4, 1, f);

    /* write regPath as wide string (with null terminator) */
    BYTE pathBuf[1024] = {0};
    int pathLen = (wcslen(regPath) + 1) * 2;
    memcpy(pathBuf, regPath, pathLen);
    fwrite(pathBuf, 1, 1024, f);

    /* count subkeys */
    DWORD numSubKeys = 0;
    RegQueryInfoKeyW(hKey, NULL, NULL, NULL, &numSubKeys, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    fwrite(&numSubKeys, 4, 1, f);

    /* recursively write subkeys */
    for (DWORD i = 0; i < numSubKeys; i++) {
        wchar_t subName[256] = {0};
        DWORD subNameLen = 255;
        if (RegEnumKeyExW(hKey, i, subName, &subNameLen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            continue;
        BYTE nameBuf[512] = {0};
        memcpy(nameBuf, subName, (wcslen(subName) + 1) * 2);
        fwrite(nameBuf, 1, 512, f);

        /* open this subkey and recurse */
        HKEY hSub;
        wchar_t fullSubPath[1024];
        swprintf(fullSubPath, 1024, L"%s\\%s", subKey, subName);
        if (RegOpenKeyExW(root, fullSubPath, 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
            DWORD subSubCount = 0;
            RegQueryInfoKeyW(hSub, NULL, NULL, NULL, &subSubCount, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
            fwrite(&subSubCount, 4, 1, f);

            for (DWORD j = 0; j < subSubCount; j++) {
                wchar_t ssName[256] = {0};
                DWORD ssNameLen = 255;
                if (RegEnumKeyExW(hSub, j, ssName, &ssNameLen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                    continue;
                BYTE ssBuf[512] = {0};
                memcpy(ssBuf, ssName, (wcslen(ssName) + 1) * 2);
                fwrite(ssBuf, 1, 512, f);

                /* recurse one more level (2 levels deep max for snapshot) */
                wchar_t fullSSSPath[1024];
                swprintf(fullSSSPath, 1024, L"%s\\%s\\%s", subKey, subName, ssName);
                HKEY hSS;
                if (RegOpenKeyExW(root, fullSSSPath, 0, KEY_READ, &hSS) == ERROR_SUCCESS) {
                    DWORD ssValues = 0;
                    RegQueryInfoKeyW(hSS, NULL, NULL, NULL, NULL, NULL, NULL, &ssValues, NULL, NULL, NULL, NULL);
                    fwrite(&ssValues, 4, 1, f);

                    for (DWORD k = 0; k < ssValues; k++) {
                        wchar_t valName[256] = {0};
                        DWORD valNameLen = 255, valType = 0;
                        BYTE valData[8192] = {0};
                        DWORD valDataSize = 8192;
                        if (RegEnumValueW(hSS, k, valName, &valNameLen, NULL, &valType, valData, &valDataSize) != ERROR_SUCCESS)
                            continue;
                        BYTE vBuf[512] = {0};
                        memcpy(vBuf, valName, (wcslen(valName) + 1) * 2);
                        fwrite(vBuf, 1, 512, f);
                        fwrite(&valType, 4, 1, f);
                        fwrite(&valDataSize, 4, 1, f);
                        if (valDataSize > 0) fwrite(valData, 1, valDataSize, f);
                    }
                    RegCloseKey(hSS);
                } else {
                    DWORD zero = 0;
                    fwrite(&zero, 4, 1, f);
                }
            }
            RegCloseKey(hSub);
        } else {
            DWORD zero = 0;
            fwrite(&zero, 4, 1, f);
        }
    }
    /* write values of the root key */
    DWORD numValues = 0;
    RegQueryInfoKeyW(hKey, NULL, NULL, NULL, NULL, NULL, NULL, &numValues, NULL, NULL, NULL, NULL);
    fwrite(&numValues, 4, 1, f);
    for (DWORD i = 0; i < numValues; i++) {
        wchar_t valName[256] = {0};
        DWORD valNameLen = 255, valType = 0;
        BYTE valData[8192] = {0};
        DWORD valDataSize = 8192;
        if (RegEnumValueW(hKey, i, valName, &valNameLen, NULL, &valType, valData, &valDataSize) != ERROR_SUCCESS)
            continue;
        BYTE vBuf[512] = {0};
        memcpy(vBuf, valName, (wcslen(valName) + 1) * 2);
        fwrite(vBuf, 1, 512, f);
        fwrite(&valType, 4, 1, f);
        fwrite(&valDataSize, 4, 1, f);
        if (valDataSize > 0) fwrite(valData, 1, valDataSize, f);
    }
    fclose(f);
    RegCloseKey(hKey);
    return TRUE;
}

/* ========== Original GetRegistryEntries (unchanged) ========== */
int REG_API GetRegistryEntries(HKEY rootKey, const wchar_t *subKey, RegistryEntry **outEntries) {
    HKEY hKey;
    LONG result = RegOpenKeyExW(rootKey, subKey, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) return 0;

    DWORD numSubKeys = 0;
    RegQueryInfoKeyW(hKey, NULL, NULL, NULL, &numSubKeys, NULL,
                     NULL, NULL, NULL, NULL, NULL, NULL);

    if (numSubKeys == 0) {
        DWORD numValues = 0;
        RegQueryInfoKeyW(hKey, NULL, NULL, NULL, NULL, NULL, NULL,
                         &numValues, NULL, NULL, NULL, NULL);

        RegistryEntry *entries = (RegistryEntry *)malloc((numValues + 1) * sizeof(RegistryEntry));
        if (!entries) { RegCloseKey(hKey); return 0; }
        memset(entries, 0, (numValues + 1) * sizeof(RegistryEntry));

        int count = 0;
        for (DWORD i = 0; i < numValues; i++) {
            wchar_t name[256] = {0};
            DWORD nameLen = 255;
            DWORD type = 0;
            BYTE data[1024] = {0};
            DWORD dataLen = 1024;

            result = RegEnumValueW(hKey, i, name, &nameLen, NULL, &type, data, &dataLen);
            if (result == ERROR_SUCCESS) {
                RegistryEntry *entry = &entries[count];
                wcsncpy(entry->name, name, 255);

                wchar_t rootStr[64] = L"";
                if (rootKey == HKEY_LOCAL_MACHINE) wcscpy(rootStr, L"HKEY_LOCAL_MACHINE");
                else if (rootKey == HKEY_CURRENT_USER) wcscpy(rootStr, L"HKEY_CURRENT_USER");
                else if (rootKey == HKEY_CLASSES_ROOT) wcscpy(rootStr, L"HKEY_CLASSES_ROOT");
                else wcscpy(rootStr, L"HKEY_UNKNOWN");

                wcscpy(entry->path, rootStr);
                wcscat(entry->path, L"\\");
                wcscat(entry->path, subKey);
                if (wcslen(name) > 0) {
                    wcscat(entry->path, L"\\");
                    wcscat(entry->path, name);
                }

                switch (type) {
                    case REG_SZ: wcscpy(entry->type, L"REG_SZ"); break;
                    case REG_DWORD: wcscpy(entry->type, L"REG_DWORD"); break;
                    case REG_BINARY: wcscpy(entry->type, L"REG_BINARY"); break;
                    case REG_MULTI_SZ: wcscpy(entry->type, L"REG_MULTI_SZ"); break;
                    case REG_EXPAND_SZ: wcscpy(entry->type, L"REG_EXPAND_SZ"); break;
                    default: swprintf(entry->type, 63, L"0x%x", type); break;
                }

                if (type == REG_SZ && dataLen > 0) {
                    wcsncpy(entry->value, (wchar_t *)data, 1023);
                } else if (type == REG_DWORD && dataLen >= 4) {
                    swprintf(entry->value, 1023, L"%lu", *(DWORD *)data);
                } else if (type == REG_EXPAND_SZ && dataLen > 0) {
                    wcsncpy(entry->value, (wchar_t *)data, 1023);
                } else {
                    entry->value[0] = L'\0';
                }

                count++;
            }
        }

        RegistryEntry *entry = &entries[count];
        wcscpy(entry->name, L"..");
        wcscpy(entry->path, L"(parent)");
        wcscpy(entry->type, L"");
        wcscpy(entry->value, L"");
        count++;

        RegCloseKey(hKey);
        *outEntries = entries;
        return count;
    }

    RegistryEntry *entries = (RegistryEntry *)malloc((numSubKeys + 1) * sizeof(RegistryEntry));
    if (!entries) { RegCloseKey(hKey); return 0; }
    memset(entries, 0, (numSubKeys + 1) * sizeof(RegistryEntry));

    int count = 0;
    for (DWORD i = 0; i < numSubKeys; i++) {
        wchar_t name[256] = {0};
        DWORD nameLen = 255;

        result = RegEnumKeyExW(hKey, i, name, &nameLen, NULL, NULL, NULL, NULL);
        if (result == ERROR_SUCCESS) {
            RegistryEntry *entry = &entries[count];
            wcsncpy(entry->name, name, 255);

            wchar_t rootStr[64] = L"";
            if (rootKey == HKEY_LOCAL_MACHINE) wcscpy(rootStr, L"HKEY_LOCAL_MACHINE");
            else if (rootKey == HKEY_CURRENT_USER) wcscpy(rootStr, L"HKEY_CURRENT_USER");
            else if (rootKey == HKEY_CLASSES_ROOT) wcscpy(rootStr, L"HKEY_CLASSES_ROOT");
            else wcscpy(rootStr, L"HKEY_UNKNOWN");

            wcscpy(entry->path, rootStr);
            wcscat(entry->path, L"\\");
            wcscat(entry->path, subKey);
            wcscat(entry->path, L"\\");
            wcscat(entry->path, name);

            wcscpy(entry->type, L"KEY");
            entry->value[0] = L'\0';

            count++;
        }
    }

    RegistryEntry *entry = &entries[count];
    wcscpy(entry->name, L"..");
    wcscpy(entry->path, L"(parent)");
    wcscpy(entry->type, L"");
    wcscpy(entry->value, L"");
    count++;

    RegCloseKey(hKey);
    *outEntries = entries;
    return count;
}

BOOL REG_API DeleteRegistryEntry(HKEY rootKey, const wchar_t *subKey, const wchar_t *valueName) {
    wchar_t fullPath[MAX_PATH] = {0};
    wchar_t rootStr[64] = L"";
    if (rootKey == HKEY_LOCAL_MACHINE) wcscpy(rootStr, L"HKEY_LOCAL_MACHINE");
    else if (rootKey == HKEY_CURRENT_USER) wcscpy(rootStr, L"HKEY_CURRENT_USER");
    else if (rootKey == HKEY_CLASSES_ROOT) wcscpy(rootStr, L"HKEY_CLASSES_ROOT");
    else if (rootKey == HKEY_USERS) wcscpy(rootStr, L"HKEY_USERS");
    else if (rootKey == HKEY_CURRENT_CONFIG) wcscpy(rootStr, L"HKEY_CURRENT_CONFIG");

    wcscpy(fullPath, rootStr);
    wcscat(fullPath, L"\\");
    wcscat(fullPath, subKey);

    if (IsProtectedPath(fullPath)) return FALSE;

    if (valueName && wcslen(valueName) > 0) {
        HKEY hKey;
        LONG result = RegOpenKeyExW(rootKey, subKey, 0, KEY_SET_VALUE, &hKey);
        if (result != ERROR_SUCCESS) return FALSE;
        result = RegDeleteValueW(hKey, valueName);
        RegCloseKey(hKey);
        return result == ERROR_SUCCESS;
    } else {
        wchar_t parentPath[MAX_PATH] = {0};
        wchar_t keyName[MAX_PATH] = {0};

        wcsncpy(parentPath, subKey, MAX_PATH - 1);
        wchar_t *lastSlash = wcsrchr(parentPath, L'\\');
        if (lastSlash) {
            wcsncpy(keyName, lastSlash + 1, MAX_PATH - 1);
            *lastSlash = L'\0';
        } else {
            wcscpy(keyName, subKey);
            parentPath[0] = L'\0';
        }

        HKEY hParentKey;
        LONG result;
        if (wcslen(parentPath) > 0) {
            result = RegOpenKeyExW(rootKey, parentPath, 0, KEY_ENUMERATE_SUB_KEYS | DELETE, &hParentKey);
        } else {
            result = RegOpenKeyExW(rootKey, NULL, 0, DELETE, &hParentKey);
        }
        if (result != ERROR_SUCCESS) return FALSE;

        result = RegDeleteKeyW(hParentKey, keyName);
        if (result != ERROR_SUCCESS) {
            result = RegDeleteTreeW(hParentKey, keyName);
        }
        RegCloseKey(hParentKey);
        return result == ERROR_SUCCESS;
    }
}

/* ========== GetRegistryValues (for Edit dialog) ========== */
int REG_API GetRegistryValues(HKEY rootKey, const wchar_t *subKey,
                              wchar_t ***outNames, DWORD **outTypes,
                              BYTE ***outData, DWORD **outDataSizes) {
    HKEY hKey;
    if (RegOpenKeyExW(rootKey, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return 0;

    DWORD numValues = 0;
    RegQueryInfoKeyW(hKey, NULL, NULL, NULL, NULL, NULL, NULL, &numValues, NULL, NULL, NULL, NULL);
    if (numValues == 0) { RegCloseKey(hKey); return 0; }

    wchar_t **names = (wchar_t**)calloc(numValues, sizeof(wchar_t*));
    DWORD *types = (DWORD*)calloc(numValues, sizeof(DWORD));
    BYTE **datas = (BYTE**)calloc(numValues, sizeof(BYTE*));
    DWORD *sizes = (DWORD*)calloc(numValues, sizeof(DWORD));
    if (!names || !types || !datas || !sizes) {
        free(names); free(types); free(datas); free(sizes);
        RegCloseKey(hKey); return 0;
    }

    int count = 0;
    for (DWORD i = 0; i < numValues; i++) {
        wchar_t name[256] = {0};
        DWORD nameLen = 255, type = 0;
        BYTE *data = (BYTE*)malloc(8192);
        if (!data) continue;
        DWORD dataSize = 8192;

        LONG res = RegEnumValueW(hKey, i, name, &nameLen, NULL, &type, data, &dataSize);
        if (res == ERROR_SUCCESS) {
            names[count] = wcsdup(name);
            types[count] = type;
            sizes[count] = dataSize;
            datas[count] = data;
            count++;
        } else {
            free(data);
        }
    }
    RegCloseKey(hKey);
    *outNames = names;
    *outTypes = types;
    *outData = datas;
    *outDataSizes = sizes;
    return count;
}

BOOL REG_API SetRegistryValue(HKEY rootKey, const wchar_t *subKey,
                              const wchar_t *valueName, DWORD type,
                              const BYTE *data, DWORD dataSize) {
    HKEY hKey;
    LONG res = RegOpenKeyExW(rootKey, subKey, 0, KEY_SET_VALUE, &hKey);
    if (res != ERROR_SUCCESS) return FALSE;
    res = RegSetValueExW(hKey, valueName, 0, type, data, dataSize);
    RegCloseKey(hKey);
    return res == ERROR_SUCCESS;
}

BOOL REG_API CreateRegistryKey(HKEY rootKey, const wchar_t *subKey) {
    HKEY hKey;
    DWORD disposition;
    LONG res = RegCreateKeyExW(rootKey, subKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, &disposition);
    if (res != ERROR_SUCCESS) return FALSE;
    RegCloseKey(hKey);
    return TRUE;
}

/* ========== Repeated delete list ========== */
void REG_API AddToRepeatedDeleteList(const wchar_t *fullPath) {
    if (g_repeatedCount >= MAX_REPEATED) return;
    if (IsRegistryRepeatedDelete(fullPath)) return;
    g_repeatedPaths[g_repeatedCount++] = wcsdup(fullPath);
    SaveRepeatedDeleteConfig();
}

void REG_API RemoveFromRepeatedDeleteList(const wchar_t *fullPath) {
    for (int i = 0; i < g_repeatedCount; i++) {
        if (wcsicmp(g_repeatedPaths[i], fullPath) == 0) {
            free(g_repeatedPaths[i]);
            for (int j = i; j < g_repeatedCount - 1; j++)
                g_repeatedPaths[j] = g_repeatedPaths[j+1];
            g_repeatedCount--;
            SaveRepeatedDeleteConfig();
            return;
        }
    }
}

BOOL REG_API IsRegistryRepeatedDelete(const wchar_t *fullPath) {
    for (int i = 0; i < g_repeatedCount; i++) {
        if (wcsicmp(g_repeatedPaths[i], fullPath) == 0) return TRUE;
    }
    return FALSE;
}

int REG_API GetRepeatedDeleteList(wchar_t ***outPaths) {
    wchar_t **arr = (wchar_t**)calloc(g_repeatedCount, sizeof(wchar_t*));
    if (!arr) return 0;
    for (int i = 0; i < g_repeatedCount; i++) arr[i] = wcsdup(g_repeatedPaths[i]);
    *outPaths = arr;
    return g_repeatedCount;
}

static void GetRepeatedConfigPath(wchar_t *out, int outSize) {
    wchar_t base[MAX_PATH];
    GetDataDir(base, MAX_PATH);
    swprintf(out, outSize, L"%s\\repeated_reg.txt", base);
}

void REG_API SaveRepeatedDeleteConfig(void) {
    EnsureDataDirs();
    wchar_t path[MAX_PATH];
    GetRepeatedConfigPath(path, MAX_PATH);
    FILE *f = _wfopen(path, L"wt, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"# ProcessGuardian repeated-delete registry list\n");
    for (int i = 0; i < g_repeatedCount; i++)
        fwprintf(f, L"%s\n", g_repeatedPaths[i]);
    fclose(f);
}

void REG_API LoadRepeatedDeleteConfig(void) {
    wchar_t path[MAX_PATH];
    GetRepeatedConfigPath(path, MAX_PATH);
    FILE *f = _wfopen(path, L"rt, ccs=UTF-8");
    if (!f) return;
    wchar_t line[1024];
    while (fgetws(line, 1024, f)) {
        /* strip newline */
        wchar_t *nl = wcschr(line, L'\n'); if (nl) *nl = 0;
        wchar_t *cr = wcschr(line, L'\r'); if (cr) *cr = 0;
        if (line[0] == L'#' || line[0] == 0) continue;
        if (g_repeatedCount >= MAX_REPEATED) break;
        g_repeatedPaths[g_repeatedCount++] = wcsdup(line);
    }
    fclose(f);
}

/* ========== Protected list ========== */
static void GetProtectedConfigPath(wchar_t *out, int outSize) {
    wchar_t base[MAX_PATH];
    GetDataDir(base, MAX_PATH);
    swprintf(out, outSize, L"%s\\protected_reg.txt", base);
}

void REG_API SaveProtectedConfig(void) {
    EnsureDataDirs();
    wchar_t path[MAX_PATH];
    GetProtectedConfigPath(path, MAX_PATH);
    FILE *f = _wfopen(path, L"wt, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"# ProcessGuardian protected registry list\n");
    for (int i = 0; i < g_protectedCount; i++)
        fwprintf(f, L"%s\n", g_protectedPaths[i]);
    fclose(f);
}

void REG_API LoadProtectedConfig(void) {
    wchar_t path[MAX_PATH];
    GetProtectedConfigPath(path, MAX_PATH);
    FILE *f = _wfopen(path, L"rt, ccs=UTF-8");
    if (!f) return;
    wchar_t line[1024];
    while (fgetws(line, 1024, f)) {
        wchar_t *nl = wcschr(line, L'\n'); if (nl) *nl = 0;
        wchar_t *cr = wcschr(line, L'\r'); if (cr) *cr = 0;
        if (line[0] == L'#' || line[0] == 0) continue;
        if (g_protectedCount >= MAX_PROTECTED) break;
        g_protectedPaths = (wchar_t**)realloc(g_protectedPaths, (g_protectedCount + 1) * sizeof(wchar_t*));
        g_protectedPaths[g_protectedCount++] = wcsdup(line);
    }
    fclose(f);
}

BOOL REG_API AddToProtectedList(const wchar_t *fullPath) {
    if (IsRegistryProtected(fullPath)) return TRUE;
    if (!SaveRegistrySnapshot(fullPath)) return FALSE;
    g_protectedPaths = (wchar_t**)realloc(g_protectedPaths, (g_protectedCount + 1) * sizeof(wchar_t*));
    g_protectedPaths[g_protectedCount++] = wcsdup(fullPath);
    SaveProtectedConfig();
    return TRUE;
}

BOOL REG_API RemoveFromProtectedList(const wchar_t *fullPath) {
    for (int i = 0; i < g_protectedCount; i++) {
        if (wcsicmp(g_protectedPaths[i], fullPath) == 0) {
            free(g_protectedPaths[i]);
            for (int j = i; j < g_protectedCount - 1; j++)
                g_protectedPaths[j] = g_protectedPaths[j+1];
            g_protectedCount--;
            wchar_t path[MAX_PATH];
            GetSnapshotPath(fullPath, path, MAX_PATH);
            DeleteFileW(path);
            SaveProtectedConfig();
            return TRUE;
        }
    }
    return FALSE;
}

BOOL REG_API IsRegistryProtected(const wchar_t *fullPath) {
    return IsProtectedPath(fullPath);
}

int REG_API GetProtectedList(wchar_t ***outPaths) {
    wchar_t **arr = (wchar_t**)calloc(g_protectedCount, sizeof(wchar_t*));
    if (!arr) return 0;
    for (int i = 0; i < g_protectedCount; i++) arr[i] = wcsdup(g_protectedPaths[i]);
    *outPaths = arr;
    return g_protectedCount;
}

HKEY REG_API GetRootKeyFromString(const wchar_t *str) {
    if (wcsncmp(str, L"HKEY_LOCAL_MACHINE", 18) == 0) return HKEY_LOCAL_MACHINE;
    if (wcsncmp(str, L"HKEY_CURRENT_USER", 17) == 0) return HKEY_CURRENT_USER;
    if (wcsncmp(str, L"HKEY_CLASSES_ROOT", 17) == 0) return HKEY_CLASSES_ROOT;
    if (wcsncmp(str, L"HKEY_USERS", 10) == 0) return HKEY_USERS;
    if (wcsncmp(str, L"HKEY_CURRENT_CONFIG", 19) == 0) return HKEY_CURRENT_CONFIG;
    return HKEY_LOCAL_MACHINE;
}
