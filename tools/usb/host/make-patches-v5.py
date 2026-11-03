#!/usr/bin/env python3
"""Builds the two v5 kernel patches from the work copies (make-patches.py made v1 from work/b, make-patches-v2.py v2, make-patches-v3.py v3, make-patches-v4.py v4).
  work/a/bsd/dev/              the combined tree's sweet_usb.c, sweet_ufs.c and sweet_reboot.c, unchanged
  work/b5/bsd/dev/             the v5 sweet_usb.c, sweet_usb_gadget.h, sweet_ufs.c and sweet_reboot.c
  work/m5/bsd/dev/sweet_usb.c  derived here: b5 without the asteroids levels, so 2160 v5 = a -> m5, 2161 v5 = m5 -> b5
usage: python3 -I host/make-patches-v5.py   (writes work/m5 and the two -v5.patch files, nothing else)"""
import pathlib, subprocess, sys
h = pathlib.Path(__file__).resolve().parent.parent
a = (h / 'work/a/bsd/dev/sweet_usb.c').read_text()
b = (h / 'work/b5/bsd/dev/sweet_usb.c').read_text()
m = b
start = m.index('\n/*\n * Asteroids gadget, sweetusb=2..4')
end = m.index('#endif /* ARM64_BOARD_CONFIG_SWEET */\n', start)
m = m[:start] + '\n' + m[end:]
pairs = [
    ('extern bool sweet_reboot_handsfree_ready(void);          /* sweet_reboot.c */\n', ''),
    ('''	if ((level & 0xf) >= UG_LEVEL_OWN && !sweet_reboot_handsfree_ready()) {
		printf("ASTEROIDS USB L%d: FAIL refused before any access, the probe included: needs sweetfbarm=1 armed, "
		    "sweetpanicfb=N and sweetdeadman=N running\\n", level & 0xf);
		sweet_log_sync();
		return;
	}
''', ''),
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
md = h / 'work/m5/bsd/dev'
md.mkdir(parents=True, exist_ok=True)
(md / 'sweet_usb.c').write_text(m)
(md / 'sweet_usb_gadget.h').write_text((h / 'work/b5/bsd/dev/sweet_usb_gadget.h').read_text())


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
Subject: [PATCH 2160 v5] asteroids: USB gadget shared plumbing (no behaviour change on asteroids)

New bsd/dev/sweet_usb_gadget.h: the hardware-free gadget logic (DMA area layout, address gates, descriptors per level
and speed, the ep0 request table, frame and event arithmetic), host-tested by plans/usb/gadget/host/test_gadget.c.
sweet_usb.c: the DMA layout moves there and gives every TRB and small buffer its own 64-byte cache line; DWC3 register
accesses become one plain ldr/str each; ud_start_trb refuses a TRB or buffer outside the DMA area; RX length and TX
padding go through the tested helpers; the TX pad uses the bulk max packet. v2: ud_start_trb returns its result, and
in strict mode (set only by 2161's asteroids levels) refuses a slot that is not the endpoint's own or still active,
and turns a failed STARTTRANSFER into a stop. v3: a data output admission gate, ud.tx_closed, checked under ud.lock
by ud_if_output and by ud_tx_kick_locked before any packet byte is copied, with the TX slot's ownership; false and
so inert on the Poco F1. The ep0 request parser checks the whole request shape and the Address state. v4: one device
identity at levels 3 and 4 (bDeviceClass 0, bcdDevice 1.01, the same strings), so a Mac's accessory approval, which
blocked level 3 in ast108, is given once. v5: ud_tx_kick_locked records when each TX starts, for 2161's progress
check. On asteroids none of this runs until 2161 and sweetusb>=2. The Poco F1 and sweet paths keep their behaviour apart from the new layout and the gate.

'''
hdr2161 = '''From: asteroids USB gadget, part 2 of 2
Subject: [PATCH 2161 v5] asteroids: USB gadget levels 2 to 4 behind sweetusb, off by default

sweetusb=2 owns the halted DWC3 (event buffer moved off ABL's 0xD017B000 onto our own permanent, canaried DMA area,
ep0 commands, one CmdIOC event proving the DMA write), 3 soft-connects and enumerates with ep0 only and then
disconnects, 4 brings up CDC-ECM ue0 (192.168.7.2/24, fe80::5eff:fe77:ee02). +16 forces high speed. sweetusb unset,
0 or 1 behaves as before. v2 answers analysis/usb-gadget-audit.md: the USB start moves after the recovery init
(sweet_ufs.c) and levels 2 to 4 refuse unless sweet_reboot_handsfree_ready() (sweet_reboot.c); admission refuses
anything but the Phase 0 halted state before any write; the DMA area exists before the first write; the CmdIOC proof
must be exact or the level stops; ENDTRANSFER uses the verified GUCTL2[14] polling mode and its failures stop the
gadget; one transfer per slot, stale completions ignored; SET_CONFIGURATION(0) and alternate 0 tear down; every
command failure stops; ua_stop either verifies the stop or panics into the armed fastboot reset; every first access to
an unread register group is announced in the log first. v3 answers analysis/usb-gadget-audit2.md: data output is
closed under ud.lock before any alt-0, unconfigure, reset or stop teardown and reopened under it only in a ready alt 1;
levels 2 to 4 refuse before the read-only probe too when recovery is not armed; a pending SETUP is left to the halt at
stop, as Linux does. v5 answers docs/usb-gadget-audit4.md findings 2 and 3: a TX completion is judged under ud.lock,
so one that arrives while network output is still inside STARTTRANSFER is no longer dropped as stale (ast145, TX
frozen with the queue full); a TX the controller holds for 2 s is settled with output closed, finished if its TRB is
back (HWO clear), else ended with a verified ENDTRANSFER and its frame dropped, before the slot is used again; queued
TX frames are dropped on every data teardown, so none reaches the next host; the plain-UDP live log is off unless
sweetusblog=1. See plans/usb/gadget/README.md.

'''
names = ['bsd/dev/sweet_usb.c', 'bsd/dev/sweet_usb_gadget.h']
p1 = diff(h / 'work/a', h / 'work/m5', names)
for n in ('sweet_ufs.c', 'sweet_reboot.c'):     # unchanged by 2160: m5 holds the originals
    (md / n).write_text((h / 'work/a/bsd/dev' / n).read_text())
p2 = diff(h / 'work/m5', h / 'work/b5', ['bsd/dev/sweet_usb.c', 'bsd/dev/sweet_ufs.c', 'bsd/dev/sweet_reboot.c'])
(h / '2160-asteroids-usb-gadget-shared-v5.patch').write_text(hdr2160 + p1)
(h / '2161-asteroids-usb-gadget-levels-v5.patch').write_text(hdr2161 + p2)
print('2160: %d lines, 2161: %d lines' % (p1.count('\n'), p2.count('\n')))
