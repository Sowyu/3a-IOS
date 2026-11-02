#!/usr/bin/env python3
"""Mac side of a USB gadget level run (README.md). No sudo, no network settings, no prompts. Start it before dev.py,
in the background; it waits for the phone's gadget, tests the level, writes OUT and exits.

  python3 -I host/usb-check.py LEVEL OUT [--wait SECONDS]      LEVEL 3 or 4, default wait 2400 s

Level 3: the USB device 1209:0001 appears (ioreg), macOS configures it (its vendor interface, class 255, shows up as
an IOUSBHostInterface), and only then does it leave again (the phone's timed soft-disconnect). A device that leaves
without ever being configured fails. This is the host half: host/usb-verdict.py also needs the kernel's lines. Level 4: bcdDevice 0x0104; the CDC-ECM interface with the host MAC 02:00:5e:77:ee:01 gets its own
IPv6 link-local address from macOS; ping6 and ssh to the phone's fixed fe80::5eff:fe77:ee02 on that interface;
a 4 MiB checksum over ssh. Every command has a timeout. The last line of OUT is the verdict:
  USBCHECK L<n> PASS ...   or   USBCHECK L<n> FAIL <first failed step>
A device that macOS's accessory security keeps from its drivers (kernel log "blocked by transport restrictions",
ast108) ends as USBCHECK L<n> FAIL BLOCKED: approve the accessory at the unlocked Mac once, then rerun.
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


def configured(iclass=None):
    """True once macOS has configured the gadget: an IOUSBHostInterface of 1209:0001 exists (only after
    SET_CONFIGURATION), of bInterfaceClass iclass if given."""
    rc, out = run(['ioreg', '-r', '-c', 'IOUSBHostInterface', '-l', '-w0'], 20)
    for block in re.split(r'\n(?=[ |]*\+-o )', out):
        if re.search(r'"idVendor" = %d\b' % VID, block) and re.search(r'"idProduct" = %d\b' % PID, block) and \
                (iclass is None or re.search(r'"bInterfaceClass" = %d\b' % iclass, block)):
            return True
    return False


def blocked(since):
    """The kernel line macOS logs when accessory security keeps a device from its drivers (ast108, 01:48:18).
    `log show` needs no privileges for kernel messages."""
    rc, out = run(['log', 'show', '--style', 'compact', '--start', since, '--predicate',
                   'process == "kernel" AND eventMessage CONTAINS "blocked by transport restrictions"'], 60)
    lines = [l for l in out.splitlines() if 'blocked by transport restrictions' in l]
    return lines[-1].strip() if lines else None


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
    since = time.strftime('%Y-%m-%d %H:%M:%S')
    want = (0x0101, 0x0100 + a.level)   # v4 kernels: one identity; v2 and v3 kernels: 0x0100 + level
    dev = None
    while time.monotonic() - t0 < a.wait:
        dev = gadget()
        if dev and dev['bcdDevice'] in want:
            break
        time.sleep(2)
    if not dev or dev['bcdDevice'] not in want:
        note('enumerate', False, repr(dev))
        finish(False, 'enumerate: no 1209:0001 with bcdDevice %s in %d s' % (' or '.join('0x%04x' % w for w in want), a.wait))
    note('enumerate', True, json.dumps(dev))
    seen = time.monotonic()

    def check_blocked(step):
        line = blocked(since)
        if line:
            note(step, False, line)
            finish(False, 'BLOCKED by macOS accessory security (the device was never matched to a driver): approve '
                   'the accessory at the unlocked Mac once, or allow accessories to connect, then rerun')

    if a.level == 3:
        cfg = None
        while time.monotonic() - seen < 300:
            if cfg is None and time.monotonic() - seen >= 15 and int(time.monotonic() - seen) % 15 < 1:
                check_blocked('configured')
            if cfg is None and configured(255):
                cfg = time.monotonic()
                note('configured', True, 'interface present %.0f s after the device appeared' % (cfg - seen))
            if gadget() is None:
                gone = time.monotonic()
                if cfg is None:
                    check_blocked('configured')
                    note('soft-disconnect', False, 'left %.0f s after it appeared, never configured' % (gone - seen))
                    finish(False, 'configured: the device left without macOS ever configuring it')
                note('soft-disconnect', True, 'gone %.0f s after configuration' % (gone - cfg))
                finish(True, 'enumerated (%s), configured, then left by itself %.0f s after configuration'
                       % (dev['speed'], gone - cfg))
            time.sleep(1)
        note('soft-disconnect', False, 'still present after 300 s')
        finish(False, 'soft-disconnect: still enumerated 300 s later')

    name, ll = None, False
    while time.monotonic() - seen < 90:
        name, ll, text = interface()
        if name and ll:
            break
        time.sleep(2)
    if not name:
        check_blocked('interface')
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
