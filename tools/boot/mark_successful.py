#!/usr/bin/env python3
"""NOT FOR A PHONE. Compute and test the one-byte write that marks boot_a successful (analysis/bootslot.md).

There is deliberately no adb or fastboot code here. `apply_to_image` refuses anything but a 4 GiB regular file, so
the only thing this can write is a test image. Reaching a phone needs a wrapper nobody has written.

Usage: mark_successful.py check PRIMARY.bin BACKUP.bin [STOCK_GPT]
    PRIMARY.bin  LUN 4 LBA 1..5, BACKUP.bin  LUN 4 LBA 0xffffb..0xfffff, both read from the phone
    Prints the four planned writes or the reason it refuses. Writes nothing.
"""
import os
import stat
import struct
import sys
import zlib

BLOCK = 4096
LAST_LBA = 0xfffff			# gpt-sde.bin: AlternateLBA of LUN 4
LUN_BYTES = (LAST_LBA + 1) * BLOCK
DUMP_BLOCKS = 5				# 1 header + 4 entry blocks, the span ABL reads (UpdatePartitionAttributes)
ENTRIES, ENTRY_SIZE = 96, 128
ARRAY_BYTES = ENTRIES * ENTRY_SIZE
PRIMARY_ARRAY_LBA = 2
BACKUP_ARRAY_LBA = LAST_LBA - 4
BOOT_A, BOOT_B = 10, 39			# entry indexes in gpt-sde.bin
ATTR_TOP = 54				# byte 6 of the attributes field, which holds bits 48..55
ATTR_OFFSET = BOOT_A * ENTRY_SIZE + ATTR_TOP
SUCCESSFUL = 0x40

# priority 3, active, not successful, bootable, retry 7..1. Retry 0 and any unbootable state are refused: reflash those.
ALLOWED = {0x07 | retry << 3: retry for retry in range(1, 8)}


class Refuse(Exception):
	pass


def crc(b):
	return zlib.crc32(b) & 0xffffffff


def flags(byte):
	return dict(priority=byte & 3, active=bool(byte & 4), retry=byte >> 3 & 7,
		    successful=bool(byte & 0x40), unbootable=bool(byte & 0x80))


def parse_header(block):
	f = struct.unpack_from('<8sIIIIQQQQ16sQIII', block)
	return dict(zip(('sig', 'rev', 'size', 'crc', 'rsvd', 'cur', 'alt', 'first', 'last', 'guid',
			 'entries', 'count', 'entsz', 'array_crc'), f))


def header_crc(block):
	z = bytearray(block[:92])
	z[16:20] = bytes(4)
	return crc(bytes(z))


def copy_valid(header_block, array, lba):
	"""The checks XBL makes on a copy: signature, header CRC, its own LBA, array CRC (xbl_a 0x14881cc0)."""
	h = parse_header(header_block)
	return (h['sig'] == b'EFI PART' and h['size'] == 92 and header_crc(header_block) == h['crc']
		and h['cur'] == lba and h['entsz'] == ENTRY_SIZE and h['count'] == ENTRIES
		and crc(array) == h['array_crc'])


def check_copy(name, dump, cur, alt, entries_lba):
	if len(dump) != DUMP_BLOCKS * BLOCK:
		raise Refuse(f'{name}: expected {DUMP_BLOCKS * BLOCK} bytes, got {len(dump)}')
	hblock = dump[:BLOCK] if cur == 1 else dump[-BLOCK:]
	array = dump[BLOCK:BLOCK + ARRAY_BYTES] if cur == 1 else dump[:ARRAY_BYTES]
	h = parse_header(hblock)
	if h['sig'] != b'EFI PART' or h['size'] != 92 or h['rev'] != 0x10000:
		raise Refuse(f'{name}: not a GPT header')
	if header_crc(hblock) != h['crc']:
		raise Refuse(f'{name}: header CRC mismatch')
	if crc(array) != h['array_crc']:
		raise Refuse(f'{name}: entry array CRC mismatch')
	want = dict(cur=cur, alt=alt, entries=entries_lba, count=ENTRIES, entsz=ENTRY_SIZE, last=LAST_LBA - 5)
	for k, v in want.items():
		if h[k] != v:
			raise Refuse(f'{name}: header field {k} is {h[k]:#x}, expected {v:#x}')
	return hblock, array, h


def entry_name(array, index):
	e = array[index * ENTRY_SIZE:(index + 1) * ENTRY_SIZE]
	return e[56:128].decode('utf-16le', 'replace').split('\0')[0]


def rewrite_header(hblock, new_array):
	new = bytearray(hblock)
	struct.pack_into('<I', new, 88, crc(new_array))
	struct.pack_into('<I', new, 16, header_crc(bytes(new)))
	changed = [i for i in range(BLOCK) if new[i] != hblock[i]]
	if not set(changed) <= set(range(16, 20)) | set(range(88, 92)):
		raise Refuse('internal: header change outside the two CRC fields')
	return bytes(new)


