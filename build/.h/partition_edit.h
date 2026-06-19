#ifndef PARTITION_EDIT_H
#define PARTITION_EDIT_H

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma pack(push, 1)
typedef struct {
    BYTE  bootFlag;
    BYTE  startCHS[3];
    BYTE  partitionType;
    BYTE  endCHS[3];
    DWORD startLBA;
    DWORD sectorCount;
} MBRPartitionEntry;

typedef struct {
    BYTE                code[446];
    MBRPartitionEntry partitions[4];
    WORD                signature;
} MBR;

typedef struct {
    BYTE  signature[8];
    WORD  revision;
    WORD  headerSize;
    DWORD headerCRC32;
    DWORD reserved;
    ULONGLONG currentLBA;
    ULONGLONG backupLBA;
    ULONGLONG firstUsableLBA;
    ULONGLONG lastUsableLBA;
    BYTE  diskGUID[16];
    ULONGLONG partEntryLBA;
    DWORD partEntryCount;
    DWORD partEntrySize;
    DWORD partArrayCRC32;
    BYTE  reserved2[420];
} GPTHeader;
#pragma pack(pop)

// Open physical drive (read-only by default)
HANDLE PE_OpenDrive(int driveNum, BOOL writable);
// Read a sector
BOOL   PE_ReadSector(HANDLE hDrive, ULONG64 sectorNum, BYTE *buffer);
// Write a sector
BOOL   PE_WriteSector(HANDLE hDrive, ULONG64 sectorNum, BYTE *buffer);
// Close drive
void   PE_CloseDrive(HANDLE hDrive);
// Get partition table type: 0=unknown, 1=MBR, 2=GPT
int    PE_GetPartitionTableType(HANDLE hDrive);
// Read/Write MBR
BOOL   PE_ReadMBR(HANDLE hDrive, MBR *mbr);
BOOL   PE_WriteMBR(HANDLE hDrive, MBR *mbr);
// Read GPT header (LBA 1)
BOOL   PE_ReadGPTHeader(HANDLE hDrive, GPTHeader *gpt);
// Write GPT header (LBA 1) and update CRC
BOOL   PE_WriteGPTHeader(HANDLE hDrive, GPTHeader *gpt);
// Convert MBR to GPT (basic)
BOOL   PE_ConvertMBRToGPT(HANDLE hDrive);
// Convert GPT to MBR (basic)
BOOL   PE_ConvertGPTToMBR(HANDLE hDrive);
// Get disk size in sectors (try IOCTL_DISK_GET_DRIVE_GEOMETRY_EX)
BOOL   PE_GetDiskSize(HANDLE hDrive, ULONG64 *totalSectors);

/* Open the hex editor for sector 0 of the selected disk.
 * If diskNumber >= 0, skips the disk picker dialog and opens that disk directly. */
BOOL   PE_OpenSectorEditorEx(HWND hwndParent, int diskNumber);
BOOL   PE_OpenSectorEditor(HWND hwndParent);

/* Add disk/partition to repeated delete list (writes to data\part_repeated.txt). */
BOOL   PE_AddToRepeatedDelete(int disk, int partition);

/* Add disk to protected list (snapshot saved to data\partitions\diskN.snapshot). */
BOOL   PE_AddToProtected(int disk);

#endif
