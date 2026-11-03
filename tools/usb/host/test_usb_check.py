#!/usr/bin/env python3
"""Mock test of usb-check.py: canned ioreg, ifconfig, ping6, nc and ssh output, no phone, no network, no waiting.
  python3 -I host/test_usb_check.py [--checker PATH]     default host/usb-check.py
Proves the parsing and the pass/fail flow, and host/usb-verdict.py's combination with the phone's log. The ioreg and
ifconfig text is shaped like macOS 27's, typed by hand (the IOUSBHostInterface keys are inferred, not seen here)."""
import hashlib, importlib.util, json, pathlib, re, sys, tempfile

here = pathlib.Path(__file__).resolve().parent
checker = pathlib.Path(sys.argv[sys.argv.index('--checker') + 1]) if '--checker' in sys.argv else here / 'usb-check.py'
spec = importlib.util.spec_from_file_location('usbcheck', checker)
uc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uc)

IOREG = '''+-o Root  <class IORegistryEntry, id 0x100000100, retain 46>
  +-o USB3.1 Hub@01100000  <class IOUSBHostDevice, id 0x100000a00, registered, matched, active, busy 0 (1 ms), retain 30>
  | {
  |   "idProduct" = 1
  |   "idVendor" = 1234
  |   "bcdDevice" = 260
  | }
  +-o Sweet iOS (USB Ethernet)@01200000  <class IOUSBHostDevice, id 0x100000b00, registered, matched, active, retain 32>
    {
      "USBSpeed" = 3
      "idProduct" = 1
      "USB Product Name" = "Sweet iOS (USB Ethernet)"
      "bcdDevice" = %d
      "idVendor" = 4617
    }
'''
IFACE = '''+-o IOUSBHostInterface@0  <class IOUSBHostInterface, id 0x100000c00, registered, matched, active, retain 9>
    {
      "bInterfaceClass" = 255
      "idProduct" = 1
      "bConfigurationValue" = 1
      "idVendor" = 4617
    }
'''
IFCONFIG = '''en0: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500
	ether c0:c7:db:2c:5d:45
	inet6 fe80::1c%%en0 prefixlen 64 secured scopeid 0xb
en9: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500
	options=6460<TSO4,TSO6,CHANNEL_IO,PARTIAL_CSUM,ZEROINVERT_CSUM>
	ether 02:00:5e:77:ee:01
	%s
	nd6 options=201<PERFORMNUD,DAD>
	status: active
'''


class Phone:
    """Plays the Mac's tools for one scenario."""
    def __init__(self, bcd=0x104, leaves_after=None, ll=True, ssh_ok=True, checksum_ok=True, configured_after=1,
                 blocked=False):
        self.bcd, self.leaves_after, self.ll, self.ssh_ok, self.checksum_ok = bcd, leaves_after, ll, ssh_ok, checksum_ok
        self.configured_after, self.blocked = configured_after, blocked
        self.ioreg_calls, self.iface_calls, self.cmds = 0, 0, []

    def run(self, argv, timeout, data=None):
        self.cmds.append(argv)
        assert 'sudo' not in argv and 'networksetup' not in argv, argv
        tool = argv[0]
        if tool == 'ioreg' and 'IOUSBHostInterface' in argv:
            self.iface_calls += 1
            gone = self.leaves_after is not None and self.ioreg_calls >= self.leaves_after
            ok = self.configured_after is not None and self.iface_calls >= self.configured_after and not gone
            return 0, IFACE if ok else ''
        if tool == 'ioreg':
            self.ioreg_calls += 1
            if self.leaves_after is not None and self.ioreg_calls > self.leaves_after:
                return 0, IOREG.split('  +-o Sweet')[0]
            return 0, IOREG % self.bcd
        if tool == 'log':
            assert argv[1] == 'show' and 'sudo' not in argv
            return 0, ('2026-11-03 01:48:18.250 Df kernel[0:11abb5a] (IOUSBHostFamily) usb-drd2-port-hs@02100000: '
                       'AppleUSBHostPort::enumerateDeviceComplete_block_invoke: device functionality blocked by '
                       'transport restrictions\n') if self.blocked else 'Timestamp               Ty Process[PID:TID]\n'
        if tool == 'ifconfig':
            if self.blocked:                    # never matched: no ECM driver, no interface
                return 0, IFCONFIG.split('en9:')[0]
            return 0, IFCONFIG % ('inet6 fe80::4b2:aa:bb:cc%en9 prefixlen 64 secured scopeid 0x14' if self.ll else '')
        if tool in ('ping6', 'nc'):
            assert argv[-1 if tool == 'ping6' else -2] == 'fe80::5eff:fe77:ee02%en9', argv
            return 0, 'ok'
        if tool == 'ssh':
            for o in ('StrictHostKeyChecking=no', 'UserKnownHostsFile=/dev/null', 'BatchMode=yes'):
                assert o in argv, o
            assert argv[argv.index('-i') + 1].endswith('id_ed25519')
            if not self.ssh_ok:
                return 255, 'root@fe80::5eff:fe77:ee02%en9: Permission denied (publickey).'
            if data is None:
                return 0, 'Darwin iPhone 25.0.0\nuid=0(root)\nSSH-OK\n'
            s = hashlib.sha256(data).hexdigest()
            return 0, (s if self.checksum_ok else '0' * 64) + '  -\n'
        raise AssertionError(argv)


