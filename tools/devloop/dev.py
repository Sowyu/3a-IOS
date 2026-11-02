#!/usr/bin/env python3
"""One test cycle with nobody at the phone: wait for fastboot or recovery -> replace root files -> fastboot boot the test
image -> the phone runs its script and returns to fastboot -> recovery -> pull the log and files -> print a summary.

  dev.py next-loop-boot.img [ROOTPATH=FILE ...] --note tiles [--test ui] [--pull ROOTPATH ...] [--key REGEX ...] [--ref DIR]
  dev.py --dry-run ...       prints each command and plays a phone: runs no adb or fastboot
  dev.py --no-push ...       kernel-only change, the script already on the root is fine
  dev.py --print-args IMG    the boot-args inside a boot image
  dev.py --print-script NAME the self-test as a standalone script, for the root patch route

The image must carry sweetfbarm=1, sweetdeadman=N and sweetselftest=N, or it is refused: without them a reset can reach
Android, which formats the root, and a hang costs a button. Exit code: 0 the self-test finished, 1 it ran and did not,
2 stopped with a message. The phone ends in recovery, or in fastboot if the stop came before recovery. See analysis/devloop.md.
"""
import argparse, collections, json, os, pathlib, re, shlex, signal, statistics, subprocess, sys, tempfile, time, zlib
sys.dont_write_bytecode = True

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / 'plans/usb/host'))
import handsfree, framecmp       # noqa: E402  wait_for, classify, frame compare

REPO = ROOT / 'ios-asteroids'
PUT = HERE / '06-hfs-put.py'
HFS_PULL = REPO / 'tools/sweet-hfs-pull.py'
PY = ['python3', '-I']
LOG_PULL = REPO / 'tools/sweet-log-pull.sh'
RECOVERY = ROOT / 'recovery/ramboot/orangefox-ram.img'
HOOKED = '/usr/local/libexec/renegade-firstboot.sh'
FRAMES = '/private/var/selftest/frames'
PULLS = ['/private/var/tmp/bgl-backboardd.log', '/private/var/tmp/bgmtl.log', '/private/var/tmp/sweetrun.log',
         '/private/var/log/renegade-firstboot.log', '/private/var/tmp/sweetappcheck.log', '/private/var/tmp/sweetsb.log']
BATTERY = 'cat /sys/class/power_supply/battery/capacity /sys/class/power_supply/battery/status'
MARK_A, MARK_B = '# >>> sweetselftest\n', '# <<< sweetselftest\n'
LOW_BATTERY = 15


def command_allowed(argv):
    """The only way out to the phone: the exact command shapes this loop needs. Plain `fastboot reboot` is refused
    (it boots Android, and Android formats userdata); `reboot recovery` is exact, as /tmp/pull-log.sh has always used it, and so are flash, erase, format, oem, set_active and *.simg."""
    if not argv or any('.simg' in a for a in argv):
        return False
    if argv[:2] == ['python3', '-I'] and len(argv) > 2:         # the Python tools; the two have no other way in
        if pathlib.Path(argv[2]).name not in ('sweet-hfs-pull.py', '06-hfs-put.py'):
            return False
        argv = argv[2:]
    tool, rest = pathlib.Path(argv[0]).name, argv[1:]
    if rest[:1] == ['-s'] and len(rest) >= 2:
        rest = rest[2:]
    if tool == 'fastboot':
        return rest in (['devices'], ['reboot', 'recovery']) or (len(rest) == 2 and rest[0] == 'boot' and rest[1].endswith('.img'))
    if tool == 'adb':
        return rest in (['devices'], ['get-state'], ['reboot', 'bootloader'], ['root'], ['kill-server'], ['start-server'],
                        ['shell', BATTERY])
    if tool in ('sweet-log-pull.sh', 'sweet-hfs-pull.py'):      # both only read userdata (see their headers)
        return True
    if tool == '06-hfs-put.py':                                 # its dirty checks stand; undo stays a person's decision
        return rest[:1] == ['--adb'] and '--undo' not in rest and '--dev' not in rest
    return False


