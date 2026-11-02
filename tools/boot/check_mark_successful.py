#!/usr/bin/env python3
"""Host test for mark_successful.py and the ABL countdown model. Touches temp files only: run it as often as you like.

The real backup GPT has never been read (gpt-sde.bin holds LBA 0..5 only), so the backup copy here is synthesised
from the primary. A real run needs the real one, and plan() refuses it if it disagrees.
"""
import hashlib
import os
import struct
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mark_successful as m

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../..')
STOCK_PATH = os.path.join(ROOT, 'backup-asteroids/gpt-sde.bin')
checks = 0


def ok(cond, what):
	global checks
	checks += 1
	if not cond:
		sys.exit(f'FAIL: {what}')


def refuses(fn, needle):
	global checks
	checks += 1
	try:
		fn()
	except m.Refuse as e:
		if needle not in str(e):
			sys.exit(f'FAIL: refused for the wrong reason: {e!r}, expected {needle!r}')
		return
	sys.exit(f'FAIL: did not refuse, expected {needle!r}')


def abl_boot(byte):
	"""One normal boot on boot_a as FindBootableSlot does it (abl_a 0x18df8). Returns (new byte, what happens)."""
	if not byte & 0x80 and byte & 0x40:
		return byte, 'boots'				# 0x18ed4..0x18fe8: no decrement, no write
	retry = byte >> 3 & 7
	if not byte & 0x80 and retry:
		return byte & ~0x38 | (retry - 1) << 3, 'boots'	# 0x19434..0x19460
	return byte | 0x80, 'fastboot'				# 0x18a74, then boot_b fails the check at 0x18acc


def make_copies(stock, attr):
	"""Primary and synthetic backup dumps as the phone would have them with boot_a's byte at attr."""
	array = bytearray(stock[m.BLOCK * 2:m.BLOCK * 2 + m.ARRAY_BYTES])
	array[m.ATTR_OFFSET] = attr
	array = bytes(array)
	tail = stock[m.BLOCK * 2 + m.ARRAY_BYTES:m.BLOCK * 6]
	phdr = bytearray(stock[m.BLOCK:m.BLOCK * 2])
	struct.pack_into('<I', phdr, 88, m.crc(array))
	struct.pack_into('<I', phdr, 16, m.header_crc(bytes(phdr)))
	bhdr = bytearray(phdr)
	struct.pack_into('<QQ', bhdr, 24, m.LAST_LBA, 1)
	struct.pack_into('<Q', bhdr, 72, m.BACKUP_ARRAY_LBA)
	struct.pack_into('<I', bhdr, 16, m.header_crc(bytes(bhdr)))
	return bytes(phdr) + array + tail, array + tail + bytes(bhdr)


def make_image(path, primary, backup, stock):
	with open(path, 'wb') as f:
		f.truncate(m.LUN_BYTES)
		f.write(stock[:m.BLOCK])
		f.write(primary)
		f.seek(m.BACKUP_ARRAY_LBA * m.BLOCK)
		f.write(backup)


def xbl_view(path):
	"""What XBL would use: primary if valid, else the copy at the last LBA (xbl_a 0x14881afc). None = no table."""
	with open(path, 'rb') as f:
		for cur in (1, m.LAST_LBA):
			hdr, array = m.read_copy(f, cur)
			if m.copy_valid(hdr, array, cur):
				return array
	return None


def test_decode():
	ok(m.flags(0x77) == dict(priority=3, active=True, retry=6, successful=True, unbootable=False), 'stock boot_a')
	ok(m.flags(0xf3) == dict(priority=3, active=False, retry=6, successful=True, unbootable=True), 'stock boot_b')
	ok(m.flags(0x3f) == dict(priority=3, active=True, retry=7, successful=False, unbootable=False), 'after flash')
	ok(0x3f in m.ALLOWED and 0x07 not in m.ALLOWED and 0x77 not in m.ALLOWED, 'allowed set')


