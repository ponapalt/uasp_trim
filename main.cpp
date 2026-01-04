/**
 * UASP SSD Full TRIM Tool
 *
 * A tool to send full-disk TRIM (SCSI UNMAP) commands to USB-attached SSDs via UASP.
 *
 * Build: nmake
 * Or:    cl /EHsc /W4 /O2 main.cpp uasp_trim.res /Fe:uasp_trim.exe advapi32.lib
 */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <conio.h>

#pragma comment(lib, "advapi32.lib")

// Wait for keypress before exiting
void WaitForExit() {
    printf("\nPress any key to exit...");
    fflush(stdout);
    _getch();
}

// Maximum number of physical drives to enumerate
#define MAX_DRIVES 16

// Drive information structure
struct DriveInfo {
    int driveNumber;
    char model[256];
    ULONGLONG sizeBytes;
    bool available;
    bool isSystemDrive;
};

// Check if running as administrator
bool IsRunAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;

    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }

    return isAdmin != FALSE;
}

// Format size in human-readable form
void FormatSize(ULONGLONG bytes, char* buffer, size_t bufSize) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = (double)bytes;

    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }

    snprintf(buffer, bufSize, "%.1f %s", size, units[unitIndex]);
}

// Get device model name using STORAGE_PROPERTY_QUERY
bool GetDeviceModel(HANDLE hDevice, char* model, size_t modelSize) {
    STORAGE_PROPERTY_QUERY query = {0};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    BYTE buffer[4096] = {0};
    DWORD bytesReturned = 0;

    if (!DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
        &query, sizeof(query), buffer, sizeof(buffer),
        &bytesReturned, NULL)) {
        strncpy(model, "(Unknown)", modelSize);
        return false;
    }

    STORAGE_DEVICE_DESCRIPTOR* desc = (STORAGE_DEVICE_DESCRIPTOR*)buffer;

    // Build model string from vendor and product
    char vendor[128] = {0};
    char product[128] = {0};

    if (desc->VendorIdOffset != 0) {
        strncpy(vendor, (char*)buffer + desc->VendorIdOffset, sizeof(vendor) - 1);
        // Trim trailing spaces
        for (int i = (int)strlen(vendor) - 1; i >= 0 && vendor[i] == ' '; i--) {
            vendor[i] = '\0';
        }
    }

    if (desc->ProductIdOffset != 0) {
        strncpy(product, (char*)buffer + desc->ProductIdOffset, sizeof(product) - 1);
        // Trim trailing spaces
        for (int i = (int)strlen(product) - 1; i >= 0 && product[i] == ' '; i--) {
            product[i] = '\0';
        }
    }

    if (vendor[0] && product[0]) {
        snprintf(model, modelSize, "%s %s", vendor, product);
    } else if (product[0]) {
        strncpy(model, product, modelSize);
    } else if (vendor[0]) {
        strncpy(model, vendor, modelSize);
    } else {
        strncpy(model, "(Unknown Device)", modelSize);
    }

    return true;
}

// Get disk size using IOCTL_DISK_GET_DRIVE_GEOMETRY_EX
bool GetDiskSize(HANDLE hDevice, ULONGLONG* sizeBytes) {
    DISK_GEOMETRY_EX geometry = {0};
    DWORD bytesReturned = 0;

    if (!DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
        NULL, 0, &geometry, sizeof(geometry),
        &bytesReturned, NULL)) {
        *sizeBytes = 0;
        return false;
    }

    *sizeBytes = geometry.DiskSize.QuadPart;
    return true;
}

// Get the physical drive number where Windows is installed
int GetSystemDriveNumber() {
    char sysDir[MAX_PATH];
    if (!GetSystemDirectoryA(sysDir, sizeof(sysDir))) {
        return -1;
    }

    // Get the drive letter (e.g., "C")
    char volumePath[16];
    snprintf(volumePath, sizeof(volumePath), "\\\\.\\%c:", sysDir[0]);

    HANDLE hVolume = CreateFileA(volumePath,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);

    if (hVolume == INVALID_HANDLE_VALUE) {
        return -1;
    }

    VOLUME_DISK_EXTENTS extents;
    DWORD bytesReturned;
    int driveNumber = -1;

    if (DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
        NULL, 0, &extents, sizeof(extents), &bytesReturned, NULL)) {
        if (extents.NumberOfDiskExtents > 0) {
            driveNumber = (int)extents.Extents[0].DiskNumber;
        }
    }

    CloseHandle(hVolume);
    return driveNumber;
}

