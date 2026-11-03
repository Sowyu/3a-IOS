"""The configd preferences for the en2 proof, built from the root's original preferences.plist.

  python3 -I en2-prefs.py ORIGINAL OUT

ORIGINAL must be the root's file byte for byte (SHA-256 below); keep it, it is what goes back afterwards. OUT:
  - a new service for en2 (the cellular ifnet of patch 2220): IPv4 DHCP, IPv6 Automatic, first in the service order
  - the existing ue0 service kept for SSH but with no router and no DNS, so it can never be the primary route
Run with --check OUT to verify a built file. Nothing here writes to the phone."""
import hashlib, plistlib, sys

ORIGINAL_SHA256 = 'e8d0685f5b691028a7779bd2d481a537ed87574baeabd3799249efad3af4f368'  # the root's file, restored after ast163/164/167
SET = '5EE7C0DE-0000-4000-8000-0000000005E7'
UE0 = '5EE7C0DE-0000-4000-8000-0000000000E0'
EN2 = '5EE7C0DE-0000-4000-8000-0000000000E2'


def build(p):
    ue0 = p['NetworkServices'][UE0]
    assert ue0['Interface']['DeviceName'] == 'ue0'
    # ue0: an address for SSH, nothing else
    ue0['IPv4'] = {'ConfigMethod': 'Manual', 'Addresses': ['192.168.7.2'], 'SubnetMasks': ['255.255.255.0']}
    ue0['DNS'] = {}
    ue0['IPv6'] = {'ConfigMethod': 'LinkLocal'}
    p['NetworkServices'][EN2] = {
        'Interface': {'DeviceName': 'en2', 'Hardware': 'Ethernet', 'Type': 'Ethernet', 'UserDefinedName': 'Cellular (en2)'},
        'IPv4': {'ConfigMethod': 'DHCP'},
        'IPv6': {'ConfigMethod': 'Automatic'},
        'DNS': {},
        'Proxies': {'ExceptionsList': ['*.local', '169.254/16'], 'FTPPassive': 1},
        'UserDefinedName': 'Cellular (en2)',
    }
    net = p['Sets'][SET]['Network']
    net['Service'][EN2] = {'__LINK__': '/NetworkServices/' + EN2}
    net['Global']['IPv4']['ServiceOrder'] = [EN2, UE0]
    return p


def check(p):
    s = p['NetworkServices']
    assert s[EN2]['Interface']['DeviceName'] == 'en2' and s[EN2]['IPv4'] == {'ConfigMethod': 'DHCP'}
    assert 'Router' not in s[UE0]['IPv4'] and not s[UE0]['DNS'].get('ServerAddresses')
    assert s[UE0]['IPv6']['ConfigMethod'] == 'LinkLocal'
    net = p['Sets'][SET]['Network']
    assert net['Global']['IPv4']['ServiceOrder'] == [EN2, UE0] and set(net['Service']) == {EN2, UE0}
    assert p['CurrentSet'] == '/Sets/' + SET


def main():
    if sys.argv[1] == '--check':
        check(plistlib.loads(open(sys.argv[2], 'rb').read()))
        print('en2 prefs: check passed')
        return
    raw = open(sys.argv[1], 'rb').read()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != ORIGINAL_SHA256:
        sys.exit('refused: %s is not the original preferences.plist (sha256 %s)' % (sys.argv[1], digest))
    p = build(plistlib.loads(raw))
    check(p)
    out = plistlib.dumps(p, fmt=plistlib.FMT_BINARY)
    check(plistlib.loads(out))
    open(sys.argv[2], 'wb').write(out)
    print('en2 prefs: wrote %s, %d bytes, sha256 %s' % (sys.argv[2], len(out), hashlib.sha256(out).hexdigest()))


if __name__ == '__main__':
    main()