def scenario(level, phone, wait=10):
    uc.run = phone.run
    clock = [0.0]
    uc.time.monotonic = lambda: clock[0]
    uc.time.sleep = lambda s: clock.__setitem__(0, clock[0] + s)
    uc.KEY = pathlib.Path(tempfile.gettempdir()) / 'id_ed25519'
    uc.KEY.write_text('mock key, never used\n')
    uc.SSH_OPTS[-1] = str(uc.KEY)
    out = pathlib.Path(tempfile.mkdtemp(prefix='usbcheck-')) / 'r.txt'
    sys.argv = ['usb-check.py', str(level), str(out), '--wait', str(wait)]
    try:
        uc.main()
        code = 0
    except SystemExit as e:
        code = e.code
    last = out.read_text().strip().splitlines()[-1]
    json.loads(out.read_text().rsplit('\n', 2)[0])          # the JSON part stays valid
    return code, last


fails = 0


def expect(name, got, code, pattern):
    global fails
    ok = got[0] == code and re.search(pattern, got[1])
    fails += not ok
    print('%-4s %-36s %s' % ('ok' if ok else 'FAIL', name, got[1]))


expect('L3 pass', scenario(3, Phone(bcd=0x103, leaves_after=5)), 0, r'^USBCHECK L3 PASS enumerated')
expect('L3 v4 identity (bcdDevice 0x0101) pass', scenario(3, Phone(bcd=0x101, leaves_after=5)), 0, r'^USBCHECK L3 PASS')
expect('L4 v4 identity pass', scenario(4, Phone(bcd=0x101)), 0, r'^USBCHECK L4 PASS')
expect('L3 blocked by accessory security (ast108)', scenario(3, Phone(bcd=0x103, leaves_after=60, configured_after=None,
       blocked=True)), 1, r'L3 FAIL BLOCKED by macOS accessory security')
expect('L4 blocked by accessory security', scenario(4, Phone(bcd=0x101, configured_after=None, blocked=True)), 1,
       r'L4 FAIL BLOCKED by macOS accessory security')
expect('L3 seen, never configured, then gone', scenario(3, Phone(bcd=0x103, leaves_after=60, configured_after=None)),
       1, r'L3 FAIL configured')
expect('L3 never leaves', scenario(3, Phone(bcd=0x103)), 1, r'L3 FAIL soft-disconnect')
expect('L3 wrong level enumerated', scenario(3, Phone(bcd=0x104)), 1, r'L3 FAIL enumerate')
expect('L4 pass', scenario(4, Phone()), 0, r'^USBCHECK L4 PASS ssh root@fe80::5eff:fe77:ee02%en9 works')
expect('L4 no link-local on the Mac', scenario(4, Phone(ll=False)), 1, r'L4 FAIL interface: en9 has no IPv6')
expect('L4 key refused', scenario(4, Phone(ssh_ok=False)), 1, r'L4 FAIL ssh: rc 255')
expect('L4 checksum differs', scenario(4, Phone(checksum_ok=False)), 1, r'L4 FAIL 4 MiB checksum')
expect('L4 no gadget', scenario(4, Phone(bcd=0x999)), 1, r'L4 FAIL enumerate')

