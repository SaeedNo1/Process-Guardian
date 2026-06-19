// Partition management DLL interface

#ifndef PARTITION_MANAGER_H
#define PARTITION_MANAGER_H

#include <windows.h>

#ifdef BUILD_PARTITION_MANAGER_DLL
#define PART_API __declspec(dllexport)
#elif defined(USE_PARTITION_MANAGER_DLL)
#define PART_API __declspec(dllimport)
#else
#define PART_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PARTITION_TYPE_MBR = 0,
    PARTITION_TYPE_GPT = 1
} PartitionTableType;

typedef struct {
    int diskNumber;
    wchar_t model[256];
    ULONGLONG sizeBytes;
    PartitionTableType tableType;
    int partitionCount;
} DiskInfo;

typedef struct {
    int diskNumber;
    int partitionNumber;
    wchar_t name[256];
    wchar_t location[512];
    wchar_t id[64];
    wchar_t fsType[64];
    wchar_t driveLetter[16];
    ULONGLONG sizeBytes;
    BOOL isBootable;
} PartitionEntry;

PART_API int GetAllDisks(DiskInfo **outDisks);
PART_API int GetPartitionsOnDisk(int diskNumber, PartitionEntry **outPartitions);
PART_API const wchar_t* GetPartitionTableTypeString(PartitionTableType type);
PART_API BOOL DeletePartition(int diskNumber, int partitionNumber);
PART_API void AddToRepeatedDeleteList(int diskNumber, int partitionNumber, const wchar_t *location);
PART_API void RemoveFromRepeatedDeleteList(int diskNumber, int partitionNumber);
PART_API BOOL IsPartitionRepeatedDelete(int diskNumber, int partitionNumber);
PART_API void SaveRepeatedDeleteConfig(void);
PART_API void LoadRepeatedDeleteConfig(void);

#ifdef __cplusplus
}
#endif

#endif