class Runner:
    """Every command has a timeout and dies with its children, so no step can hang the loop."""
    sleep, now = staticmethod(time.sleep), staticmethod(time.monotonic)

    def run(self, argv, timeout=120):
        if not command_allowed(argv):
            raise PermissionError('refused: ' + shlex.join(argv))
        # adb and fastboot chatter on stderr ("daemon not running"), which must not read as a device; the tools report there
        err = subprocess.DEVNULL if argv[0] in ('adb', 'fastboot') else subprocess.STDOUT
        p = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=err, text=True, errors='replace', start_new_session=True)
        try:
            out = p.communicate(timeout=timeout)[0]
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            p.communicate()
            return 124, ''
        return p.returncode, out


class Stop(Exception):
    pass


def bootargs_of(path):
    """The cmdline of a shim boot image: gzip of Image + mkpayload payload at the first page, header at SWEETXNU."""
    data = pathlib.Path(path).read_bytes()
    raw = zlib.decompressobj(31).decompress(data[4096:])
    # the shim's own code holds the magic too: the payload is the match with version 1 behind it
    at = next((m.start() for m in re.finditer(b'SWEETXNU', raw) if raw[m.start() + 8:m.start() + 12] == b'\1\0\0\0'), -1)
    if at < 0:
        sys.exit('%s has no payload: not a shim boot image' % path)
    return raw[at + 0x48:at + 0x48 + 608].split(b'\0')[0].decode(errors='replace')


def check_loop_args(args):
    """Why this image cannot run in the loop, or None. Returns the deadman too, for the deadline."""
    words = args.split()
    arg = {w.split('=', 1)[0]: w.split('=', 1)[1] for w in words if '=' in w}
    if arg.get('sweetfbarm') != '1':
        return 'no sweetfbarm=1: an uncontrolled reset could boot Android, which formats the root', 0
    if not arg.get('sweetdeadman', '').isdigit() or not 30 <= int(arg['sweetdeadman']) <= 86400:
        return 'no sweetdeadman=N (30..86400): a stuck boot would need a button', 0
    if arg.get('sweetselftest', '0') == '0':
        return 'no sweetselftest=N: the self-test would not run', 0
    return None, int(arg['sweetdeadman'])


def next_label(logs):
    nums = [int(m[1]) for p in pathlib.Path(logs).glob('ast*') if (m := re.match(r'ast(\d+)', p.name))]
    return 'ast%d' % (max(nums, default=0) + 1)


def compose_script(body, base=''):
    """hook + test body, on top of the first-boot script with any earlier hook taken out."""
    hook = (HERE / 'selftest/hook.sh').read_text().replace('\t# @BODY@\n', ''.join('\t' + l + '\n' if l else '\n' for l in body.splitlines()))
    if MARK_A in base:
        base = base[:base.index(MARK_A)] + base[base.index(MARK_B) + len(MARK_B):]
    base = base[len('#!/bin/sh\n'):] if base.startswith('#!/bin/sh\n') else base
    return '#!/bin/sh\n' + MARK_A + hook + MARK_B + base


STAT = re.compile(r'up_s=(\d+) .*?swaps=(\d+) swaps_s=([\d.]+)')
FLIPS = re.compile(r'flips done=(\d+) latched=\d+ repeated=\d+ dropped=(\d+) hold_expired=(\d+)')
DEFAULT_KEYS = (r'panic\(cpu', r'SELFTEST: (FAIL|SKIP|DONE|springboard)', r'deadman:', r'sweetfbarm:', r'0x7148 = ',
                r'Initializing the journal', r'replay_journal', r'hfs_mark_inconsistent', r'mounting dirty', r'submit refused')


