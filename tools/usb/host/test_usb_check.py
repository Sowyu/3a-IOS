#!/usr/bin/env python3
"""Mock test of usb-check.py: canned ioreg, ifconfig, ping6, nc and ssh output, no phone, no network, no waiting.
  python3 -I host/test_usb_check.py
Proves the parsing and the pass/fail flow. The ioreg and ifconfig text is shaped like macOS 27's, typed by hand."""
import hashlib, importlib.util, json, pathlib, re, sys, tempfile

here = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('usbcheck', here / 'usb-check.py')
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
    def __init__(self, bcd=0x104, leaves_after=None, ll=True, ssh_ok=True, checksum_ok=True):
        self.bcd, self.leaves_after, self.ll, self.ssh_ok, self.checksum_ok = bcd, leaves_after, ll, ssh_ok, checksum_ok
        self.ioreg_calls, self.cmds = 0, []

    def run(self, argv, timeout, data=None):
        self.cmds.append(argv)
        assert 'sudo' not in argv and 'networksetup' not in argv, argv
        tool = argv[0]
        if tool == 'ioreg':
            self.ioreg_calls += 1
            if self.leaves_after is not None and self.ioreg_calls > self.leaves_after:
                return 0, IOREG.split('  +-o Sweet')[0]
            return 0, IOREG % self.bcd
        if tool == 'ifconfig':
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
expect('L3 never leaves', scenario(3, Phone(bcd=0x103)), 1, r'L3 FAIL soft-disconnect')
expect('L3 wrong level enumerated', scenario(3, Phone(bcd=0x104)), 1, r'L3 FAIL enumerate')
expect('L4 pass', scenario(4, Phone()), 0, r'^USBCHECK L4 PASS ssh root@fe80::5eff:fe77:ee02%en9 works')
expect('L4 no link-local on the Mac', scenario(4, Phone(ll=False)), 1, r'L4 FAIL interface: en9 has no IPv6')
expect('L4 key refused', scenario(4, Phone(ssh_ok=False)), 1, r'L4 FAIL ssh: rc 255')
expect('L4 checksum differs', scenario(4, Phone(checksum_ok=False)), 1, r'L4 FAIL 4 MiB checksum')
expect('L4 no gadget', scenario(4, Phone(bcd=0x999)), 1, r'L4 FAIL enumerate')
print('%s: %d failed' % ('FAIL' if fails else 'PASS', fails))
sys.exit(1 if fails else 0)
