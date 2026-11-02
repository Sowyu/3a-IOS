#!/usr/bin/env python3
"""Mac side of a USB gadget level run (README.md). No sudo, no network settings, no prompts. Start it before dev.py,
in the background; it waits for the phone's gadget, tests the level, writes OUT and exits.

  python3 -I host/usb-check.py LEVEL OUT [--wait SECONDS]      LEVEL 3 or 4, default wait 2400 s

Level 3: the USB device 1209:0001 with bcdDevice 0x0103 appears (ioreg), then leaves again (the phone's timed
soft-disconnect). Level 4: bcdDevice 0x0104; the CDC-ECM interface with the host MAC 02:00:5e:77:ee:01 gets its own
IPv6 link-local address from macOS; ping6 and ssh to the phone's fixed fe80::5eff:fe77:ee02 on that interface;
a 4 MiB checksum over ssh. Every command has a timeout. The last line of OUT is the verdict:
  USBCHECK L<n> PASS ...   or   USBCHECK L<n> FAIL <first failed step>
Exit code 0 pass, 1 fail.
"""
import argparse, hashlib, json, os, pathlib, re, subprocess, sys, time

VID, PID = 0x1209, 0x0001
HOST_MAC = '02:00:5e:77:ee:01'
PHONE_LL = 'fe80::5eff:fe77:ee02'
KEY = pathlib.Path.home() / '.ssh/id_ed25519'
SSH_OPTS = ['-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=no', '-o', 'UserKnownHostsFile=/dev/null',
            '-o', 'LogLevel=ERROR', '-o', 'ConnectTimeout=10', '-o', 'ServerAliveInterval=5', '-o', 'ServerAliveCountMax=2',
            '-o', 'IdentitiesOnly=yes', '-o', 'PreferredAuthentications=publickey', '-i', str(KEY)]


def run(argv, timeout, data=None):
    try:
        p = subprocess.run(argv, input=data, capture_output=True, timeout=timeout)
        return p.returncode, p.stdout.decode(errors='replace') + p.stderr.decode(errors='replace')
    except subprocess.TimeoutExpired:
        return 124, 'timeout after %d s' % timeout


def gadget():
    """The phone's USB device as macOS sees it, or None: bcdDevice, speed and product, from ioreg (no privileges)."""
    rc, out = run(['ioreg', '-p', 'IOUSB', '-l', '-w0'], 20)
    for block in re.split(r'\n(?=[ |]*\+-o )', out):
        if re.search(r'"idVendor" = %d\b' % VID, block) and re.search(r'"idProduct" = %d\b' % PID, block):
            get = lambda k: (re.search(r'"%s" = ([^\n]+)' % k, block) or [None, None])[1]
            return {'bcdDevice': int(get('bcdDevice') or -1), 'speed': get('USBSpeed') or get('Device Speed'),
                    'product': (get('USB Product Name') or get('kUSBProductString') or '').strip('"')}
    return None


def interface():
    """The ECM interface: (name, has an IPv6 link-local address, ifconfig text), by the host MAC."""
    rc, out = run(['ifconfig', '-a'], 10)
    for block in re.split(r'\n(?=\S)', out):
        if re.search(r'\bether %s\b' % HOST_MAC, block, re.I):
            name = block.split(':', 1)[0]
            return name, bool(re.search(r'inet6 fe80::', block)), block
    return None, False, ''


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('level', type=int, choices=(3, 4))
    ap.add_argument('out')
    ap.add_argument('--wait', type=int, default=2400, help='how long to wait for the gadget to appear')
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    res = {'level': a.level, 'start': time.strftime('%Y-%m-%d %H:%M:%S'), 'steps': []}

    def note(step, ok, detail=''):
        res['steps'].append({'t': round(time.monotonic() - t0, 1), 'step': step, 'ok': ok, 'detail': detail[-2000:]})
        out.write_text(json.dumps(res, indent=1) + '\n')

    def finish(ok, why):
        line = 'USBCHECK L%d %s %s' % (a.level, 'PASS' if ok else 'FAIL', why)
        res['verdict'] = line
        out.write_text(json.dumps(res, indent=1) + '\n' + line + '\n')
        print(line)
        sys.exit(0 if ok else 1)

    t0 = time.monotonic()
    want = 0x0100 + a.level
    dev = None
    while time.monotonic() - t0 < a.wait:
        dev = gadget()
        if dev and dev['bcdDevice'] == want:
            break
        time.sleep(2)
    if not dev or dev['bcdDevice'] != want:
        note('enumerate', False, repr(dev))
        finish(False, 'enumerate: no 1209:0001 with bcdDevice 0x%04x in %d s' % (want, a.wait))
    note('enumerate', True, json.dumps(dev))
    seen = time.monotonic()

    if a.level == 3:
        while time.monotonic() - seen < 300:
            if gadget() is None:
                note('soft-disconnect', True, 'gone %.0f s after it appeared' % (time.monotonic() - seen))
                finish(True, 'enumerated (%s), then left by itself after %.0f s' % (dev['speed'], time.monotonic() - seen))
            time.sleep(2)
        note('soft-disconnect', False, 'still present after 300 s')
        finish(False, 'soft-disconnect: still enumerated 300 s later')

    name, ll = None, False
    while time.monotonic() - seen < 90:
        name, ll, text = interface()
        if name and ll:
            break
        time.sleep(2)
    note('interface', bool(name and ll), text if name else 'no interface with ether ' + HOST_MAC)
    if not (name and ll):
        finish(False, 'interface: %s' % ('%s has no IPv6 link-local address' % name if name else 'no ECM interface'))
    target = '%s%%%s' % (PHONE_LL, name)
    rc, txt = run(['ping6', '-c', '3', '-i', '1', target], 15)
    note('ping6', rc == 0, txt)
    rc, txt = run(['nc', '-z', '-G', '5', '-w', '5', target, '22'], 12)
    note('port 22', rc == 0, txt)
    if not KEY.exists():
        finish(False, 'ssh: no key %s' % KEY)
    rc, txt = run(['ssh', *SSH_OPTS, 'root@' + target, 'uname -a; id; echo SSH-OK'], 30)
    note('ssh', rc == 0 and 'SSH-OK' in txt, txt)
    if not (rc == 0 and 'SSH-OK' in txt):
        finish(False, 'ssh: rc %d: %s' % (rc, txt.strip().splitlines()[-1] if txt.strip() else ''))
    blob = os.urandom(4 << 20)
    want_sum = hashlib.sha256(blob).hexdigest()
    rc, txt = run(['ssh', *SSH_OPTS, 'root@' + target, 'sha256sum 2>/dev/null || shasum -a 256'], 120, blob)
    got = (re.search(r'\b[0-9a-f]{64}\b', txt) or [None])[0]
    note('4 MiB checksum', got == want_sum, 'mac %s phone %s' % (want_sum, txt.strip()))
    if got != want_sum:
        finish(False, '4 MiB checksum: phone %s' % (got or txt.strip()[:200]))
    finish(True, 'ssh root@%s works, 4 MiB checksum matches, speed %s' % (target, dev['speed']))


if __name__ == '__main__':
    main()