def rate(stats):
    """frames/s over stats lines (up_s, swaps, swaps_s), and the median of the seconds that drew anything."""
    if len(stats) < 2 or stats[-1][0] <= stats[0][0]:
        return None, None
    busy = [s[2] for s in stats if s[2] > 1]
    return round((stats[-1][1] - stats[0][1]) / (stats[-1][0] - stats[0][0]), 1), round(statistics.median(busy), 1) if busy else 0


def last_boot(text):
    at = text.rfind('SWEET REBOOT: asteroids: 0x7148 = ')
    return text[text.rfind('\n', 0, at) + 1:] if at >= 0 else text


def summarize(text, keys=()):
    """The numbers of the last boot in a console log. Absent lines give None, never a guessed zero."""
    boot = last_boot(text)
    res = {'lines': boot.count('\n')}
    stats, steps, cur = [], collections.OrderedDict(), None
    for line in boot.splitlines():
        m = STAT.search(line)
        if m:
            s = (int(m[1]), int(m[2]), float(m[3]))
            stats.append(s)
            if cur:
                steps[cur].append(s)
        elif 'SELFTEST: step ' in line:
            cur = line.split('SELFTEST: step ', 1)[1].strip()
            steps[cur] = stats[-1:]     # the rate of a step starts at the stats line before it
    res['window_s'] = stats[-1][0] - stats[0][0] if len(stats) > 1 else None
    res['fps'], res['fps_busy_median'] = rate(stats)
    res['fps_by_step'] = {k: rate(v)[0] for k, v in steps.items()}
    f = FLIPS.findall(boot)
    res['flips'], res['dropped'], res['hold_expired'] = (int(f[-1][0]), int(f[-1][1]), int(f[-1][2])) if f else (None, None, None)
    res['recoveries'] = len(re.findall(r'RECOVERY \d+', boot))
    res['backboardd_restarts'] = max(0, len(re.findall(r'^exec backboardd\s*$', boot, re.M)) - 1)
    bos = [int(x) for x in re.findall(r'submissions now name (\d+) BOs', boot)]
    res['bo_max'] = max(bos) if bos else None
    res['crashes'] = dict(collections.Counter(re.findall(r'^CRASH ([^\s\[]+)\[', boot, re.M)).most_common())
    res['key_lines'] = [l[:160] for l in boot.splitlines() if any(re.search(k, l) for k in (*DEFAULT_KEYS, *keys))][:40]
    return res


def frame_checks(frames_dir):
    """Which gestures changed nothing on screen, and which frames are blank."""
    prev, none, flat = None, [], []
    for p in sorted(pathlib.Path(frames_dir).glob('*.png')):
        try:
            b = framecmp.blocks(framecmp.read_png(p), 16)
        except Exception as x:
            flat.append('%s (%s)' % (p.name, x))
            continue
        if max(max(c) for c in b) - min(min(c) for c in b) < 8:
            flat.append(p.name)
        if '.check.' in p.name and prev is not None and len(prev) == len(b):
            if sum(1 for x, y in zip(prev, b) if max(abs(x[i] - y[i]) for i in range(3)) > 24) / len(b) < 0.005:
                none.append(p.name)
        prev = b
    return {'no_visible_effect': none, 'flat': flat}