// Enumerate physical drives
int EnumerateDrives(DriveInfo* drives, int maxDrives) {
    int count = 0;
    int systemDriveNum = GetSystemDriveNumber();

    for (int i = 0; i < maxDrives; i++) {
        char path[64];
        snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%d", i);

        HANDLE hDevice = CreateFileA(path,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, 0, NULL);

        if (hDevice == INVALID_HANDLE_VALUE) {
            continue;
        }

        drives[count].driveNumber = i;
        drives[count].available = true;
        drives[count].isSystemDrive = (i == systemDriveNum);

        GetDeviceModel(hDevice, drives[count].model, sizeof(drives[count].model));
        GetDiskSize(hDevice, &drives[count].sizeBytes);

        CloseHandle(hDevice);
        count++;
    }

    return count;
}

// Get sector size for the drive
DWORD GetSectorSize(HANDLE hDevice) {
    DISK_GEOMETRY_EX geometry = {0};
    DWORD bytesReturned = 0;

    if (DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
        NULL, 0, &geometry, sizeof(geometry),
        &bytesReturned, NULL)) {
        return geometry.Geometry.BytesPerSector;
    }
    return 512; // Default
}

// SCSI UNMAP command structure
#pragma pack(push, 1)
struct UNMAP_BLOCK_DESCRIPTOR {
    BYTE UnmapLBA[8];        // Big-endian LBA
    BYTE UnmapBlocks[4];     // Big-endian block count
    BYTE Reserved[4];
};

struct UNMAP_PARAMETER_LIST {
    BYTE DataLength[2];           // Big-endian, total length - 2
    BYTE BlockDescLength[2];      // Big-endian, block descriptor length
    BYTE Reserved[4];
    UNMAP_BLOCK_DESCRIPTOR Descriptors[1];
};
#pragma pack(pop)

// Helper to set big-endian values
void SetBE16(BYTE* dest, USHORT val) {
    dest[0] = (BYTE)(val >> 8);
    dest[1] = (BYTE)(val & 0xFF);
}

void SetBE32(BYTE* dest, ULONG val) {
    dest[0] = (BYTE)(val >> 24);
    dest[1] = (BYTE)(val >> 16);
    dest[2] = (BYTE)(val >> 8);
    dest[3] = (BYTE)(val & 0xFF);
}

void SetBE64(BYTE* dest, ULONGLONG val) {
    dest[0] = (BYTE)(val >> 56);
    dest[1] = (BYTE)(val >> 48);
    dest[2] = (BYTE)(val >> 40);
    dest[3] = (BYTE)(val >> 32);
    dest[4] = (BYTE)(val >> 24);
    dest[5] = (BYTE)(val >> 16);
    dest[6] = (BYTE)(val >> 8);
    dest[7] = (BYTE)(val & 0xFF);
}

