#!/usr/bin/env python3
"""The combined verdict of one level run: the phone's kernel log (pulled by dev.py) and, for levels 3 and 4, the
Mac's usb-check.py result. PASS needs both halves; any failure line on either side is a FAIL. Only the phone log from
the last "ASTEROIDS USB L<n>: start" on counts, so lines from an earlier boot in the same pulled log cannot vouch for
this one. Use the usb-check file written for this same run.

  python3 -I host/usb-verdict.py LEVEL PHONE_LOG [USBCHECK_FILE]

Level 4 also fails when two status lines in a row show TX frozen with the queue full, ast145's signature after its
passing checksum (audit 4 finding 2), and a PASS names the stale, lost and ended TX counts of v5 kernels.

Prints one line, USBVERDICT L<n> PASS|FAIL <reason>, and exits 0 on PASS."""
import pathlib, re, sys

STATUS = re.compile(r'ASTEROIDS USB L4: ue0 rx (\d+) tx (\d+) frames; cfg (\d) alt (\d+) txq (\d+)(?:.*?stale (\d+), '
                    r'TX lost (\d+) ended (\d+))?')

NEED = {
    2: ['ASTEROIDS USB L2: start', 'CmdIOC proof: count 4', ': proven', 'ASTEROIDS USB L2: PASS owned'],
    3: ['ASTEROIDS USB L3: start', ': proven', 'ASTEROIDS USB L3: configured by the host',
        'ASTEROIDS USB STOP: level 3 hold time over; halted yes, transfers ended yes, event buffer ours (read back)',
        'stop verified', 'ASTEROIDS USB L3: PASS enumerated at speed'],
    4: ['ASTEROIDS USB L4: start', ': proven', 'ASTEROIDS USB L4: configured by the host',
        'SWEET USB: ue0 fe80::5eff:fe77:ee02 up', 'ASTEROIDS USB L4: up'],
}
BAD = ['ASTEROIDS USB GATE:', 'ASTEROIDS USB L%d: FAIL', 'stop NOT verified', 'did NOT halt', 'panic(cpu',
       'canaries HIT']


def verdict(level, log, check):
    start = log.rfind('ASTEROIDS USB L%d: start (sweetusb=' % level)
    if start < 0:
        return False, 'phone: no "ASTEROIDS USB L%d: start" in the log' % level
    log = log[log.rfind('\n', 0, start) + 1:]
    for want in NEED[level]:
        if want not in log:
            return False, 'phone: missing "%s"' % want
    for bad in BAD:
        bad = bad % level if '%d' in bad else bad
        if bad in log:
            line = next(l for l in log.splitlines() if bad in l)
            return False, 'phone: ' + line.strip()[:160]
    if level == 4 and 'ASTEROIDS USB STOP:' in log:
        return False, 'phone: level 4 stopped: ' + next(l for l in log.splitlines() if 'ASTEROIDS USB STOP:' in l)[:160]
    counts = ''
    if level == 4:
        st = [m for m in STATUS.finditer(log)]
        for a, b in zip(st, st[1:]):
            if a[2] == b[2] and int(a[5]) >= 31 and int(b[5]) >= 31:
                return False, 'phone: TX frozen at %s frames with the queue full (txq %s) while rx went %s -> %s' % (
                    b[2], b[5], a[1], b[1])
        if st and st[-1][6] is not None:
            counts = ' (%d status lines; stale %s, TX lost %s, TX ended %s)' % (len(st), st[-1][6], st[-1][7], st[-1][8])
    if level >= 3:
        last = (check or '').strip().splitlines()[-1:] or ['']
        if not re.match(r'USBCHECK L%d PASS' % level, last[0]):
            return False, 'mac: ' + (last[0] or 'no usb-check result')
    return True, ('phone and Mac agree' if level >= 3 else 'phone') + counts


if __name__ == '__main__':
    if len(sys.argv) not in (3, 4) or sys.argv[1] not in ('2', '3', '4'):
        sys.exit(__doc__)
    level = int(sys.argv[1])
    log = pathlib.Path(sys.argv[2]).read_text(errors='replace')
    check = pathlib.Path(sys.argv[3]).read_text() if len(sys.argv) == 4 else None
    ok, why = verdict(level, log, check)
    print('USBVERDICT L%d %s %s' % (level, 'PASS' if ok else 'FAIL', why))
    sys.exit(0 if ok else 1)