def report(label, note, res):
    """The short summary a person reads."""
    s = res.get('log', {})
    out = ['%s %s: %s in %s s, root %s' % (label, note, res['result'], res.get('run_seconds', '?'), res.get('root_state', 'unknown'))]
    if s:
        out.append('frames/s %s (median while drawing %s) over %s s; flips %s, dropped %s, hold expired %s' % (
            s['fps'], s['fps_busy_median'], s['window_s'], s['flips'], s['dropped'], s['hold_expired']))
        out.append('recoveries %d, backboardd restarts %d, BO max %s, crashes %s' % (
            s['recoveries'], s['backboardd_restarts'], s['bo_max'], ' '.join('%s:%d' % c for c in s['crashes'].items()) or 'none'))
        if s['fps_by_step']:
            out.append('frames/s by step: ' + ' '.join('%s=%s' % kv for kv in s['fps_by_step'].items()))
    if 'frames' in res:
        c = res.get('frame_checks', {})
        out.append('%d frames; no visible effect: %s; blank: %s' % (res['frames'], ', '.join(c.get('no_visible_effect', [])) or 'none',
                                                                     ', '.join(c.get('flat', [])) or 'none'))
    if 'frames_ok' in res:
        out.append('against reference: ' + ('ok' if res['frames_ok'] else 'DIFFERS'))
    if res.get('stale'):
        out.append('ROOT FILES MAY BE STALE: the journal was not replayed, so pulled files can be old or missing. The kernel log is not affected.')
    if res.get('battery'):
        out.append('battery ' + res['battery'])
    out += ['  ' + k for k in s.get('key_lines', [])]
    return '\n'.join(out)


