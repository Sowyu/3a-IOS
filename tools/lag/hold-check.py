#!/usr/bin/env python3
"""Compiles the hold gate out of bgmtl.m and runs it against a fake hold and a fake clock (lag: the rest after four misses lasts
2 s, not 120 busy commits).  hold-check.py [BGMTL_M]"""
from pathlib import Path
import subprocess, sys, tempfile
here = Path(__file__).resolve().parent
m = Path(sys.argv[1]) if len(sys.argv) > 1 else here.parent / 'next-keep/Chlorophyll/src/bgmtl.m'
s = m.read_text()
gate = s[s.index('// BEGIN hold gate'):s.index('// END hold gate')]
src = '''#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
typedef int BOOL;
#define YES 1
#define NO 0
typedef void *IOSurfaceRef;
static long free_after;     // polls until the fake hold goes; -1 never
static int calls, lines;
static char last[256];
static BOOL IOSurfaceIsInUse(IOSurfaceRef s) { (void)s; calls++; return free_after < 0 || free_after-- > 0; }
static uint64_t fake_ns = 20000000000ull;   // past the first 10 s log interval
#define clock_gettime_nsec_np(c) (fake_ns)
#define usleep(us) (fake_ns += (uint64_t)(us) * 1000ull)
static void bgmtl_note_line(const char *l) { lines++; strncpy(last, l, 255); }
''' + gate + '''
int main(void)
{
   IOSurfaceRef s[1] = { (void *)1 };
   free_after = 0;                                  // free: no wait
   bg_hold_gate(s, 1);
   assert(g_hold.busy == 0 && g_hold.wait_us == 0 && lines == 1 && strstr(last, "commits=1 busy=0"));
   free_after = 3;                                  // the copy ends: released, and no timeout
   bg_hold_gate(s, 1);
   assert(g_hold.busy == 1 && g_hold.wait_us > 0 && g_hold.timeouts == 0 && g_hold.wait_us < BG_HOLD_MAX_US);
   free_after = -1;                                 // stuck: the limit holds, then four misses stop the waiting for 120 commits
   for (int i = 0; i < 4; i++) {
      bg_hold_gate(s, 1);
      assert(g_hold.max_us >= BG_HOLD_MAX_US && g_hold.max_us < BG_HOLD_MAX_US + 20000);
   }
   assert(g_hold.timeouts == 4 && g_hold.rest_until > fake_ns);
   uint64_t before = g_hold.wait_us;
   for (int i = 0; i < 300; i++) { bg_hold_gate(s, 1); fake_ns += 1000000ull; }   // 300 busy commits in 0.3 s: all skipped
   assert(g_hold.skipped == 300 && g_hold.wait_us == before);
   fake_ns += 2000000000ull;                        // the rest is over after 2 s, however few commits were busy
   bg_hold_gate(s, 1);
   assert(g_hold.timeouts == 5);
   free_after = 0;                                  // a clean commit resets the miss count
   bg_hold_gate(s, 1);
   assert(g_hold.stuck == 0);
   puts("hold gate: ok");
   return 0;
}
'''
with tempfile.TemporaryDirectory() as t:
    c = Path(t) / 'h.c'
    c.write_text(src)
    subprocess.run(['clang', '-Wall', '-Wno-unused-function', '-O1', str(c), '-o', t + '/h'], check=True)
    subprocess.run([t + '/h'], check=True)