# usb-verdict.py: both halves needed.
vspec = importlib.util.spec_from_file_location('usbverdict', here / 'usb-verdict.py')
uv = importlib.util.module_from_spec(vspec)
vspec.loader.exec_module(uv)
L3 = '''ASTEROIDS USB L3: start (sweetusb=3)
ASTEROIDS USB: CmdIOC proof: count 4, first word 0x020001c2, want 0x020001c2 under mask 0x0f00ffff: proven
ASTEROIDS USB L3: configured by the host at speed 0 after 9 s, 5 setups
ASTEROIDS USB STOP: level 3 hold time over; halted yes, transfers ended yes, event buffer ours (read back), events 40 setups 5, canaries intact: stop verified
ASTEROIDS USB L3: PASS enumerated at speed 0 and stopped (verified): 1 connects, 1 resets, 5 setups, 40 events, 0 stale
'''
HOST3 = 'USBCHECK L3 PASS enumerated (3), configured, then left by itself 20 s after configuration\n'
for name, level, log, check, ok, pat in [
    ('verdict L3 both halves', 3, L3, HOST3, True, r'phone and Mac agree'),
    ('verdict L3 host only, never configured', 3, L3.split('ASTEROIDS USB L3: configured')[0], HOST3, False,
     r'missing "ASTEROIDS USB L3: configured'),
    ('verdict L3 stop not verified', 3, L3.replace('stop verified', 'stop NOT verified'), HOST3, False, r'missing|NOT'),
    ('verdict L3 kernel fine, Mac failed', 3, L3, 'USBCHECK L3 FAIL soft-disconnect\n', False, r'mac: USBCHECK L3 FAIL'),
    ('verdict L3 a gate line', 3, L3 + 'ASTEROIDS USB GATE: canary\n', HOST3, False, r'GATE'),
    ('verdict L3 an old passing boot, then a failing one', 3,
     L3 + L3.split('ASTEROIDS USB L3: configured')[0] + 'ASTEROIDS USB L3: FAIL no SET_CONFIGURATION in 120 s\n',
     HOST3, False, r'missing|FAIL'),
    ('verdict L3 an old passing boot, then one that went quiet', 3,
     L3 + 'ASTEROIDS USB L3: start (sweetusb=3)\n', HOST3, False, r'missing'),
    ('verdict L3 an old failing boot, then a passing one', 3,
     'ASTEROIDS USB L3: start (sweetusb=3)\nASTEROIDS USB GATE: canary\n' + L3, HOST3, True, r'phone and Mac agree'),
]:
    got = uv.verdict(level, log, check)
    good = got[0] == ok and re.search(pat, got[1])
    fails += not good
    print('%-4s %-36s %s' % ('ok' if good else 'FAIL', name, got[1]))
# ast143: macOS named the phone's interface en8 with ether 02:00:00:00:00:00, so the one new interface counts too
NEWIF = IFCONFIG.replace('02:00:5e:77:ee:01', '02:00:00:00:00:00') % 'inet6 fe80::dd:1%en9 prefixlen 64 scopeid 0x1a'
for name, base, text, want in [
    ('interface new en9, other MAC', {'en0'}, NEWIF, 'en9'),
    ('interface en9 already there before', {'en0', 'en9'}, NEWIF, None),
    ('interface by HOST_MAC', {'en0', 'en9'}, IFCONFIG % '', 'en9'),
]:
    uc.run = lambda argv, timeout, data=None, t=text: (0, t)
    got = uc.interface(base)[0]
    fails += got != want
    print('%-4s %-36s %s' % ('ok' if got == want else 'FAIL', name, got))
print('%s: %d failed' % ('FAIL' if fails else 'PASS', fails))
sys.exit(1 if fails else 0)