class Cycle:
    def __init__(self, a, runner, say):
        self.a, self.r, self.say = a, runner, say
        self.state = 'unknown'
        self.log_before = None

    def fastboot(self):
        return handsfree.fastboot_serial(self.r)

    def recovery(self):
        return handsfree.adb_serial(self.r)

    def wait(self, what, cond, seconds):
        v = handsfree.wait_for(cond, self.r, seconds)
        if not v:
            raise Stop('timed out after %d s waiting for %s' % (seconds, what))
        return v

    def locate(self):
        """Fastboot or recovery, with a person's help only if neither shows up."""
        def where():
            return 'fastboot' if self.fastboot() else 'recovery' if self.recovery() else None
        self.state = handsfree.wait_for(where, self.r, self.a.find_wait)
        if not self.state:
            self.say('no phone in fastboot or recovery: hold Power + Volume down')
            self.state = handsfree.wait_for(where, self.r, self.a.button_wait)
            if not self.state:
                raise Stop('no phone in fastboot or recovery after %d s' % (self.a.find_wait + self.a.button_wait))
        self.say('phone is in ' + self.state)

    def boot_until_left(self, img, cmd=None):
        """fastboot boot, retried: ast18 had a reboot sent right as fastboot returned accepted and ignored (/tmp/pull-log.sh)."""
        for attempt in range(self.a.boot_tries):
            rc, out = self.r.run(cmd or ['fastboot', 'boot', str(img)], timeout=300)
            if handsfree.wait_for(lambda: not self.fastboot(), self.r, 30):
                return
            self.say('still in fastboot after boot attempt %d (rc %d), retrying' % (attempt + 1, rc))
            self.r.sleep(self.a.boot_retry_sleep)
        self.state = 'fastboot'
        raise Stop('%s did not leave fastboot in %d tries (a stall in Sending can need Power + Volume down)' % (img, self.a.boot_tries))

    def to_recovery(self):
        """Ends with adbd as root. `adb root` restarts adbd, so the server is restarted as in /tmp/pull-log.sh."""
        if self.state == 'fastboot':
            self.say('booting recovery')
            # recovery_a has no kernel: ABL takes boot_a's, which is ours now, so `reboot recovery` boots iOS.
            # This is OrangeFox's ramdisk with the stock kernel, in RAM only, and `boot` of it writes nothing.
            self.boot_until_left(self.a.recovery)
            self.state = 'unknown'
        def up():
            s = self.recovery()
            if not s:
                self.r.run(['adb', 'kill-server'])
                self.r.run(['adb', 'start-server'], timeout=30)
            return s
        self.wait('recovery', up, self.a.recovery_wait)
        self.state = 'recovery'
        self.r.sleep(5)
        self.r.run(['adb', 'root'])
        self.r.sleep(3)
        self.r.run(['adb', 'kill-server'])
        self.r.run(['adb', 'start-server'], timeout=30)
        return self.wait('recovery after adb root', self.recovery, 60)

    def to_fastboot(self):
        self.r.run(['adb', 'reboot', 'bootloader'])
        self.wait('fastboot after adb reboot bootloader', self.fastboot, 120)
        self.state = 'fastboot'
        self.r.sleep(4)

    def root_state(self, serial):
        rc, out = self.r.run([*PY, str(PUT), '--adb', serial, '--state'], timeout=300)
        return 'clean' if rc == 0 else out.strip().splitlines()[-1] if out.strip() else 'state read failed (rc %d)' % rc

    def run_test(self, img, res):
        """From fastboot: boot the image and wait until it is back."""
        t0 = self.r.now()
        self.say('booting ' + str(img))
        self.boot_until_left(img)
        self.state = 'unknown'
        if not handsfree.wait_for(self.fastboot, self.r, self.deadline):
            self.say('no return in %d s, hard hang: hold Power + Volume down' % self.deadline)
            res['hang'] = True
            if not handsfree.wait_for(self.fastboot, self.r, self.a.button_wait):
                raise Stop('hard hang, nobody pressed the buttons in %d s' % self.a.button_wait)
        self.state = 'fastboot'
        self.r.sleep(4)
        res['run_seconds'] = round(self.r.now() - t0)
        self.say('back in fastboot after %d s' % res['run_seconds'])

    def push(self, serial, run_dir, res):
        """Replace root files. A dirty root gets one extra run of the image: the mount replays the journal, a clean exit closes it."""
        state = self.root_state(serial)
        res['root_before'] = state
        if state != 'clean':
            self.say('root not clean (%s): one boot to replay the journal' % state)
            self.to_fastboot()
            self.run_test(self.a.replay_image or self.a.image, {})
            serial = self.to_recovery()
            state = self.root_state(serial)
            if state != 'clean':
                raise Stop('root still not clean after a replay boot: ' + state + '. Boot a known good image with --replay-image, '
                           'or use Settings > Shut Down from one')
        body = (HERE / 'selftest/scripts' / (self.a.test + '.sh')).read_text()
        rc, out = self.r.run([*PY, str(HFS_PULL), '-s', serial, str(run_dir), HOOKED], timeout=300)
        local = pathlib.Path(run_dir) / pathlib.Path(HOOKED).name
        if not self.a.dry_run:
            if rc != 0 or not local.exists():
                raise Stop('cannot read %s from the root: %s' % (HOOKED, out[-300:]))
            base = local.read_text()
            local.rename(local.with_suffix('.before'))
            local.write_text(compose_script(body, base))
        pairs = ['%s=%s' % (HOOKED, local)] + list(self.a.files)
        rc, out = self.r.run([*PY, str(PUT), '--adb', serial, '--undo-dir', str(run_dir), *pairs], timeout=self.a.put_wait)
        res['put'] = out.strip().splitlines()[-8:]
        if rc != 0:
            raise Stop('put failed (rc %d): %s\nThe root may be half written. The undo file is in %s: %s --adb %s --undo FILE' % (
                rc, out[-400:], run_dir, PUT, serial))
        return serial

    def collect(self, serial, run_dir, res):
        """Log first: it is raw and always right. Root files only after the journal state is known."""
        log = pathlib.Path(self.a.logs) / ('%s.txt' % run_dir.name)
        rc, out = self.r.run([str(LOG_PULL), '-s', serial, str(log)], timeout=300)
        res['log_file'] = str(log)
        text = log.read_text(errors='replace') if log.exists() else ''
        res['root_state'] = self.root_state(serial)
        res['stale'] = res['root_state'] != 'clean'
        rc, out = self.r.run(['adb', 'shell', BATTERY], timeout=30)
        res['battery'] = ' '.join(out.split()) if rc == 0 else None
        var = run_dir
        self.r.run([*PY, str(HFS_PULL), '-s', serial, str(var), *PULLS, *self.a.pull], timeout=600)
        frames = var / 'frames'
        rc, listing = self.r.run([*PY, str(HFS_PULL), '-s', serial, '--ls', str(frames), FRAMES], timeout=300)
        paths = [l.rsplit(' ', 1)[0] for l in listing.splitlines() if l.endswith(' f') and '.png ' in l]
        if paths:
            self.r.run([*PY, str(HFS_PULL), '-s', serial, str(frames), *paths], timeout=900)
        res['frames'] = len(paths)
        res['log'] = summarize(text, self.a.key) if text else {}
        res['result'] = handsfree.classify(last_boot(text), paths, not res.get('hang')) if text else 'no-log'
        if text and text == self.log_before:      # the kernel never reached its log: these lines are the run before
            res['result'], res['log'] = 'no-new-log', {}
        res['frame_checks'] = frame_checks(frames) if paths and frames.is_dir() else {}
        if self.a.ref and paths and frames.is_dir():
            cmp = framecmp.compare_dirs(self.a.ref, frames)
            res['compare'] = cmp
            res['frames_ok'] = all(c['ok'] for c in cmp)

    def run(self):
        a = self.a
        res = {'image': str(a.image), 'note': a.note, 'label': a.label, 'test': a.test, 'files': a.files, 'result': 'stopped'}
        logs = pathlib.Path(a.logs)
        run_dir = logs / ('%s-%s' % (a.label, a.note))
        res['run_dir'] = str(run_dir)
        run_dir.mkdir(parents=True, exist_ok=True)
        try:
            self.locate()
            if not a.no_push:
                serial = self.to_recovery()
                rc, out = self.r.run(['adb', 'shell', BATTERY], timeout=30)
                if rc == 0 and out.split() and out.split()[0].isdigit() and int(out.split()[0]) < LOW_BATTERY:
                    raise Stop('battery %s%%: charge the phone, the ADSP is down in iOS and a cut mid-run costs the state' % out.split()[0])
                serial = self.push(serial, run_dir, res)
                before = run_dir / 'log-before.txt'
                self.r.run([str(LOG_PULL), '-s', serial, str(before)], timeout=300)
                self.log_before = before.read_text(errors='replace') if before.exists() else None
                self.to_fastboot()
            elif self.state == 'recovery':
                self.to_fastboot()
            self.run_test(a.image, res)
            self.collect(self.to_recovery(), run_dir, res)
            return res, 0 if res['result'] == 'selftest-done' else 1
        except Stop as x:
            res['stop'] = str(x)
            self.say('STOP: %s\nPhone: %s' % (x, self.state))
            return res, 2
        finally:
            (run_dir / 'result.json').write_text(json.dumps(res, indent=1))