def test_countdown():
	byte, boots = 0x3f, 0
	for _ in range(20):
		byte, what = abl_boot(byte)
		if what == 'fastboot':
			break
		boots += 1
	ok(boots == 7 and byte == 0x87, f'countdown gave {boots} boots, end byte {byte:#x}')
	byte = 0x3f | m.SUCCESSFUL
	for _ in range(100):
		new, what = abl_boot(byte)
		ok(new == byte and what == 'boots', 'a successful slot never changes')


def test_plan_exact(stock, tmp):
	for attr, retry in sorted(m.ALLOWED.items()):
		primary, backup = make_copies(stock, attr)
		writes = m.plan(primary, backup, stock)
		ok([w[0] for w in writes] == [m.BACKUP_ARRAY_LBA, m.LAST_LBA, 2, 1], 'backup first, entries before header')
		img = os.path.join(tmp, 'img')
		make_image(img, primary, backup, stock)
		ok(m.apply_to_image(img, writes) is None, 'applies')
		with open(img, 'rb') as f:
			for cur in (1, m.LAST_LBA):
				hdr, array = m.read_copy(f, cur)
				ok(m.copy_valid(hdr, array, cur), 'copy valid after apply')
				ok(array[m.ATTR_OFFSET] == attr | m.SUCCESSFUL, 'attr byte set')
				ok(array[m.BOOT_B * m.ENTRY_SIZE + m.ATTR_TOP] == 0xf3, 'boot_b untouched')
				old = primary[m.BLOCK:m.BLOCK + m.ARRAY_BYTES]
				ok(sum(a != b for a, b in zip(old, array)) == 1, 'one array byte differs')
			f.seek(0)
			ok(f.read(m.BLOCK) == stock[:m.BLOCK], 'protective MBR untouched')
			f.seek(m.BLOCK * 3)
			ok(f.read(m.BLOCK * 3) == primary[m.BLOCK * 2:], 'other primary blocks untouched')
			f.seek(m.BLOCK * 6)
			ok(f.read(m.BLOCK) == bytes(m.BLOCK), 'nothing past the table written')


def test_interrupted(stock, tmp):
	primary, backup = make_copies(stock, 0x3f)
	writes = m.plan(primary, backup, stock)
	img = os.path.join(tmp, 'img')
	for n in range(len(writes)):
		for torn in (0, 512, 2048, 4095):
			make_image(img, primary, backup, stock)
			ok(m.apply_to_image(img, writes, crash_at=n, torn_bytes=torn) == n, 'crash reached')
			view = xbl_view(img)
			ok(view is not None, f'no valid GPT after crash at write {n}, {torn} bytes')
			ok(view[m.ATTR_OFFSET] in (0x3f, 0x7f), 'slot byte is the old or the new value')
			ok(view[m.BOOT_B * m.ENTRY_SIZE + m.ATTR_TOP] == 0xf3, 'boot_b intact after crash')
			ok(view[:m.ATTR_OFFSET] == primary[m.BLOCK:m.BLOCK + m.ATTR_OFFSET], 'tables intact after crash')


def test_flash_partition_order_is_worse(stock, tmp):
	"""Negative control: the order `fastboot flash partition:4` uses (upstream WriteGpt) has a no-valid-table window."""
	primary, backup = make_copies(stock, 0x3f)
	w = m.plan(primary, backup, stock)
	order = [w[3], w[1], w[2], w[0]]
	img = os.path.join(tmp, 'img')
	make_image(img, primary, backup, stock)
	windows = 0
	with open(img, 'r+b') as f:
		for lba, _, new in order:
			f.seek(lba * m.BLOCK)
			f.write(new)
			f.flush()
			windows += xbl_view(img) is None
	ok(windows > 0, 'the crash model must catch the both-invalid window')


