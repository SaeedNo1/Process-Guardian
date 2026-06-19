#include "partition_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winioctl.h>

typedef struct {
    int diskNumber;
    wchar_t model[256];
    ULONGLONG sizeBytes;
    PartitionTableType tableType;
    int partitionCount;
} DiskInfoInternal;

static DiskInfoInternal g_disks[16];
static int g_diskCount = 0;

static void DetectPartitionTableType(int diskNumber) {
    wchar_t path[64];
    swprintf(path, 64, L"\\\\.\\PhysicalDrive%d", diskNumber);
    
    HANDLE hDisk = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hDisk == INVALID_HANDLE_VALUE) return;
    
    BYTE buffer[512];
    DWORD bytesRead = 0;
    if (ReadFile(hDisk, buffer, 512, &bytesRead, NULL) && bytesRead == 512) {
        // Check for GPT signature: offset 512-515 should be "EFI PART"
        BYTE gptSig[8] = {0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54};
        if (memcmp(&buffer[512], gptSig, 8) == 0) {
            g_disks[g_diskCount].tableType = PARTITION_TYPE_GPT;
        } else {
            // Check for MBR signature at offset 510-511 (0x55AA)
            if (buffer[510] == 0x55 && buffer[511] == 0xAA) {
                g_disks[g_diskCount].tableType = PARTITION_TYPE_MBR;
            } else {
                g_disks[g_diskCount].tableType = PARTITION_TYPE_MBR; // Default to MBR
            }
        }
    }
    
    CloseHandle(hDisk);
}

PART_API int GetAllDisks(DiskInfo **outDisks) {
    g_diskCount = 0;
    
    for (int i = 0; i < 16; i++) {
        wchar_t path[64];
        swprintf(path, 64, L"\\\\.\\PhysicalDrive%d", i);
        
        HANDLE hDisk = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, 0, NULL);
        if (hDisk == INVALID_HANDLE_VALUE) {
            if (i == 0) continue; // Disk 0 should exist
            break; // No more disks
        }
        
        g_disks[g_diskCount].diskNumber = i;
        
        // Get disk size
        GET_LENGTH_INFORMATION lengthInfo;
        DWORD bytesReturned = 0;
        if (DeviceIoControl(hDisk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                           &lengthInfo, sizeof(lengthInfo), &bytesReturned, NULL)) {
            g_disks[g_diskCount].sizeBytes = lengthInfo.Length.QuadPart;
        } else {
            g_disks[g_diskCount].sizeBytes = 0;
        }
        
        // Get disk model (via IOCTL)
        STORAGE_PROPERTY_QUERY query;
        memset(&query, 0, sizeof(query));
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;
        
        BYTE outBuf[512];
        if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                           outBuf, sizeof(outBuf), &bytesReturned, NULL)) {
            STORAGE_DEVICE_DESCRIPTOR *desc = (STORAGE_DEVICE_DESCRIPTOR *)outBuf;
            if (desc->ProductIdOffset) {
                char *productId = (char *)outBuf + desc->ProductIdOffset;
                mbstowcs(g_disks[g_diskCount].model, productId, 255);
            }
        }
        
        DetectPartitionTableType(i);
        
        CloseHandle(hDisk);
        g_diskCount++;
    }
    
    // Allocate and fill output array
    *outDisks = (DiskInfo *)malloc(g_diskCount * sizeof(DiskInfo));
    for (int i = 0; i < g_diskCount; i++) {
        (*outDisks)[i].diskNumber = g_disks[i].diskNumber;
        wcscpy((*outDisks)[i].model, g_disks[i].model);
        (*outDisks)[i].sizeBytes = g_disks[i].sizeBytes;
        (*outDisks)[i].tableType = g_disks[i].tableType;
        (*outDisks)[i].partitionCount = 0; // Will be filled by GetPartitionsOnDisk
    }
    
    return g_diskCount;
}

