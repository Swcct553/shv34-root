#!/usr/bin/env python3
"""Build the first ~1 MiB of an SD card for SHV34 SBL1 SD_LOADER mode.

SBL1 loader (xbl.bin PH13 @0x85e98538) does:
  - read 34 sectors (MBR + GPT header + partition entries) to 0xD0000000
  - verify "EFI PART", parse GPT, find a partition NAMED "sdl" (UTF-16LE)
  - read that whole partition to 0xD0000000
  - memcmp(buf, "SDLOADER_IMG", 12)
  - checksum(buf[0:0x1fc])   == *(u32*)(buf+0x1fc)
  - checksum(buf[0x200:N*512]) == *(u32*)(buf+0x30)
  - copy ELF to 0xC0400000 and jump to entry 0xC0402000

Output: bl-research/sdcard_head.bin  (LBA 0 .. 34+1023)
The rest of the card is untouched. This is fully reversible: reformat the card.
"""
import struct, zlib, os

CARD_BYTES = 63864569856          # 64 GB card (VID:03 SD64G), from Get-Disk
SECTOR = 512
NSECT = CARD_BYTES // SECTOR
PART_START = 34
PART_SECTORS = 1024               # p23 sdl partition is 1024 sectors
IMG = 'bl-research/sdl_23.bin'
OUT = 'bl-research/sdcard_head.bin'

img = open(IMG, 'rb').read()
assert len(img) == PART_SECTORS * SECTOR, len(img)
assert img[:12] == b'SDLOADER_IMG'

# ---- validate checksums (proves format understanding) ----
h1 = struct.unpack_from('<I', img, 0x1fc)[0]
h2 = struct.unpack_from('<I', img, 0x30)[0]
print('checksum stored  [0x1fc]=%#x  computed=%#x' % (h1, sum(img[:0x1fc]) & 0xffffffff))
print('checksum stored  [0x30] =%#x  computed=%#x' % (h2, sum(img[0x200:]) & 0xffffffff))

# ---- protective MBR (LBA 0) ----
mbr = bytearray(SECTOR)
mbr[0x1fe:0x200] = b'\x55\xaa'
mbr[0x1c2:0x1c6] = bytes([0xee, 0xff, 0xff, 0xff])   # GPT protective, start 1
mbr[0x1c6:0x1ca] = struct.pack('<I', 1)
mbr[0x1ca:0x1ce] = struct.pack('<I', NSECT - 1)

# ---- GPT partition entry ----
def guid(b):
    return b

ent = bytearray(128)
ent[0:16] = bytes(range(0x10))            # arbitrary type GUID
ent[16:32] = bytes(range(0x10, 0x20))      # unique GUID
ent[32:40] = struct.pack('<Q', PART_START)
ent[40:48] = struct.pack('<Q', PART_START + PART_SECTORS - 1)
name = 'sdl'.encode('utf-16-le') + b'\x00\x00'
ent[56:56+len(name)] = name
entries = bytes(ent) + bytes(128 * 127)
part_crc = zlib.crc32(entries) & 0xffffffff

# ---- GPT header (LBA 1) ----
hdr = bytearray(SECTOR)
hdr[0:8] = b'EFI PART'
hdr[8:12] = struct.pack('<I', 0x00010000)
hdr[12:16] = struct.pack('<I', 92)
hdr[24:32] = struct.pack('<Q', 1)                 # current LBA
hdr[32:40] = struct.pack('<Q', NSECT - 1)        # backup LBA
hdr[40:48] = struct.pack('<Q', PART_START)        # first usable
hdr[48:56] = struct.pack('<Q', NSECT - 34)       # last usable
hdr[56:72] = bytes(range(0x20, 0x30))     # disk GUID
hdr[72:80] = struct.pack('<Q', 2)                 # partition entries LBA
hdr[80:84] = struct.pack('<I', 128)               # number of entries
hdr[84:88] = struct.pack('<I', 128)               # size of entry
hdr[88:92] = struct.pack('<I', part_crc)
hdr[16:20] = struct.pack('<I', zlib.crc32(bytes(hdr[:92])) & 0xffffffff)

head = bytes(mbr) + bytes(hdr) + entries + img
assert len(head) == (34 + PART_SECTORS) * SECTOR
open(OUT, 'wb').write(head)
print('wrote', OUT, len(head), 'bytes =', len(head)//SECTOR, 'sectors')
print('GPT partition "sdl": LBA %d..%d' % (PART_START, PART_START+PART_SECTORS-1))
print('GPT partition-array CRC = %#x, header CRC = %#x' % (part_crc, struct.unpack_from('<I', hdr, 16)[0]))