// Execute SCSI UNMAP command
bool ExecuteScsiUnmap(HANDLE hDevice, ULONGLONG startLBA, ULONG blockCount) {
    // Buffer for SCSI_PASS_THROUGH_DIRECT + sense data
    struct {
        SCSI_PASS_THROUGH_DIRECT sptd;
        BYTE sense[32];
    } sptwb;

    memset(&sptwb, 0, sizeof(sptwb));

    // UNMAP parameter list
    UNMAP_PARAMETER_LIST unmapParams;
    memset(&unmapParams, 0, sizeof(unmapParams));

    USHORT blockDescLen = sizeof(UNMAP_BLOCK_DESCRIPTOR);
    USHORT dataLen = 6 + blockDescLen;  // 8 bytes header - 2 for length field + descriptors

    SetBE16(unmapParams.DataLength, dataLen);
    SetBE16(unmapParams.BlockDescLength, blockDescLen);
    SetBE64(unmapParams.Descriptors[0].UnmapLBA, startLBA);
    SetBE32(unmapParams.Descriptors[0].UnmapBlocks, blockCount);

    // Setup SCSI_PASS_THROUGH_DIRECT
    sptwb.sptd.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
    sptwb.sptd.PathId = 0;
    sptwb.sptd.TargetId = 0;
    sptwb.sptd.Lun = 0;
    sptwb.sptd.CdbLength = 10;
    sptwb.sptd.SenseInfoLength = sizeof(sptwb.sense);
    sptwb.sptd.DataIn = SCSI_IOCTL_DATA_OUT;
    sptwb.sptd.DataTransferLength = sizeof(unmapParams);
    sptwb.sptd.TimeOutValue = 300;  // 5 minutes
    sptwb.sptd.DataBuffer = &unmapParams;
    sptwb.sptd.SenseInfoOffset = offsetof(decltype(sptwb), sense);

    // UNMAP CDB (opcode 0x42)
    sptwb.sptd.Cdb[0] = 0x42;  // UNMAP
    sptwb.sptd.Cdb[1] = 0;     // ANCHOR = 0
    sptwb.sptd.Cdb[6] = 0;     // Group number
    // Parameter list length (big-endian, bytes 7-8)
    sptwb.sptd.Cdb[7] = (BYTE)(sizeof(unmapParams) >> 8);
    sptwb.sptd.Cdb[8] = (BYTE)(sizeof(unmapParams) & 0xFF);
    sptwb.sptd.Cdb[9] = 0;     // Control

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(hDevice,
        IOCTL_SCSI_PASS_THROUGH_DIRECT,
        &sptwb, sizeof(sptwb),
        &sptwb, sizeof(sptwb),
        &bytesReturned, NULL);

    if (!result) {
        return false;
    }

    // Check SCSI status
    if (sptwb.sptd.ScsiStatus != 0) {
        printf("  SCSI error: Status=0x%02X, SenseKey=0x%02X, ASC=0x%02X, ASCQ=0x%02X\n",
            sptwb.sptd.ScsiStatus,
            sptwb.sense[2] & 0x0F,
            sptwb.sense[12],
            sptwb.sense[13]);
        return false;
    }

    return true;
}

// Get volumes on a physical drive and lock/dismount them
#define MAX_VOLUMES 26
struct VolumeHandle {
    HANDLE handle;
    char letter;
};

int LockVolumesOnDrive(int driveNumber, VolumeHandle* volumes) {
    int count = 0;

    // Check each drive letter
    for (char letter = 'A'; letter <= 'Z'; letter++) {
        char volumePath[16];
        snprintf(volumePath, sizeof(volumePath), "\\\\.\\%c:", letter);

        HANDLE hVolume = CreateFileA(volumePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, 0, NULL);

        if (hVolume == INVALID_HANDLE_VALUE) {
            continue;
        }

        // Get the physical drive number for this volume
        VOLUME_DISK_EXTENTS extents;
        DWORD bytesReturned;

        if (DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
            NULL, 0, &extents, sizeof(extents), &bytesReturned, NULL)) {

            for (DWORD i = 0; i < extents.NumberOfDiskExtents; i++) {
                if ((int)extents.Extents[i].DiskNumber == driveNumber) {
                    // This volume is on our target drive
                    printf("  Locking volume %c:...\n", letter);

                    // Lock the volume
                    if (!DeviceIoControl(hVolume, FSCTL_LOCK_VOLUME,
                        NULL, 0, NULL, 0, &bytesReturned, NULL)) {
                        printf("  Warning: Could not lock volume %c: (error %lu)\n",
                            letter, GetLastError());
                    }

                    // Dismount the volume
                    if (!DeviceIoControl(hVolume, FSCTL_DISMOUNT_VOLUME,
                        NULL, 0, NULL, 0, &bytesReturned, NULL)) {
                        printf("  Warning: Could not dismount volume %c: (error %lu)\n",
                            letter, GetLastError());
                    }

                    volumes[count].handle = hVolume;
                    volumes[count].letter = letter;
                    count++;
                    hVolume = INVALID_HANDLE_VALUE; // Don't close, we need to keep it locked
                    break;
                }
            }
        }

        if (hVolume != INVALID_HANDLE_VALUE) {
            CloseHandle(hVolume);
        }
    }

    return count;
}

