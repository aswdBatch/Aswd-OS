#!/usr/bin/env python3
import argparse
import math
import struct
import subprocess
from pathlib import Path


SECTOR_SIZE = 512
IMAGE_SIZE = 64 * 1024 * 1024
PARTITION_START = 2048
RESERVED_SECTORS = 32
FAT_COUNT = 2
SECTORS_PER_CLUSTER = 8


def write_u16(buffer: bytearray, offset: int, value: int) -> None:
    buffer[offset:offset + 2] = struct.pack("<H", value & 0xFFFF)


def write_u32(buffer: bytearray, offset: int, value: int) -> None:
    buffer[offset:offset + 4] = struct.pack("<I", value & 0xFFFFFFFF)


def assemble(source: Path, output: Path, defines: dict[str, int]) -> bytes:
    command = ["nasm", "-f", "bin"]
    for name, value in defines.items():
        command.append(f"-D{name}={value}")
    command.extend([str(source), "-o", str(output)])
    subprocess.run(command, check=True)
    return output.read_bytes()


def fat_name(name: str, extension: str) -> bytes:
    base = name.upper().ljust(8)[:8]
    ext = extension.upper().ljust(3)[:3]
    return (base + ext).encode("ascii")


def make_dir_entry(name: str, extension: str, cluster: int, size: int, attr: int = 0x20) -> bytes:
    entry = bytearray(32)
    entry[0:11] = fat_name(name, extension)
    entry[11] = attr
    write_u16(entry, 20, (cluster >> 16) & 0xFFFF)
    write_u16(entry, 26, cluster & 0xFFFF)
    write_u32(entry, 28, size)
    return bytes(entry)


def make_dot_dir_entry(name: str, cluster: int) -> bytes:
    entry = bytearray(32)
    entry[0:11] = b"           "
    encoded = name.encode("ascii")
    entry[0:len(encoded)] = encoded
    entry[11] = 0x10
    write_u16(entry, 20, (cluster >> 16) & 0xFFFF)
    write_u16(entry, 26, cluster & 0xFFFF)
    return bytes(entry)


