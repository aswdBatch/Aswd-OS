#Requires -RunAsAdministrator
param([string]$Image = "dist\aswd-usb.img")

$imgPath = Join-Path $PSScriptRoot "..\$Image"
if (-not (Test-Path $imgPath)) { Write-Error "Image not found: $imgPath"; exit 1 }

Write-Host "USB drives detected:"
$usbDisks = Get-Disk | Where-Object { $_.BusType -eq 'USB' }
if (-not $usbDisks) {
    Write-Error "No USB drives found. Plug in your USB drive and try again."
    exit 1
}
$usbDisks | ForEach-Object {
    Write-Host "  [$($_.Number)] $($_.FriendlyName)  $([math]::Round($_.Size/1GB,1)) GB"
}

$n = Read-Host "Enter disk number to flash (WILL ERASE ALL DATA)"
if ($n -notmatch '^\d+$') { Write-Error "Invalid disk number."; exit 1 }

$diskCheck = Get-Disk -Number ([int]$n) -ErrorAction SilentlyContinue
if (-not $diskCheck) { Write-Error "Disk $n not found."; exit 1 }
if ($diskCheck.BusType -ne 'USB') {
    Write-Error "Disk $n is not a USB drive (BusType=$($diskCheck.BusType)). Aborting for safety."
    exit 1
}

# Load Win32 helpers for locking volumes
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public class RawDisk {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Auto)]
    public static extern SafeFileHandle CreateFile(
        string lpFileName, uint dwDesiredAccess, uint dwShareMode,
        IntPtr lpSecurityAttributes, uint dwCreationDisposition,
        uint dwFlagsAndAttributes, IntPtr hTemplateFile);

    [DllImport("kernel32.dll", ExactSpelling=true, SetLastError=true)]
    public static extern bool DeviceIoControl(
        SafeFileHandle hDevice, uint dwIoControlCode,
        IntPtr lpInBuffer, uint nInBufferSize,
        IntPtr lpOutBuffer, uint nOutBufferSize,
        out uint lpBytesReturned, IntPtr lpOverlapped);

    public const uint GENERIC_WRITE        = 0x40000000;
    public const uint FILE_SHARE_READ      = 0x00000001;
    public const uint FILE_SHARE_WRITE     = 0x00000002;
    public const uint OPEN_EXISTING        = 3;
    public const uint FSCTL_LOCK_VOLUME    = 0x00090018;
    public const uint FSCTL_DISMOUNT_VOLUME = 0x00090020;
    public const uint IOCTL_DISK_UPDATE_PROPERTIES = 0x00070140;
}
"@

# Lock and dismount every volume on the disk so Windows releases its hold
$volHandles = @()
$partitions = Get-Partition -DiskNumber ([int]$n) -ErrorAction SilentlyContinue
foreach ($part in $partitions) {
    $letter = $part.DriveLetter
    if (-not $letter -or $letter -eq "`0") { continue }
    $volPath = "\\.\${letter}:"
    Write-Host "  Locking volume $volPath ..."
    $hVol = [RawDisk]::CreateFile($volPath,
        [RawDisk]::GENERIC_WRITE,
        ([RawDisk]::FILE_SHARE_READ -bor [RawDisk]::FILE_SHARE_WRITE),
        [IntPtr]::Zero, [RawDisk]::OPEN_EXISTING, 0, [IntPtr]::Zero)
    if ($hVol.IsInvalid) {
        Write-Warning "Could not open $volPath - continuing anyway."
        continue
    }
    $bytes = 0
    [void][RawDisk]::DeviceIoControl($hVol, [RawDisk]::FSCTL_LOCK_VOLUME,
        [IntPtr]::Zero, 0, [IntPtr]::Zero, 0, [ref]$bytes, [IntPtr]::Zero)
    [void][RawDisk]::DeviceIoControl($hVol, [RawDisk]::FSCTL_DISMOUNT_VOLUME,
        [IntPtr]::Zero, 0, [IntPtr]::Zero, 0, [ref]$bytes, [IntPtr]::Zero)
    $volHandles += $hVol
}

$target = "\\.\PhysicalDrive$n"
$imgSize = (Get-Item $imgPath).Length
$imgSizeMB = [math]::Round($imgSize / 1048576, 1)
Write-Host "Writing $imgPath ($imgSizeMB MB) -> $target ..."

try {
    $src = [System.IO.File]::OpenRead($imgPath)
    $dst = [System.IO.File]::Open($target, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Write)
    $src.CopyTo($dst)
    $dst.Flush()
    $dst.Close()
    $src.Close()
    Write-Host "Done. Safely eject and boot."
} finally {
    foreach ($h in $volHandles) { $h.Close() }
    # Tell Windows the partition table changed
    $hDisk = [RawDisk]::CreateFile($target,
        [RawDisk]::GENERIC_WRITE,
        ([RawDisk]::FILE_SHARE_READ -bor [RawDisk]::FILE_SHARE_WRITE),
        [IntPtr]::Zero, [RawDisk]::OPEN_EXISTING, 0, [IntPtr]::Zero)
    if (-not $hDisk.IsInvalid) {
        $bytes = 0
        [void][RawDisk]::DeviceIoControl($hDisk, [RawDisk]::IOCTL_DISK_UPDATE_PROPERTIES,
            [IntPtr]::Zero, 0, [IntPtr]::Zero, 0, [ref]$bytes, [IntPtr]::Zero)
        $hDisk.Close()
    }
}