class DryRunner(Runner):
    """Prints commands and plays a phone. Runs nothing, and the fake phone returns from a test at once."""

    def __init__(self, printer=print):
        self.t, self.st, self.print, self.at = 0.0, 'fastboot', printer, 0.0
        self.sleep, self.now = self.tick, lambda: self.t

    def tick(self, s):
        self.t += s
        if self.st == 'ios' and self.t - self.at > 5:
            self.st = 'fastboot'

    def run(self, argv, timeout=120):
        if not command_allowed(argv):
            raise PermissionError('refused: ' + shlex.join(argv))
        if argv[-1] not in ('devices', 'get-state', 'kill-server', 'start-server'):
            self.print('would run: ' + shlex.join(argv))
        if argv[:2] == ['fastboot', 'devices']:
            return 0, 'FAKE\tfastboot\n' if self.st == 'fastboot' else ''
        if argv[:2] == ['fastboot', 'boot']:
            self.st, self.at = ('recovery' if 'OrangeFox' in argv[2] else 'ios'), self.t
        elif argv[:2] == ['adb', 'devices']:
            return 0, 'List of devices attached\n' + ('FAKE\trecovery\n' if self.st == 'recovery' else '')
        elif argv[:3] == ['adb', 'reboot', 'bootloader']:
            self.st = 'fastboot'
        elif argv[1:2] == ['shell']:
            return 0, '80\nCharging\n'
        return 0, ''