void UnlockVolumes(VolumeHandle* volumes, int count) {
    for (int i = 0; i < count; i++) {
        if (volumes[i].handle != INVALID_HANDLE_VALUE) {
            DWORD bytesReturned;
            DeviceIoControl(volumes[i].handle, FSCTL_UNLOCK_VOLUME,
                NULL, 0, NULL, 0, &bytesReturned, NULL);
            CloseHandle(volumes[i].handle);
        }
    }
}

// Set disk offline/online
bool SetDiskOffline(HANDLE hDevice, bool offline) {
    SET_DISK_ATTRIBUTES attrs = {0};
    attrs.Version = sizeof(SET_DISK_ATTRIBUTES);
    attrs.Attributes = offline ? DISK_ATTRIBUTE_OFFLINE : 0;
    attrs.AttributesMask = DISK_ATTRIBUTE_OFFLINE;

    DWORD bytesReturned;
    return DeviceIoControl(hDevice, IOCTL_DISK_SET_DISK_ATTRIBUTES,
        &attrs, sizeof(attrs), NULL, 0, &bytesReturned, NULL) != FALSE;
}

// Execute full TRIM on a drive using SCSI UNMAP
bool ExecuteFullTrim(int driveNumber, ULONGLONG diskSize) {
    char path[64];
    snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%d", driveNumber);

    // First, lock and dismount all volumes on this drive
    VolumeHandle volumes[MAX_VOLUMES] = {0};
    int volumeCount = LockVolumesOnDrive(driveNumber, volumes);

    // Open with exclusive access
    HANDLE hDevice = CreateFileA(path,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Error: Failed to open drive. Error code: %lu\n", GetLastError());
        UnlockVolumes(volumes, volumeCount);
        return false;
    }

    // Try to set disk offline for exclusive access
    printf("  Setting disk offline...\n");
    bool wasOnline = true;
    if (!SetDiskOffline(hDevice, true)) {
        printf("  Note: Could not set disk offline (error %lu), continuing anyway...\n",
            GetLastError());
        wasOnline = false;  // Don't try to set back online
    }

    DWORD sectorSize = GetSectorSize(hDevice);
    ULONGLONG totalBlocks = diskSize / sectorSize;

    printf("  Disk size: %llu bytes (%llu blocks), Sector size: %lu\n",
        diskSize, totalBlocks, sectorSize);
    printf("  Sending SCSI UNMAP command...\n");

    // UNMAP has a limit on block count per descriptor (typically 0xFFFFFFFF)
    // We'll send in chunks if needed
    const ULONG MAX_BLOCKS_PER_UNMAP = 0xFFFFFFFF;
    ULONGLONG remainingBlocks = totalBlocks;
    ULONGLONG currentLBA = 0;
    bool success = true;

    while (remainingBlocks > 0) {
        ULONG blocksThisRound = (remainingBlocks > MAX_BLOCKS_PER_UNMAP)
            ? MAX_BLOCKS_PER_UNMAP
            : (ULONG)remainingBlocks;

        if (!ExecuteScsiUnmap(hDevice, currentLBA, blocksThisRound)) {
            DWORD lastError = GetLastError();
            printf("Error: SCSI UNMAP failed at LBA %llu. Error code: %lu\n",
                currentLBA, lastError);
            success = false;
            break;
        }

        currentLBA += blocksThisRound;
        remainingBlocks -= blocksThisRound;

        if (totalBlocks > MAX_BLOCKS_PER_UNMAP) {
            printf("  Progress: %.1f%%\r",
                (double)(totalBlocks - remainingBlocks) / totalBlocks * 100.0);
            fflush(stdout);
        }
    }

    // Set disk back online if we set it offline
    if (wasOnline) {
        printf("  Setting disk back online...\n");
        SetDiskOffline(hDevice, false);
    }

    CloseHandle(hDevice);
    UnlockVolumes(volumes, volumeCount);

    return success;
}

