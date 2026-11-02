#!/usr/bin/env python3
"""Builds the two kernel patches from the work copies, so they can be regenerated after an edit.
  work/a/bsd/dev/sweet_usb.c   the combined tree's file (SHA256 4cd078e8...), unchanged
  work/b/bsd/dev/              the finished sweet_usb.c and the new sweet_usb_gadget.h
  work/m/bsd/dev/sweet_usb.c   derived here: b without the asteroids levels, so 2160 = a -> m and 2161 = m -> b
usage: python3 -I host/make-patches.py   (writes work/m and the two .patch files, nothing else)"""
import pathlib, subprocess, sys
h = pathlib.Path(__file__).resolve().parent.parent
a = (h / 'work/a/bsd/dev/sweet_usb.c').read_text()
b = (h / 'work/b/bsd/dev/sweet_usb.c').read_text()
m = b
start = m.index('\n/*\n * Asteroids gadget, sweetusb=2..4')
end = m.index('#endif /* ARM64_BOARD_CONFIG_SWEET */\n', start)
m = m[:start] + '\n' + m[end:]
pairs = [
    ('static void ua_gadget(int arg);\n', ''),
    ('static void ua_if_inet6(void);\n', ''),
    ('''	if ((level & 0xf) < UG_LEVEL_PROBE || (level & 0xf) > UG_LEVEL_ECM || (level & ~(0xf | UG_FLAG_HS)) != 0) {
		printf("ASTEROIDS USB: sweetusb=%d refused; 1 read-only probe, 2 own, 3 enumerate, 4 ECM, +16 high speed\\n", level);
''', '''	if (level != 1) {
		printf("ASTEROIDS USB: sweetusb=%d refused; only 0=off, 1=read-only; SID 0x40 DMA unproven\\n", level);
'''),
    ('''	sweet_log_sync();
	if ((level & 0xf) >= UG_LEVEL_OWN) {
		ua_gadget(level);
	}
}
''', '''	sweet_log_sync();
}
'''),
    ('''	if (sweet_board_asteroids()) {
		ua_if_inet6();
	}
}
''', '''}
'''),
]
for old, new in pairs:
    if m.count(old) != 1:
        sys.exit('make-patches: expected one of: ' + old[:70])
    m = m.replace(old, new)
md = h / 'work/m/bsd/dev'
md.mkdir(parents=True, exist_ok=True)
(md / 'sweet_usb.c').write_text(m)
(md / 'sweet_usb_gadget.h').write_text((h / 'work/b/bsd/dev/sweet_usb_gadget.h').read_text())


def diff(old_root, new_root, names):
    out = ''
    for n in names:
        o, w = old_root / n, new_root / n
        r = subprocess.run(['diff', '-u', '--label', 'a/' + n, '--label', 'b/' + n,
                            str(o) if o.exists() else '/dev/null', str(w)], capture_output=True, text=True)
        if r.returncode > 1:
            sys.exit(r.stderr)
        if not o.exists():
            r_out = r.stdout.replace('--- a/' + n, '--- /dev/null', 1)
        else:
            r_out = r.stdout
        out += ''.join('diff --git a/%s b/%s\n' % (n, n) + (('new file mode 100644\n') if not o.exists() else '') + r_out) if r_out else ''
    return out


hdr2160 = '''From: asteroids USB gadget, part 1 of 2
Subject: [PATCH 2160] asteroids: USB gadget shared plumbing (no behaviour change on asteroids)

New bsd/dev/sweet_usb_gadget.h: the hardware-free gadget logic (DMA area layout, address gates, descriptors per level
and speed, the ep0 request table, frame and event arithmetic), host-tested by plans/usb/gadget/host/test_gadget.c.
sweet_usb.c: the DMA layout moves there and gives every TRB and small buffer its own 64-byte cache line; DWC3 register
accesses become one plain ldr/str each; ud_start_trb refuses a TRB or buffer outside the DMA area; RX length and TX
padding go through the tested helpers; the TX pad uses the bulk max packet. On asteroids none of this runs until
2161 and sweetusb>=2. The Poco F1 and sweet paths keep their behaviour apart from the new layout and the gate.

'''
hdr2161 = '''From: asteroids USB gadget, part 2 of 2
Subject: [PATCH 2161] asteroids: USB gadget levels 2 to 4 behind sweetusb, off by default

sweetusb=2 owns the halted DWC3 (event buffer moved off ABL's 0xD017B000 onto our own permanent, canaried DMA area,
ep0 commands, one CmdIOC event proving the DMA write), 3 soft-connects and enumerates with ep0 only and then
disconnects, 4 brings up CDC-ECM ue0 (192.168.7.2/24, fe80::5eff:fe77:ee02). +16 forces high speed. sweetusb unset,
0 or 1 behaves as before. See plans/usb/gadget/README.md.

'''
names = ['bsd/dev/sweet_usb.c', 'bsd/dev/sweet_usb_gadget.h']
p1 = diff(h / 'work/a', h / 'work/m', names)
p2 = diff(h / 'work/m', h / 'work/b', ['bsd/dev/sweet_usb.c'])
(h / '2160-asteroids-usb-gadget-shared.patch').write_text(hdr2160 + p1)
(h / '2161-asteroids-usb-gadget-levels.patch').write_text(hdr2161 + p2)
print('2160: %d lines, 2161: %d lines' % (p1.count('\n'), p2.count('\n')))
