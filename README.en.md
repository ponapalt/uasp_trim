# UASP SSD Full TRIM Tool for Windows

[日本語版 README はこちら](README.md)

A Windows tool to issue full disk TRIM commands (SCSI UNMAP) to USB external SSDs connected via UASP (USB Attached SCSI Protocol).

![UASP SSD Full Trim Tool Screenshot](uasp_trim_screenshot.png)

## Overview

This tool performs a simple SSD "reset" by directly issuing SCSI UNMAP commands to the entire SSD area **via a UASP-compatible USB-SSD adapter**.

## Warning

This tool issues TRIM to the entire disk. **Data recovery after execution is typically impossible with standard tools.**

Please verify the following before use:
- Data on the target drive is no longer needed
- The correct drive is selected
- Backups of important data are complete

**This is not a complete data erasure tool**, but on modern SSDs, data recovery from TRIMmed areas is very difficult, so it can be used as a simple erasure tool.

## Requirements

- Windows 10/11
- Administrator privileges
- UASP-compatible USB-SSD adapter

## Usage

1. Run `uasp_trim.exe`
2. Enter the drive number from the displayed drive list
3. Type `yes` at the confirmation prompt to execute

```
=== UASP SSD Full TRIM Tool ===

Available drives:
  [0] Sa**ung SSD 980 PRO - 1.0 TB
  [1] JM**ron Tech - 512.0 GB

Enter drive number to TRIM (or 'q' to quit): 1
```

## Limitations

- Only works with SSDs via UASP. For internal SSDs (M.2 or direct SATA), please use other tools.
- If you execute TRIM on a drive with open files or being browsed in Explorer, unexpected behavior may occur.
- Ideally, we would like to perform a Secure Erase equivalent, but many UASP bridge chips do not support this, so we settled for full-disk TRIM for now.

## Build Instructions

With a recent version of Visual Studio installed, run build.bat.

## Technical Details

- Uses SCSI UNMAP (opcode 0x42) command
- Issued via IOCTL_SCSI_PASS_THROUGH_DIRECT
- Automatically locks and dismounts volumes
- Sets disk offline before execution

Created with Vibe Coding using Claude Code, followed by author verification with actual SSDs and code review.

## License

Unlicense - Public Domain.