def make_volume_label(label: str) -> bytes:
    entry = bytearray(32)
    entry[0:11] = label.upper().ljust(11)[:11].encode("ascii")
    entry[11] = 0x08
    return bytes(entry)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--kernel-bin", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    build_dir = repo_root / "build" / "usbboot"
    build_dir.mkdir(parents=True, exist_ok=True)

    mbr_source = repo_root / "src" / "usbboot" / "mbr.asm"
    vbr_source = repo_root / "src" / "usbboot" / "vbr.asm"
    stage2_source = repo_root / "src" / "usbboot" / "stage2.asm"
    mbr_bin = build_dir / "mbr.bin"
    vbr_bin = build_dir / "vbr.bin"
    stage2_bin = build_dir / "stage2.bin"

    kernel_path = Path(args.kernel_bin)
    output_path = Path(args.output)

    kernel_bytes = kernel_path.read_bytes()
    stage2_probe = assemble(stage2_source, stage2_bin, {
        "KERNEL_LBA": 0,
        "KERNEL_SECTORS": math.ceil(len(kernel_bytes) / SECTOR_SIZE),
    })
    stage2_sectors = math.ceil(len(stage2_probe) / SECTOR_SIZE)
    kernel_sectors = math.ceil(len(kernel_bytes) / SECTOR_SIZE)

    image_sectors = IMAGE_SIZE // SECTOR_SIZE
    partition_sectors = image_sectors - PARTITION_START

    data_area_sectors = partition_sectors - RESERVED_SECTORS
    fats_size = math.ceil(((data_area_sectors / SECTORS_PER_CLUSTER) + 2) * 4 / SECTOR_SIZE)
    if fats_size < 1:
        fats_size = 1

    data_start = PARTITION_START + RESERVED_SECTORS + (FAT_COUNT * fats_size)
    stage2_lba = data_start + SECTORS_PER_CLUSTER
    kernel_lba = stage2_lba + stage2_sectors
    kernel_clusters = math.ceil(kernel_sectors / SECTORS_PER_CLUSTER)
    kernel_start_cluster = 4
    users_cluster = kernel_start_cluster + kernel_clusters
    root_cluster = users_cluster + 1

    mbr_bytes = assemble(mbr_source, mbr_bin, {
        "PARTITION_START": PARTITION_START,
        "PARTITION_SECTORS": partition_sectors,
    })
    stage2_bytes = assemble(stage2_source, stage2_bin, {
        "KERNEL_LBA": kernel_lba,
        "KERNEL_SECTORS": kernel_sectors,
    })

    vbr_bytes = assemble(vbr_source, vbr_bin, {
        "PARTITION_START": PARTITION_START,
        "PARTITION_SECTORS": partition_sectors,
        "SECTORS_PER_CLUSTER": SECTORS_PER_CLUSTER,
        "RESERVED_SECTORS": RESERVED_SECTORS,
        "FAT_SECTORS": fats_size,
        "STAGE2_LBA": stage2_lba,
        "STAGE2_SECTORS": stage2_sectors,
    })

    image = bytearray(IMAGE_SIZE)
    image[0:len(mbr_bytes)] = mbr_bytes

    # Inject a standard DOS partition table entry at MBR offset 446 so the
    # ATA driver can locate the FAT32 partition when booting via ATA (QEMU).
    part_entry = bytearray(16)
    part_entry[0] = 0x80
    part_entry[4] = 0x0C
    part_entry[1] = 0xFE
    part_entry[2] = 0xFF
    part_entry[3] = 0xFF
    part_entry[5] = 0xFE
    part_entry[6] = 0xFF
    part_entry[7] = 0xFF
    struct.pack_into("<I", part_entry, 8, PARTITION_START)
    struct.pack_into("<I", part_entry, 12, partition_sectors)
    image[446:462] = bytes(part_entry)
    image[510] = 0x55
    image[511] = 0xAA

    image[PARTITION_START * SECTOR_SIZE:PARTITION_START * SECTOR_SIZE + len(vbr_bytes)] = vbr_bytes
    backup_offset = (PARTITION_START + 6) * SECTOR_SIZE
    image[backup_offset:backup_offset + len(vbr_bytes)] = vbr_bytes

    fsinfo = bytearray(SECTOR_SIZE)
    write_u32(fsinfo, 0, 0x41615252)
    write_u32(fsinfo, 484, 0x61417272)
    write_u32(fsinfo, 488, 0xFFFFFFFF)
    write_u32(fsinfo, 492, 0xFFFFFFFF)
    write_u32(fsinfo, 508, 0xAA550000)
    image[(PARTITION_START + 1) * SECTOR_SIZE:(PARTITION_START + 2) * SECTOR_SIZE] = fsinfo

    # Auth credential sector (PARTITION_START + 2).
    # Layout of the FAT32 reserved area (32 sectors):
    #   +0  VBR (boot sector)
    #   +1  FSInfo
    #   +2  AUTH sector - reserved for auth_store; zeroed = no admin exists
    #   +3..+5  unused
    #   +6  backup VBR
    #   +7..+31 unused
    # A zeroed AUTH sector means magic != AUTH_MAGIC, so auth_store_has_admin()
    # returns 0 and the first-boot Setup screen is shown.
    auth_sector_offset = (PARTITION_START + 2) * SECTOR_SIZE
    image[auth_sector_offset:auth_sector_offset + SECTOR_SIZE] = bytes(SECTOR_SIZE)

    fat = bytearray(fats_size * SECTOR_SIZE)
    write_u32(fat, 0, 0x0FFFFFF8)
    write_u32(fat, 4, 0x0FFFFFFF)
    write_u32(fat, 8, 0x0FFFFFFF)
    write_u32(fat, 12, 0x0FFFFFFF)

    for cluster in range(kernel_start_cluster, kernel_start_cluster + kernel_clusters - 1):
        write_u32(fat, cluster * 4, cluster + 1)
    if kernel_clusters > 0:
        write_u32(fat, (kernel_start_cluster + kernel_clusters - 1) * 4, 0x0FFFFFFF)
    write_u32(fat, users_cluster * 4, 0x0FFFFFFF)
    write_u32(fat, root_cluster * 4, 0x0FFFFFFF)

    fat1_offset = (PARTITION_START + RESERVED_SECTORS) * SECTOR_SIZE
    fat2_offset = fat1_offset + fats_size * SECTOR_SIZE
    image[fat1_offset:fat1_offset + len(fat)] = fat
    image[fat2_offset:fat2_offset + len(fat)] = fat

    root_dir = bytearray(SECTORS_PER_CLUSTER * SECTOR_SIZE)
    root_dir[0:32] = make_volume_label("ASWDOS")
    root_dir[32:64] = make_dir_entry("STAGE2", "BIN", 3, len(stage2_bytes))
    root_dir[64:96] = make_dir_entry("KERNEL", "BIN", kernel_start_cluster, len(kernel_bytes))
    root_dir[96:128] = make_dir_entry("USERS", "", users_cluster, 0, 0x10)
    root_dir[128:160] = make_dir_entry("ROOT", "", root_cluster, 0, 0x10)
    root_dir[160] = 0x00
    image[data_start * SECTOR_SIZE:(data_start + SECTORS_PER_CLUSTER) * SECTOR_SIZE] = root_dir

    users_dir = bytearray(SECTORS_PER_CLUSTER * SECTOR_SIZE)
    users_dir[0:32] = make_dot_dir_entry(".", users_cluster)
    users_dir[32:64] = make_dot_dir_entry("..", 2)
    users_dir[64] = 0x00
    users_dir_lba = data_start + (users_cluster - 2) * SECTORS_PER_CLUSTER
    image[users_dir_lba * SECTOR_SIZE:(users_dir_lba + SECTORS_PER_CLUSTER) * SECTOR_SIZE] = users_dir

    root_workspace_dir = bytearray(SECTORS_PER_CLUSTER * SECTOR_SIZE)
    root_workspace_dir[0:32] = make_dot_dir_entry(".", root_cluster)
    root_workspace_dir[32:64] = make_dot_dir_entry("..", 2)
    root_workspace_dir[64] = 0x00
    root_workspace_lba = data_start + (root_cluster - 2) * SECTORS_PER_CLUSTER
    image[root_workspace_lba * SECTOR_SIZE:(root_workspace_lba + SECTORS_PER_CLUSTER) * SECTOR_SIZE] = root_workspace_dir

    image[stage2_lba * SECTOR_SIZE:stage2_lba * SECTOR_SIZE + len(stage2_bytes)] = stage2_bytes
    image[kernel_lba * SECTOR_SIZE:kernel_lba * SECTOR_SIZE + len(kernel_bytes)] = kernel_bytes

    output_path.write_bytes(image)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