PART_API int GetPartitionsOnDisk(int diskNumber, PartitionEntry **outPartitions) {
    wchar_t path[64];
    swprintf(path, 64, L"\\\\.\\PhysicalDrive%d", diskNumber);
    
    HANDLE hDisk = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hDisk == INVALID_HANDLE_VALUE) return 0;
    
    // Get partition information
    BYTE layoutBuf[4096];
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0,
                                  layoutBuf, sizeof(layoutBuf), &bytesReturned, NULL);
    
    if (!result) {
        CloseHandle(hDisk);
        return 0;
    }
    
    DRIVE_LAYOUT_INFORMATION_EX *layout = (DRIVE_LAYOUT_INFORMATION_EX *)layoutBuf;
    int partCount = layout->PartitionCount;
    
    *outPartitions = (PartitionEntry *)malloc(partCount * sizeof(PartitionEntry));
    memset(*outPartitions, 0, partCount * sizeof(PartitionEntry));
    
    int validCount = 0;
    for (int i = 0; i < partCount; i++) {
        PARTITION_INFORMATION_EX *part = &layout->PartitionEntry[i];
        if (!part->PartitionLength.QuadPart) continue; // Empty partition
        
        PartitionEntry *entry = &(*outPartitions)[validCount];
        entry->diskNumber = diskNumber;
        entry->partitionNumber = part->PartitionNumber;
        entry->sizeBytes = part->PartitionLength.QuadPart;
        entry->isBootable = (layout->PartitionStyle == PARTITION_STYLE_MBR) ? part->Mbr.BootIndicator : FALSE;
        
        // Name
        if (layout->PartitionStyle == PARTITION_STYLE_GPT) {
            // Try to get partition name (may not be available)
            wcscpy(entry->name, L"GPT Partition");
            // Convert GUID to string
            swprintf(entry->id, 63, L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                     part->Gpt.PartitionType.Data1, part->Gpt.PartitionType.Data2, 
                     part->Gpt.PartitionType.Data3, part->Gpt.PartitionType.Data4[0],
                     part->Gpt.PartitionType.Data4[1], part->Gpt.PartitionType.Data4[2],
                     part->Gpt.PartitionType.Data4[3], part->Gpt.PartitionType.Data4[4],
                     part->Gpt.PartitionType.Data4[5], part->Gpt.PartitionType.Data4[6],
                     part->Gpt.PartitionType.Data4[7]);
        } else {
            swprintf(entry->name, 255, L"Partition %d", part->PartitionNumber);
            swprintf(entry->id, 63, L"0x%02X", part->Mbr.PartitionType);
        }
        
        // Location
        swprintf(entry->location, 511, L"Disk %d, Partition %d", diskNumber, part->PartitionNumber);
        
        // File system type (stub)
        wcscpy(entry->fsType, L"Unknown");
        
        // Drive letter
        entry->driveLetter[0] = L'\0';
        
        validCount++;
    }
    
    CloseHandle(hDisk);
    return validCount;
}

PART_API const wchar_t* GetPartitionTableTypeString(PartitionTableType type) {
    static const wchar_t *types[] = { L"MBR", L"GPT" };
    return types[type];
}

PART_API BOOL DeletePartition(int diskNumber, int partitionNumber) {
    // DANGEROUS! This will destroy data
    // For safety, just return FALSE (stub)
    return FALSE;
}

PART_API void AddToRepeatedDeleteList(int diskNumber, int partitionNumber, const wchar_t *location) {
    // Stub
}

PART_API void RemoveFromRepeatedDeleteList(int diskNumber, int partitionNumber) {
    // Stub
}

PART_API BOOL IsPartitionRepeatedDelete(int diskNumber, int partitionNumber) {
    // Stub
    return FALSE;
}

PART_API void SaveRepeatedDeleteConfig(void) {
    // Stub
}

PART_API void LoadRepeatedDeleteConfig(void) {
    // Stub
}