def plan(primary_dump, backup_dump, stock_gpt):
	"""Validate everything and return [(lba, old_block, new_block)] in write order, or raise Refuse."""
	phdr, parray, ph = check_copy('primary', primary_dump, 1, LAST_LBA, PRIMARY_ARRAY_LBA)
	bhdr, barray, bh = check_copy('backup', backup_dump, LAST_LBA, 1, BACKUP_ARRAY_LBA)
	if parray != barray:
		raise Refuse('primary and backup entry arrays differ')
	if ph['guid'] != bh['guid']:
		raise Refuse('primary and backup disk GUIDs differ')

	stock_hdr, stock_array, stock_h = check_copy('stock', stock_gpt[BLOCK:BLOCK * 6], 1, LAST_LBA, PRIMARY_ARRAY_LBA)
	if ph['guid'] != stock_h['guid']:
		raise Refuse('disk GUID is not the stock LUN 4 GUID: wrong LUN or changed table')
	diff = [i for i in range(ARRAY_BYTES) if parray[i] != stock_array[i]]
	# boot_b's attribute byte may differ too: iOS is flashed there as the fallback slot. It is checked, never written.
	B_ATTR = BOOT_B * ENTRY_SIZE + ATTR_TOP
	if not set(diff) <= {ATTR_OFFSET, B_ATTR}:
		raise Refuse(f'entries differ from stock LUN 4 at {[hex(i) for i in diff[:8]]}; only {ATTR_OFFSET:#x} and {B_ATTR:#x} may')
	if entry_name(parray, BOOT_A) != 'boot_a' or entry_name(parray, BOOT_B) != 'boot_b':
		raise Refuse('boot_a or boot_b is not at the expected entry index')

	byte = parray[ATTR_OFFSET]
	if byte not in ALLOWED:
		raise Refuse(f'boot_a attribute byte {byte:#04x} is not a counting-down state: {flags(byte)}')

	narray = bytearray(parray)
	narray[ATTR_OFFSET] = byte | SUCCESSFUL
	narray = bytes(narray)

	# entries first, then the header that describes them, backup before primary. Each of the four is one block.
	p_blocks, b_blocks = primary_dump[BLOCK:BLOCK * 2], backup_dump[:BLOCK]
	np_block = narray[:BLOCK]
	return [
		(BACKUP_ARRAY_LBA, b_blocks, np_block),
		(LAST_LBA, bhdr, rewrite_header(bhdr, narray)),
		(PRIMARY_ARRAY_LBA, p_blocks, np_block),
		(1, phdr, rewrite_header(phdr, narray)),
	]


def read_copy(f, cur):
	"""(header block, entry array) of one copy in an image."""
	f.seek((1 if cur == 1 else LAST_LBA) * BLOCK)
	hblock = f.read(BLOCK)
	f.seek((PRIMARY_ARRAY_LBA if cur == 1 else BACKUP_ARRAY_LBA) * BLOCK)
	return hblock, f.read(ARRAY_BYTES)


def apply_to_image(path, writes, crash_at=None, torn_bytes=0):
	"""Apply a plan to a test image with read-back. crash_at/torn_bytes simulate power loss at write number crash_at."""
	st = os.stat(path)
	if not stat.S_ISREG(st.st_mode) or st.st_size != LUN_BYTES:
		raise Refuse(f'{path}: not a {LUN_BYTES} byte regular file; this tool only writes test images')
	if 'backup-asteroids' in os.path.realpath(path):
		raise Refuse(f'{path}: refusing to touch the partition backups')
	with open(path, 'r+b') as f:
		for n, (lba, old, new) in enumerate(writes):
			f.seek(lba * BLOCK)
			if f.read(BLOCK) != old:
				raise Refuse(f'write {n}: LBA {lba:#x} changed since the plan')
			f.seek(lba * BLOCK)
			if n == crash_at:
				f.write(new[:torn_bytes])
				f.flush()
				return n
			f.write(new)
			f.flush()
			os.fsync(f.fileno())
			f.seek(lba * BLOCK)
			if f.read(BLOCK) != new:
				raise Refuse(f'write {n}: read-back of LBA {lba:#x} differs')
			if n == 1 and not copy_valid(*read_copy(f, LAST_LBA), LAST_LBA):
				raise Refuse('backup copy is not valid after writing it: primary left untouched')
		if not copy_valid(*read_copy(f, 1), 1):
			raise Refuse('primary copy is not valid after writing it')
	return None


def main(argv):
	if len(argv) not in (4, 5) or argv[1] != 'check':
		sys.exit(__doc__)
	here = os.path.dirname(os.path.abspath(__file__))
	stock_path = argv[4] if len(argv) == 5 else os.path.join(here, '../../backup-asteroids/gpt-sde.bin')
	primary, backup, stock = (open(p, 'rb').read() for p in (argv[2], argv[3], stock_path))
	try:
		writes = plan(primary, backup, stock)
	except Refuse as e:
		sys.exit(f'REFUSED: {e}')
	array_byte = primary[BLOCK + ATTR_OFFSET]
	print(f'boot_a attribute byte {array_byte:#04x} -> {array_byte | SUCCESSFUL:#04x} {flags(array_byte | SUCCESSFUL)}')
	for lba, old, new in writes:
		d = [i for i in range(BLOCK) if old[i] != new[i]]
		print(f'LBA {lba:#07x}: {len(d)} bytes change at {[hex(i) for i in d]}')


if __name__ == '__main__':
	main(sys.argv)
