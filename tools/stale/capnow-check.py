#!/usr/bin/env python3
# Compiles bg_cap_now_due from the patched bgmtl.m on the host with the stamp path swapped for a temp file: no fire at the first
# look, one fire per mtime change, none while it stays, the re-arm fires once, a missing file never fires.
import os, re, subprocess, sys, tempfile, time
src = open(os.path.join(os.path.dirname(__file__), os.environ.get('BGMTL_M', '../Chlorophyll/src/bgmtl.m'))).read()
fn = re.search(r'static unsigned bg_cap_now, bg_cap_now_again;\nstatic BOOL bg_cap_now_due\(void\)\n\{.*?\n\}', src, re.S).group(0)
d = tempfile.mkdtemp(); stamp = os.path.join(d, 'capnow')
fn = fn.replace('"/private/var/tmp/bgmtl.capnow"', 'STAMP')
c = f'''#include <stdio.h>
#include <sys/stat.h>
#include <objc/objc.h>
#define STAMP "{stamp}"
{fn}
int main(int c, char **v) {{ if (v[1][0]=='a') bg_cap_now_again=1; printf("%d", bg_cap_now_due()); return 0; }}
'''
# one process per check would reset the static, so drive it through stdin steps instead
c = c.replace('int main(int c, char **v) {', 'int main(void) { char s[8]; while (scanf("%7s", s) == 1) { if (s[0]==\'a\') bg_cap_now_again=1; printf("%d", bg_cap_now_due()); fflush(stdout); } return 0; } int unused(int c, char **v) {')
open(os.path.join(d, 't.c'), 'w').write(c)
subprocess.check_call(['clang', '-w', '-o', os.path.join(d, 't'), os.path.join(d, 't.c')])
p = subprocess.Popen([os.path.join(d, 't')], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
def look(cmd='x'):
    p.stdin.write(cmd + '\n'); p.stdin.flush(); return p.stdout.read(1)
out = look()                                  # missing file
open(stamp, 'w').close(); os.utime(stamp, (1, 1)); out += look()   # first look learns
out += look()                                 # unchanged
os.utime(stamp, (2, 2)); out += look() + look()   # change fires once
out += look('a')                              # re-arm fires
os.utime(stamp, (3, 3)); out += look()
p.stdin.close(); p.wait()
want = '0001011'
print('capnow-check', 'ok' if out == want else f'FAIL got {out} want {want}')
sys.exit(out != want)