def test_refusals(stock, tmp):
	primary, backup = make_copies(stock, 0x3f)
	plan = lambda p=primary, b=backup, s=stock: m.plan(p, b, s)

	def flipped(buf, off):
		x = bytearray(buf)
		x[off] ^= 1
		return bytes(x)

	refuses(lambda: plan(p=primary[:-1]), 'expected')
	refuses(lambda: plan(p=flipped(primary, 16)), 'header CRC')
	refuses(lambda: plan(p=flipped(primary, m.BLOCK + 3)), 'array CRC')
	refuses(lambda: plan(b=flipped(backup, 3)), 'array CRC')
	refuses(lambda: plan(b=make_copies(stock, 0x37)[1]), 'arrays differ')
	refuses(lambda: plan(p=primary, b=primary), 'not a GPT header')
	for attr, reason in ((0x77, 'stock, already successful'), (0x07, 'retry 0'), (0x87, 'exhausted'),
			     (0x3b, 'not active'), (0x3e, 'priority 2'), (0xbf, 'unbootable')):
		p, b = make_copies(stock, attr)
		refuses(lambda: m.plan(p, b, stock), 'counting-down state')

	# wrong LUN: sda's real table, and its GUID grafted into a table that otherwise looks right
	sda = open(os.path.join(ROOT, 'backup-asteroids/gpt-sda.bin'), 'rb').read()
	refuses(lambda: plan(p=sda[m.BLOCK:m.BLOCK * 6]), 'array CRC')
	bad_guid = bytearray(primary)
	bad_guid[56] ^= 1
	struct.pack_into('<I', bad_guid, 16, m.header_crc(bytes(bad_guid[:m.BLOCK])))
	bad_b = bytearray(backup)
	bad_b[-m.BLOCK + 56] ^= 1
	struct.pack_into('<I', bad_b, len(bad_b) - m.BLOCK + 16, m.header_crc(bytes(bad_b[-m.BLOCK:])))
	refuses(lambda: plan(p=bytes(bad_guid), b=bytes(bad_b)), 'stock LUN 4 GUID')

	# a valid table that differs from stock anywhere else
	other = bytearray(primary[m.BLOCK:m.BLOCK + m.ARRAY_BYTES])
	other[m.BOOT_B * m.ENTRY_SIZE + m.ATTR_TOP + 1] ^= 0x01		# a byte next to boot_b's attributes: not one the plan may differ in
	other = bytes(other)
	tail = primary[m.BLOCK + m.ARRAY_BYTES:]
	ph = m.rewrite_header(primary[:m.BLOCK], other)
	bh = m.rewrite_header(backup[-m.BLOCK:], other)
	refuses(lambda: plan(p=ph + other + tail, b=other + tail + bh), 'differ from stock')

	writes = plan()
	refuses(lambda: m.apply_to_image('/dev/null', writes), 'regular file')
	short = os.path.join(tmp, 'short')
	open(short, 'wb').close()
	refuses(lambda: m.apply_to_image(short, writes), 'regular file')
	refuses(lambda: m.apply_to_image(STOCK_PATH, writes), 'regular file')

	# the image changed between plan and write: nothing gets written
	img = os.path.join(tmp, 'img')
	make_image(img, primary, backup, stock)
	before = open(img, 'rb').read(m.BLOCK * 6)
	with open(img, 'r+b') as f:
		f.seek(m.BACKUP_ARRAY_LBA * m.BLOCK + 9)
		f.write(b'\xff')
	refuses(lambda: m.apply_to_image(img, writes), 'changed since the plan')
	ok(open(img, 'rb').read(m.BLOCK * 6) == before, 'primary untouched after a stale plan')


def main():
	paths = [os.path.join(ROOT, 'backup-asteroids', n) for n in ('gpt-sde.bin', 'gpt-sda.bin')]
	before = [hashlib.sha256(open(p, 'rb').read()).hexdigest() for p in paths]
	stock = open(STOCK_PATH, 'rb').read()
	ok(len(stock) == m.BLOCK * 6, 'gpt-sde.bin holds LBA 0..5')
	ok(stock[m.BLOCK * 2 + m.ATTR_OFFSET] == 0x77, 'stock boot_a byte')
	with tempfile.TemporaryDirectory() as tmp:
		test_decode()
		test_countdown()
		test_plan_exact(stock, tmp)
		test_interrupted(stock, tmp)
		test_flash_partition_order_is_worse(stock, tmp)
		test_refusals(stock, tmp)
	after = [hashlib.sha256(open(p, 'rb').read()).hexdigest() for p in paths]
	ok(before == after, 'partition backups unchanged')
	print(f'all {checks} checks passed; backups unchanged; backup GPT synthetic (real one never read)')


if __name__ == '__main__':
	main()