int main() {
    // Set console to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    setlocale(LC_ALL, "");

    printf("=== UASP SSD Full TRIM Tool ===\n\n");

    // Check admin privileges
    if (!IsRunAsAdmin()) {
        printf("Error: This tool requires administrator privileges.\n");
        printf("Please run as administrator.\n");
        WaitForExit();
        return 1;
    }

    // Enumerate drives
    DriveInfo drives[MAX_DRIVES];
    int driveCount = EnumerateDrives(drives, MAX_DRIVES);

    if (driveCount == 0) {
        printf("No drives found.\n");
        WaitForExit();
        return 1;
    }

    // Display drive list
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    WORD originalAttrs = consoleInfo.wAttributes;

    printf("Available drives:\n");
    for (int i = 0; i < driveCount; i++) {
        char sizeStr[32];
        FormatSize(drives[i].sizeBytes, sizeStr, sizeof(sizeStr));
        if (drives[i].isSystemDrive) {
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            printf("  [%d] %s - %s [SYSTEM DRIVE - Windows]\n",
                drives[i].driveNumber, drives[i].model, sizeStr);
            SetConsoleTextAttribute(hConsole, originalAttrs);
        } else {
            printf("  [%d] %s - %s\n", drives[i].driveNumber, drives[i].model, sizeStr);
        }
    }
    printf("\n");

    // Get user selection
    printf("Enter drive number to TRIM (or 'q' to quit): ");
    fflush(stdout);

    char input[32];
    if (!fgets(input, sizeof(input), stdin)) {
        WaitForExit();
        return 1;
    }

    // Check for quit
    if (input[0] == 'q' || input[0] == 'Q') {
        printf("Operation cancelled.\n");
        WaitForExit();
        return 0;
    }

    int selectedDriveNum = atoi(input);

    // Find the selected drive in our list
    int selectedIndex = -1;
    for (int i = 0; i < driveCount; i++) {
        if (drives[i].driveNumber == selectedDriveNum) {
            selectedIndex = i;
            break;
        }
    }

    if (selectedIndex < 0) {
        printf("Error: Invalid drive number.\n");
        WaitForExit();
        return 1;
    }

    DriveInfo* selected = &drives[selectedIndex];
    char sizeStr[32];
    FormatSize(selected->sizeBytes, sizeStr, sizeof(sizeStr));

    // Final warning with red color
    printf("\n");

    // Set bright red background with white text
    SetConsoleTextAttribute(hConsole, BACKGROUND_RED | BACKGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    printf("                                                               \n");
    printf("  *** WARNING: ALL DATA ON THIS DRIVE WILL BE DESTROYED! ***   \n");
    printf("                                                               \n");
    SetConsoleTextAttribute(hConsole, originalAttrs);

    printf("\n");
    // Red text for drive info
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
    printf("  Target: [%d] %s (%s)\n", selected->driveNumber, selected->model, sizeStr);
    SetConsoleTextAttribute(hConsole, originalAttrs);

    printf("\n");
    printf("This operation will send a TRIM command to the entire drive.\n");
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
    printf("Data recovery will be IMPOSSIBLE after this operation.\n");
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf("\n");
    printf("Type 'yes' to confirm: ");
    fflush(stdout);

    if (!fgets(input, sizeof(input), stdin)) {
        WaitForExit();
        return 1;
    }

    // Remove newline
    input[strcspn(input, "\r\n")] = '\0';

    if (strcmp(input, "yes") != 0) {
        printf("Operation cancelled.\n");
        WaitForExit();
        return 0;
    }

    // Execute TRIM
    printf("\nExecuting full TRIM on PhysicalDrive%d...\n", selected->driveNumber);

    if (ExecuteFullTrim(selected->driveNumber, selected->sizeBytes)) {
        printf("TRIM completed successfully.\n");
        WaitForExit();
        return 0;
    } else {
        printf("TRIM failed.\n");
        WaitForExit();
        return 1;
    }
}
