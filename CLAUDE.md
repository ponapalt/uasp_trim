# UASP SSD Full TRIM Tool - Developer Documentation

This document is for AI assistants (like Claude Code) and developers working on this project.

## Project Overview

A Windows command-line tool that issues SCSI UNMAP (TRIM) commands to USB-attached SSDs via UASP (USB Attached SCSI Protocol). The tool performs a full-disk TRIM operation, effectively resetting the SSD.

## File Structure

```
uasp_trim/
├── main.cpp                 - Main implementation (630 lines)
├── Makefile                 - nmake build configuration
├── build.bat                - Automated build script with VS2022 environment setup
├── uasp_trim.rc             - Windows resource file
├── uasp_trim.manifest       - Application manifest (requires administrator)
├── README.md                - Japanese documentation
├── README.en.md             - English documentation
├── LICENSE                  - Unlicense (Public Domain)
└── uasp_trim_screenshot.png - Screenshot for documentation
```

## Implementation Details

### Core Functionality (main.cpp)

The implementation is a single-file C++ program with the following key components:

#### 1. Drive Enumeration
- `EnumerateDrives()` (lines 176-205): Scans PhysicalDrive0-15
- `GetDeviceModel()` (lines 74-122): Retrieves drive model via STORAGE_PROPERTY_QUERY
- `GetDiskSize()` (lines 125-138): Gets disk size via IOCTL_DISK_GET_DRIVE_GEOMETRY_EX
- `GetSystemDriveNumber()` (lines 141-173): Identifies Windows system drive to warn users

#### 2. Volume Management
- `LockVolumesOnDrive()` (lines 335-393): Locks and dismounts all volumes on target drive
- Uses IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS to map volumes to physical drives
- Uses FSCTL_LOCK_VOLUME and FSCTL_DISMOUNT_VOLUME
- `UnlockVolumes()` (lines 395-404): Cleanup function

#### 3. SCSI UNMAP Command
- `ExecuteScsiUnmap()` (lines 261-326): Core TRIM implementation
- Uses IOCTL_SCSI_PASS_THROUGH_DIRECT to send SCSI commands directly
- Constructs SCSI UNMAP command (opcode 0x42) with proper parameter list
- Big-endian helper functions (lines 237-258): SetBE16, SetBE32, SetBE64

#### 4. SCSI UNMAP Command Structure (lines 221-234)
```cpp
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
```

#### 5. Disk Offline Management
- `SetDiskOffline()` (lines 407-416): Sets disk offline before TRIM, online after
- Uses IOCTL_DISK_SET_DISK_ATTRIBUTES

#### 6. Main TRIM Execution
- `ExecuteFullTrim()` (lines 419-495): Orchestrates the full TRIM process
  1. Lock and dismount volumes
  2. Open drive with exclusive access
  3. Set disk offline
  4. Calculate total blocks
  5. Send UNMAP commands (in chunks if needed, max 0xFFFFFFFF blocks per command)
  6. Show progress for large drives
  7. Set disk back online
  8. Unlock volumes

#### 7. User Interface
- `main()` (lines 497-632): User interaction flow
  - Admin privilege check (lines 505-510)
  - Drive enumeration and display (lines 513-541)
  - User selection with input validation (lines 544-576)
  - Multiple warnings with colored console output (lines 582-618)
  - "yes" confirmation required (lines 614-618)
  - System drive highlighted in yellow (lines 532-536)

### Technical Specifications

- **Language**: C++ (C++11 compatible)
- **Platform**: Windows 10/11 (x64 primary, x86 supported)
- **Compiler**: Microsoft Visual C++ (MSVC)
- **Required privileges**: Administrator (enforced via manifest)
- **Windows APIs used**:
  - IOCTL_STORAGE_QUERY_PROPERTY - Get device info
  - IOCTL_DISK_GET_DRIVE_GEOMETRY_EX - Get disk geometry
  - IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS - Map volumes to drives
  - FSCTL_LOCK_VOLUME - Lock volumes
  - FSCTL_DISMOUNT_VOLUME - Dismount volumes
  - IOCTL_DISK_SET_DISK_ATTRIBUTES - Set disk offline/online
  - IOCTL_SCSI_PASS_THROUGH_DIRECT - Send SCSI commands
- **SCSI Command**: UNMAP (opcode 0x42) with 300-second timeout
- **Chunk size**: Maximum 0xFFFFFFFF blocks per UNMAP command

### Build System

Two build methods are supported:

1. **build.bat** (Recommended for users)
   - Automatically finds VS2022 installation using vswhere.exe
   - Sets up environment (vcvars64.bat or vcvars32.bat)
   - Compiles resource file and main.cpp
   - Links with advapi32.lib

2. **nmake** (For developers)
   - `nmake` - Release build (optimized, /O2)
   - `nmake debug` - Debug build (/Od /Zi)
   - `nmake clean` - Clean artifacts

Compiler flags:
- Release: `/nologo /EHsc /W4 /O2 /DNDEBUG`
- Debug: `/nologo /EHsc /W4 /Od /Zi /DDEBUG`

## Limitations and Constraints

1. **UASP-only**: The tool only works with UASP (USB Attached SCSI Protocol) devices
   - Does not work with internal M.2 or SATA drives
   - Reason: Uses SCSI UNMAP instead of ATA TRIM

2. **Not Secure Erase**: Uses TRIM/UNMAP instead of proper Secure Erase
   - Reason: Most UASP bridge chips don't support Secure Erase commands
   - TRIM is a compromise solution

3. **Volume state sensitivity**: May behave unexpectedly if files are open on the target drive
   - Mitigation: Tool locks and dismounts volumes before TRIM

## Development Guidelines

### When Adding Features

**CRITICAL: Always update both README files when adding features**

1. Make code changes in main.cpp
2. Update README.md (Japanese version)
3. Update README.en.md (English version)
4. Ensure both READMEs have matching information
5. Keep both files in sync for:
   - New features
   - New limitations or warnings
   - Changed requirements
   - Updated usage instructions
   - Technical details

### Common Development Tasks

#### Adding a new command-line option
1. Parse in main() around lines 544-576
2. Update help text in both READMEs
3. Document in "Usage" section of both READMEs

#### Changing SCSI command behavior
1. Modify ExecuteScsiUnmap() (lines 261-326)
2. Update "Technical Details" in both READMEs if SCSI command changes
3. Test with actual hardware (UASP adapter + SSD)

#### Adding new drive filters or safety checks
1. Modify EnumerateDrives() or main()
2. Update "Limitations" in both READMEs
3. Update warning messages if applicable

#### Improving error handling
1. Add error checks in appropriate functions
2. Update error message list if user-visible errors change

### Code Style

The codebase follows these conventions:
- Windows-style line endings (CRLF)
- Indentation: 4 spaces (not tabs)
- Brace style: Allman style (opening brace on new line)
- Naming: PascalCase for functions, camelCase for variables
- Comments: C++-style `//` for single-line, `/** */` for file headers

### Testing Checklist

Before committing changes:
- [ ] Code compiles with no warnings (W4 level)
- [ ] Tested with administrator privileges
- [ ] Tested drive enumeration shows correct drives
- [ ] System drive is correctly identified and highlighted
- [ ] Confirmation prompt works correctly
- [ ] If possible, tested actual TRIM operation on non-critical SSD
- [ ] README.md updated (Japanese)
- [ ] README.en.md updated (English)
- [ ] Both README files have matching content

## History

This tool was created using "Vibe Coding" with Claude Code, followed by author verification with actual SSDs and code review.

## License

Unlicense - Public Domain. See LICENSE file.