def main(argv=None, runner=None, say=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('image', nargs='?')
    ap.add_argument('files', nargs='*', help='ROOTPATH=LOCALFILE: replace an existing root file (new paths still need a flash)')
    ap.add_argument('--note', default='run')
    ap.add_argument('--test', default='ui', help='selftest/scripts/NAME.sh')
    ap.add_argument('--pull', action='append', default=[], metavar='ROOTPATH')
    ap.add_argument('--key', action='append', default=[], metavar='REGEX', help='extra log lines for the summary')
    ap.add_argument('--ref', help='reference frames for framecmp')
    ap.add_argument('--logs', default=str(REPO / 'logs'))
    ap.add_argument('--recovery', default=str(RECOVERY))
    ap.add_argument('--replay-image', help='image for a replay boot when the root is dirty (default: the test image)')
    ap.add_argument('--no-push', action='store_true')
    ap.add_argument('--find-wait', type=int, default=120)
    ap.add_argument('--button-wait', type=int, default=1800)
    ap.add_argument('--recovery-wait', type=int, default=180)
    ap.add_argument('--put-wait', type=int, default=900)
    ap.add_argument('--boot-tries', type=int, default=4)
    ap.add_argument('--boot-retry-sleep', type=int, default=12)
    ap.add_argument('--print-args', action='store_true')
    ap.add_argument('--print-script', metavar='NAME')
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args(argv)
    if a.print_script:
        print(compose_script((HERE / 'selftest/scripts' / (a.print_script + '.sh')).read_text()), end='')
        return 0
    if not a.image:
        ap.error('give a boot image')
    if a.print_args:
        print(bootargs_of(a.image))
        return 0
    a.note = re.sub(r'[^A-Za-z0-9._-]+', '-', a.note)[:40]
    say = say or (lambda s: print(time.strftime('%H:%M:%S'), s, flush=True))
    if not (HERE / 'selftest/scripts' / (a.test + '.sh')).exists():
        sys.exit('no test script %s' % a.test)
    for p in [a.image, *([a.replay_image] if a.replay_image else [])]:
        if p.endswith('.simg') or not p.endswith('.img'):
            sys.exit('refused: %s is not a boot image' % p)
        why, deadman = check_loop_args(bootargs_of(p))
        if why and not (p == a.replay_image and why.startswith('no sweetselftest')):
            sys.exit('refused: %s: %s' % (p, why))
        if p == a.image:
            image_deadman = deadman
    for f in a.files:
        if '=' not in f or not os.path.isfile(f.split('=', 1)[1]):
            sys.exit('give ROOTPATH=LOCALFILE, got ' + f)
        if f.split('=', 1)[0] == HOOKED:
            sys.exit('%s is the self-test hook: dev.py writes it' % HOOKED)
    if a.dry_run:
        a.logs = tempfile.mkdtemp(prefix='dev-dry')
    pathlib.Path(a.logs).mkdir(parents=True, exist_ok=True)
    a.label = next_label(a.logs)
    runner = runner or (DryRunner() if a.dry_run else Runner())
    cycle = Cycle(a, runner, say)
    cycle.deadline = image_deadman + 30 + 90       # the deadman, its 30 s backstop, and the way into fastboot
    res, code = cycle.run()
    if 'stop' not in res:
        text = report(a.label, a.note, res)
        print(text)
        (pathlib.Path(res['run_dir']) / 'summary.txt').write_text(text + '\n')
    return 0 if a.dry_run and code == 1 else code     # a fake phone leaves no log to pass


if __name__ == '__main__':
    sys.exit(main())
