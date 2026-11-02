#!/usr/bin/env python3
"""Host check of 2130 against Sol's two audits (analysis/input-ff-audit.md findings 3 to 5, analysis/2130-reaudit.md blockers 1
and 2 and should-fixes 3 and 4). Builds the real exec_extract_strings, 2130's helpers and its lines around exec_handle_sugid,
cut from temp copies of the kernel tree's kern_exec.c, four ways:
  base       the tree as it is: 2058 without 2130
  audited    tests/old/2130-audited.patch, the first audit's version
  reaudited  tests/old/2130-reaudited.patch, the second audit's version
  current    plans/freeform/2130-asteroids-app-metal-exec.patch
and runs them against kernel stubs: user memory addressed by offset (so 4-byte pointers work), exec_add_user_string with the
kernel's own accounting, a path builder relative to the caller's chroot as vn_getpath's is, and an exec_handle_sugid stub that
makes its changes during the call. Every finding must reproduce on the version it was found in and be fixed in current. With
sweetappmetal off or refused, current must equal base exactly: string area bytes, counters, pointer vector and console lines.
Writes nothing to the tree. Run: python3 -I plans/freeform/tests/exec_env_check.py"""
import itertools, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.abspath(os.path.join(HERE, '../../..'))
K = os.path.join(ROOT, 'ios-asteroids/research/xnu-local/dxb/xnu')
PATCHES = {'base': None, 'audited': os.path.join(HERE, 'old/2130-audited.patch'),
           'reaudited': os.path.join(HERE, 'old/2130-reaudited.patch'),
           'current': os.path.join(HERE, '../2130-asteroids-app-metal-exec.patch')}

STUBS = r'''
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int boolean_t;
#define TRUE 1
#define FALSE 0
typedef uint64_t user_addr_t;
struct vnode { const char *path; };
struct filedesc { struct vnode *fd_rdir; };
struct proc { struct filedesc p_fd; };
typedef struct proc *proc_t;
#define PROC_NULL ((proc_t)0)
#define NULLVP ((struct vnode *)0)
#define CAST_USER_ADDR_T(x) ((user_addr_t)(uintptr_t)(x))
#define UIO_SYSSPACE 0
#define UIO_USERSPACE 1
#define EXECUTABLE_KEY "executable_path="
#define CPU_SUBTYPE_MASK 0xff000000u
#define CPU_SUBTYPE_ARM64E 2
#define VSUID 04000
#define VSGID 02000
#define IMGPF_INTERPRET 0x1
#define IMGPF_WAS_64BIT_ADDR 0x4
#define IMGPF_IS_64BIT_ADDR 0x8
#define IS_WHITESPACE(c) ((c) == ' ' || (c) == '\t')
#define CONFIG_MACF 1
#define VN_GETPATH_NO_PROCROOT 0x8
#define CS_INSTALLER 0x8
#define CS_FORCED_LV 0x10
#define CS_RESTRICT 0x800
#define CS_REQUIRE_LV 0x2000
#define CS_RUNTIME 0x10000
#define CS_DATAVAULT_CONTROLLER 0x80000000u
#define CS_NVRAM_UNRESTRICTED 0x40000000u
struct vattr { int va_mode; };
struct image_params {
	char *ip_strings, *ip_strendp, *ip_startargv, *ip_endargv, *ip_endenvv;
	int ip_strspace, ip_argspace, ip_argc, ip_envc, ip_flags, ip_seg, ip_interp_sugid_fd;
	user_addr_t ip_user_argv, ip_user_envv, ip_user_fname;
	unsigned ip_origcpusubtype;
	struct vattr *ip_origvattr;
	struct vnode *ip_vp;
	char ip_interp_buffer[256];
	uint32_t ip_csflags;
	void *ip_vfs_context, *ip_scriptvp, *ip_scriptlabelp, *ip_execlabelp, *ip_px_smpx;
	long ip_arch_offset;
};
static char ubase[65536];                     /* user memory: a user address is an offset into it */
static const char *ba_sb, *ba_app, *ba_env;   /* the scenario's boot-args, NULL = absent */
static struct proc caller, target;            /* current_proc() and the new process (posix_spawn) */
static int t_setugid, q_mac, s_setugid, s_mac;
static uint32_t s_cs;
static int
PE_parse_boot_argn(const char *n, void *v, int size)
{
	const char *s = !strcmp(n, "sweetsb") ? ba_sb : !strcmp(n, "sweetappmetal") ? ba_app : !strcmp(n, "sweetexecenv") ? ba_env : NULL;
	if (!s) return 0;
	if (!strcmp(n, "sweetsb")) { *(int *)v = atoi(s); return 1; }
	snprintf(v, size, "%s", s);
	return 1;
}
static int sweet_arm64e_enabled(void) { return 1; }
static int
copyinptr(user_addr_t a, user_addr_t *out, int size)
{
	if (size == 8) { uint64_t v; memcpy(&v, ubase + a, 8); *out = v; } else { uint32_t v; memcpy(&v, ubase + a, 4); *out = v; }
	return 0;
}
/* the kernel's: copy up to the space left, then E2BIG once nothing is left */
static int
exec_add_user_string(struct image_params *imgp, user_addr_t str, int seg, boolean_t is_ncargs)
{
	int error = 0;
	const char *src = seg == UIO_SYSSPACE ? (const char *)(uintptr_t)str : ubase + str;
	do {
		int space = is_ncargs ? imgp->ip_argspace : imgp->ip_strspace;
		if (space <= 0) { error = E2BIG; break; }
		size_t n = strlen(src) + 1, len = n <= (size_t)space ? n : (size_t)space;
		memcpy(imgp->ip_strendp, src, len);
		error = n <= (size_t)space ? 0 : ENAMETOOLONG;
		src += len;
		imgp->ip_strendp += len;
		imgp->ip_strspace -= (int)len;
		if (is_ncargs) imgp->ip_argspace -= (int)len;
	} while (error == ENAMETOOLONG);
	return error;
}
static proc_t current_proc(void) { return &caller; }
static proc_t vfs_context_proc(void *ctx) { (void)ctx; return &target; }
static void proc_fdlock(proc_t p) { (void)p; }
static void proc_fdunlock(proc_t p) { (void)p; }
/* vfs_cache.c builds a path up to the calling process's root unless told not to */
static int
build_path(struct vnode *vp, char *buf, size_t len, int procroot)
{
	const char *s = vp->path, *r = caller.p_fd.fd_rdir ? caller.p_fd.fd_rdir->path : NULL;
	if (procroot && r && !strncmp(s, r, strlen(r)) && s[strlen(r)] == '/') s += strlen(r);
	if (strlen(s) + 1 > len) return ENOSPC;
	strcpy(buf, s);
	return 0;
}
static int vn_getpath(struct vnode *vp, char *buf, int *len) { return build_path(vp, buf, (size_t)*len, 1); }
static int
vn_getpath_ext(struct vnode *vp, struct vnode *dvp, char *buf, size_t *len, int flags)
{ (void)dvp; return build_path(vp, buf, *len, !(flags & VN_GETPATH_NO_PROCROOT)); }
static int proc_issetugid(proc_t p) { (void)p; return t_setugid; }
static int
mac_cred_check_label_update_execve(void *a, void *b, long c, void *d, void *e, void *f, proc_t g, void *h)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; return q_mac; }
'''

# after the cut source: exec_handle_sugid makes its changes during the call, as the real one does. Current's hands back the
# MAC decision only with the feature on, as its one new line there does.
SUGID = r'''
#ifdef SUGID_OUT
static int
exec_handle_sugid(struct image_params *imgp, int *sweet_mac)
{
	if (sweet_appmetal_enabled()) *sweet_mac = s_mac;
#else
static int
exec_handle_sugid(struct image_params *imgp)
{
#endif
	t_setugid = s_setugid;
	imgp->ip_csflags |= s_cs;
	return 0;
}
'''

MAIN = r'''
static void
dump(struct image_params *im, int rc)
{
	char *b = im->ip_strings;
	printf("RC %d\nSTATE argc %d envc %d strspace %d argspace %d start %ld endargv %ld endenvv %ld end %ld\n", rc, im->ip_argc,
	       im->ip_envc, im->ip_strspace, im->ip_argspace, (long)(im->ip_startargv - b), (long)(im->ip_endargv - b),
	       (long)(im->ip_endenvv - b), (long)(im->ip_strendp - b));
	printf("AREA ");
	for (char *c = b; c < im->ip_strendp; c++) printf("%02x", (unsigned char)*c);
	/* the pointer vector exec_copyout_strings derives: argc then envc strings walked from the start of argv */
	printf("\nPTRS");
	char *s = im->ip_startargv;
	for (int i = 0; i < im->ip_argc + im->ip_envc && s < im->ip_strendp; i++, s += strlen(s) + 1) printf(" %ld", (long)(s - b));
	printf("\n");
	s = im->ip_endargv;
	for (int i = 0; i < im->ip_envc && s < im->ip_endenvv; i++, s += strlen(s) + 1) printf("ENV %s\n", s);
}

int
main(int argc, char **argv)
{
	#define E(n, d) (getenv(n) ? getenv(n) : (d))
	#define A(n) (getenv(n) && *getenv(n) ? getenv(n) : NULL)
	ba_sb = A("T_SB"); ba_app = A("T_APP"); ba_env = A("T_ENV");
	static struct vnode rc_vn, rt_vn;
	rc_vn.path = A("T_RDIR_CALLER"); rt_vn.path = A("T_RDIR_TARGET");
	caller.p_fd.fd_rdir = rc_vn.path ? &rc_vn : NULL;
	target.p_fd.fd_rdir = rt_vn.path ? &rt_vn : NULL;
	q_mac = atoi(E("T_QMAC", "0")); s_mac = atoi(E("T_SMAC", "0")); s_setugid = atoi(E("T_SSUGID", "0"));
	s_cs = (uint32_t)strtoul(E("T_SCS", "0"), NULL, 0);
	int ptr = atoi(E("T_PTR", "8"));
	const char *path = E("T_PATH", "/Applications/Freeform.app/Freeform");
	static char strings[16384];
	struct vattr va = { (int)strtol(E("T_MODE", "0755"), NULL, 0) };
	struct vnode vn = { E("T_VNODE", path) };
	struct image_params im;
	memset(&im, 0, sizeof(im));
	im.ip_strings = strings;
	int n = snprintf(strings, sizeof(strings), EXECUTABLE_KEY "%s", path) + 1;
	im.ip_strendp = strings + n;
	im.ip_strspace = atoi(E("T_STRSPACE", "8192")) - n;
	im.ip_argspace = atoi(E("T_ARGSPACE", "6144"));
	im.ip_flags = ptr == 8 ? IMGPF_WAS_64BIT_ADDR | IMGPF_IS_64BIT_ADDR : 0;
	im.ip_seg = UIO_USERSPACE;
	im.ip_interp_sugid_fd = -1;
	im.ip_origcpusubtype = CPU_SUBTYPE_ARM64E;
	im.ip_origvattr = &va;
	im.ip_vp = &vn;
	im.ip_csflags = (uint32_t)strtoul(E("T_CS", "0"), NULL, 0);
	/* user memory: strings, then argv and envv of the process's pointer width */
	size_t u = 16, vec[64];
	int nv = 0;
	const char *strs[64];
	strs[nv++] = path;
	for (int i = 1; i < argc && nv < 60; i++) strs[nv++] = argv[i];
	for (int i = 0; i < nv; i++) { vec[i] = u; strcpy(ubase + u, strs[i]); u += strlen(strs[i]) + 1; }
	u = (u + 7) & ~(size_t)7;
	size_t av = u, ev;
	#define PUT(v) do { uint64_t w = (v); memcpy(ubase + u, &w, ptr); u += ptr; } while (0)
	PUT(vec[0]); PUT(0);
	ev = u;
	for (int i = 1; i < nv; i++) PUT(vec[i]);
	PUT(0);
	im.ip_user_argv = av;
	im.ip_user_envv = ev;
	int rc = exec_extract_strings(&im);
#ifdef HAVE_LATE
	if (rc == 0) late_step(&im);
#endif
	dump(&im, rc);
	return 0;
}
'''


def build(name, patch, t):
    k = os.path.join(t, name, 'k'); os.makedirs(os.path.join(k, 'bsd/kern'))
    shutil.copy(os.path.join(K, 'bsd/kern/kern_exec.c'), os.path.join(k, 'bsd/kern'))
    if patch:
        subprocess.run(['git', 'init', '-q', '.'], cwd=k, check=True)
        subprocess.run(['git', 'apply', patch], cwd=k, check=True)
    src = open(os.path.join(k, 'bsd/kern/kern_exec.c')).read()
    # sweet_ui_exec_path (2058), then everything from there to the end of exec_extract_strings: 2130's helpers sit between
    a = src.rindex('\nstatic int', 0, src.index('\nsweet_ui_exec_path(const char *p)\n'))
    b = src.index('\n}\n', src.index('\nexec_extract_strings(struct image_params *imgp)\n')) + 3
    body = src[a:b].replace('__attribute__((noinline))', '')
    late = ''
    if 'sweet_appmetal_inserted(imgp);' in src:
        i = src.index('\tchar *sweet_am = sweet_appmetal_inserted(imgp);')
        late = src[i:src.index('\tif (error) {', i)]
    c = os.path.join(t, name + '.c')
    with open(c, 'w') as f:
        f.write(STUBS + body)
        if late:   # the lines around exec_handle_sugid in exec_mach_imgact, verbatim
            f.write(SUGID)
            f.write('#define HAVE_LATE 1\nstatic void\nlate_step(struct image_params *imgp)\n{\n\tproc_t p = &target;\n\tint error = 0;\n'
                    + late + '\t(void)error;\n\t(void)p;\n}\n')
        f.write(MAIN)
    exe = os.path.join(t, name + '.bin')
    flags = ['-DSUGID_OUT'] if late and 'exec_handle_sugid(imgp, &sweet_am_mac)' in late else []
    r = subprocess.run(['cc', '-w', *flags, '-o', exe, c], capture_output=True, text=True)
    if r.returncode:
        sys.exit('build %s failed:\n%s' % (name, r.stderr[-3000:]))
    return exe


FF = '/Applications/Freeform.app/Freeform'
NOTES = '/Applications/Notes.app/Notes'
X = '/usr/libexec/x'
APP = 'DYLD_INSERT_LIBRARIES=/usr/local/lib/bgmtl/bgmtl-inject.dylib'
SB = 'DYLD_INSERT_LIBRARIES=/usr/local/lib/sweetsb.dylib'
BOTH = 'DYLD_INSERT_LIBRARIES=/usr/local/lib/sweetsb.dylib:/usr/local/lib/bgmtl/bgmtl-inject.dylib'
DENIED = 'SWEET_APPMETAL_DENIED='
TRIAL = '/private/var/trial'


def run(exe, inherit=(), **kw):
    env = {'T_' + k.upper(): str(v) for k, v in kw.items()}
    return subprocess.run([exe, *inherit], capture_output=True, text=True, env=env).stdout


def envs(out):
    return [l[4:] for l in out.splitlines() if l.startswith('ENV ')]


def inserts(out):
    return [e for e in envs(out) if e.startswith('DYLD_INSERT_LIBRARIES=')]


def field(out, key):
    return next(l for l in out.splitlines() if l.startswith(key + ' ') or l == key)


bad = 0


def report(ok, text):
    global bad
    print(('ok   ' if ok else 'FAIL ') + text)
    bad |= not ok


with tempfile.TemporaryDirectory() as t:
    exe = {n: build(n, p, t) for n, p in PATCHES.items()}

    # First audit, findings 3 to 5: reproduce on audited, fixed in current
    FIRST = [
        ('3', 'strips an inherited inject variable with both args absent', dict(path=NOTES, inherit=[APP]), lambda o: envs(o) == [APP]),
        ('3', 'strips an inherited combined variable with both args absent', dict(path=NOTES, inherit=[BOTH]), lambda o: envs(o) == [BOTH]),
        ('3', 'strips an inherited combined variable with only sweetsb=1', dict(sb=1, path=X, inherit=[BOTH]), lambda o: envs(o) == [BOTH]),
        ('4', 'injects into another bundle executable named Freeform', dict(app='Freeform', path='/Applications/Other.app/Freeform'),
         lambda o: inserts(o) == []),
        ('4', 'injects through a Freeform-named path whose file is another binary',
         dict(app='Freeform', path='/Applications/Other.app/Freeform', vnode='/private/var/tmp/evil'), lambda o: inserts(o) == []),
        ('4', 'injects when the exact path is a symlink to another file', dict(app='Freeform', vnode='/private/var/tmp/evil'),
         lambda o: inserts(o) == []),
        ('4', 'accepts a boot-arg naming another app',
         dict(app='MobileSafari', path='/Applications/MobileSafari.app/MobileSafari'), lambda o: inserts(o) == []),
        ('5', 'leaves the inject in a restricted target', dict(app='Freeform', cs=0x800), lambda o: inserts(o) == []),
        ('5', 'leaves the inject in a hardened-runtime target', dict(app='Freeform', cs=0x10000), lambda o: inserts(o) == []),
        ('5', 'leaves the inject in a library-validated target', dict(app='Freeform', cs=0x2000), lambda o: inserts(o) == []),
        ('5', 'leaves the inject across a MAC credential transition', dict(app='Freeform', qmac=1, smac=1), lambda o: inserts(o) == []),
        ('5', 'leaves the inject when the exec turns set-id', dict(app='Freeform', ssugid=1), lambda o: inserts(o) == []),
        ('note', 'adds a second insert variable after sweetexecenv appended one (trial on)',
         dict(sb=1, env='Freeform:DYLD_INSERT_LIBRARIES=/x.dylib', app='Freeform'), lambda o: len(inserts(o)) == 1),
    ]
    for f, what, kw, ok in FIRST:
        kw = dict(kw); inh = kw.pop('inherit', ())
        o_old, o_new = run(exe['audited'], inh, **kw), run(exe['current'], inh, **kw)
        report(not ok(o_old) and ok(o_new), 'audit 1, finding %s: %s: audited %s, current %s' % (
            f, what, 'reproduces' if not ok(o_old) else 'DOES NOT REPRODUCE', 'fixed' if ok(o_new) else 'STILL WRONG %r' % envs(o_new)))

    # Re-audit: reproduce on reaudited, fixed in current
    chroot = dict(app='Freeform', rdir_caller=TRIAL, rdir_target=TRIAL, vnode=TRIAL + FF)
    SECOND = [
        ('blocker 1', 'a launcher chrooted at %s execs its own /Applications/Freeform.app/Freeform' % TRIAL, chroot,
         lambda o: inserts(o) == []),
        ('blocker 1', 'the same with sweetsb=1: the chrooted binary gets sweetsb only, as with 2058 alone', dict(chroot, sb=1),
         lambda o: inserts(o) == [SB]),
        ('should-fix 3', 'the MAC decision inside exec_handle_sugid is a transition, the earlier query said none',
         dict(app='Freeform', qmac=0, smac=1), lambda o: inserts(o) == []),
        ('should-fix 3', 'the earlier query said transition, the decision inside exec_handle_sugid did not',
         dict(app='Freeform', qmac=1, smac=0), lambda o: inserts(o) == [APP]),
        ('review note', 'sweetexecenv gives Notes the exact inject value; a restricted Notes exec must keep it as sweetexecenv set it',
         dict(app='Freeform', path=NOTES, env='Notes:' + APP, cs=0x800), lambda o: inserts(o) == [APP]),
    ]
    for f, what, kw, ok in SECOND:
        o_old, o_new = run(exe['reaudited'], **kw), run(exe['current'], **kw)
        report(not ok(o_old) and ok(o_new), 're-audit %s: %s: reaudited %s, current %s' % (
            f, what, 'reproduces' if not ok(o_old) else 'DOES NOT REPRODUCE', 'fixed' if ok(o_new) else 'STILL WRONG %r' % envs(o_new)))
    # blocker 2 is "differs from base with the trial off": reaudited must differ from base, current must not
    for app in ('', 'Notes', '0', 'freeform'):
        kw = dict(sb=1, app=app, env='Freeform:DYLD_INSERT_LIBRARIES=/x.dylib')
        o_b, o_old, o_new = run(exe['base'], **kw), run(exe['reaudited'], **kw), run(exe['current'], **kw)
        report(o_old != o_b and o_new == o_b, 're-audit blocker 2: sweetsb=1, sweetappmetal=%r, sweetexecenv insert for Freeform: '
               'reaudited %s base (%d inserts against %d), current %s base' % (
                   app or '(absent)', 'differs from' if o_old != o_b else 'EQUALS', len(inserts(o_old)), len(inserts(o_b)),
                   'equals' if o_new == o_b else 'DIFFERS FROM'))

    # Off means 2058 alone: current against base, every byte of the state, over a matrix
    n = same = 0
    first_diff = None
    APPS = ['', 'Notes', '0', 'freeform', 'FreeformX', 'Freeform ']
    for sb, app, ev, (path, vnode), inh, ptr, cs, extra in itertools.product(
            ['', '1'], APPS,
            ['', 'Freeform:DYLD_INSERT_LIBRARIES=/x.dylib', 'Freeform:' + APP, 'Notes:A=1,' + BOTH],
            [(FF, FF), (NOTES, NOTES), (X, X), ('/Applications/Other.app/Freeform', '/Applications/Other.app/Freeform'), (FF, TRIAL + FF)],
            [(), (APP,), (BOTH,), (SB,), (DENIED + APP[22:],), ('A=1', 'DYLD_INSERT_LIBRARIES=/mine.dylib', 'B=2')],
            [8, 4], [0, 0x800], [{}, dict(ssugid=1, smac=1, scs=0x10000), dict(mode='04755'), dict(rdir_caller=TRIAL, rdir_target=TRIAL)]):
        kw = dict(sb=sb, app=app, env=ev, path=path, vnode=vnode, ptr=ptr, cs=cs, **extra)
        o_b, o_c = run(exe['base'], inh, **kw), run(exe['current'], inh, **kw)
        n += 1
        same += o_b == o_c
        if o_b != o_c and first_diff is None:
            first_diff = (kw, inh)
    report(same == n, 'trial off or refused: current equals base in %d of %d execs (string bytes, counters, pointers, console)' % (same, n))
    if first_diff:
        print('     first difference: %r inherit %r' % first_diff)

    # Capacity: off, the append threshold is base's to the byte; on, it is where the arithmetic says, and one byte less fails open
    for ptr in (8, 4):
        for kw in (dict(sb=1, path=NOTES), dict(sb=1, path=FF)):
            outs = [(run(exe['base'], ('A=1',), argspace=a, ptr=ptr, **kw), run(exe['current'], ('A=1',), argspace=a, ptr=ptr, **kw))
                    for a in range(140, 260)]
            report(all(b == c for b, c in outs), 'capacity, trial off, %d-byte pointers, %s: current equals base at every argspace 140 to 259'
                   % (ptr, kw['path']))
        # on: measured against 2058's own sweetsb append on the same exec in base, whose arithmetic the audit cleared; the
        # thresholds must differ by the string length difference exactly, and one byte less must skip without an error
        inh = ('A=1', 'B=22')

        def low(exe_, var_kw, key):
            return next(v for v in range(40, 600) if inserts(run(exe_, inh, ptr=ptr, **{key: v}, **var_kw)))
        ref = low(exe['base'], dict(sb=1), 'argspace')
        for kw, var in ((dict(app='Freeform'), APP), (dict(sb=1, app='Freeform'), BOTH)):
            got = low(exe['current'], kw, 'argspace')
            below = run(exe['current'], inh, ptr=ptr, argspace=got - 1, **kw)
            report(got - ref == len(var) - len(SB) and inserts(below) == [] and field(below, 'RC') == 'RC 0',
                   'capacity, trial on, %d-byte pointers, %d-byte variable: needs argspace %d, 2058 needs %d for its %d bytes, and %d '
                   'skips without error' % (ptr, len(var) + 1, got, ref, len(SB) + 1, got - 1))
        # strspace steps with the initial alignment padding, so check the append's condition itself on every value: the space
        # left before it (final count, plus the variable if appended, plus the tail padding) is at least length + one pointer
        # exactly when it appended. 2058's own append in base passes the same test.
        for name_, kw, var in (('base', dict(sb=1), SB), ('current', dict(app='Freeform'), APP), ('current', dict(sb=1, app='Freeform'), BOTH)):
            wrong = []
            for sv in range(120, 260):
                o = run(exe[name_], inh, ptr=ptr, strspace=sv, **kw)
                if field(o, 'RC') != 'RC 0':
                    wrong.append((sv, 'error')); continue
                area = bytes.fromhex(field(o, 'AREA')[5:])
                tail = len(area) - len(area.rstrip(b'\0')) - 1
                left = int(field(o, 'STATE').split()[6]) + tail + (len(var) + 1 if inserts(o) else 0)
                if bool(inserts(o)) != (left >= len(var) + 1 + ptr):
                    wrong.append((sv, left))
            report(not wrong, 'capacity, %s, %d-byte pointers, %d-byte variable: appended exactly when strspace left >= length + pointer, '
                   'strspace 120 to 259%s' % (name_, ptr, len(var) + 1, '' if not wrong else ' %r' % wrong[:3]))

    # A refusal renames 22 bytes and nothing else: neighbours, counters and pointers as accepted, both pointer widths
    for ptr in (8, 4):
        for kw, inh, what in ((dict(sb=1, app='Freeform'), ('A=1', 'BB=22'), 'appended variable after A=1 and BB=22'),
                              (dict(app='Freeform', env='Freeform:' + APP + ',Z=9'), ('A=1',), 'sweetexecenv value between A=1 and Z=9')):
            ok_o = run(exe['current'], inh, ptr=ptr, **kw)
            no_o = run(exe['current'], inh, ptr=ptr, cs=0x800, **kw)
            a, b = bytes.fromhex(field(ok_o, 'AREA')[5:]), bytes.fromhex(field(no_o, 'AREA')[5:])
            diff = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
            at = a.find(b'DYLD_INSERT_LIBRARIES=/usr/local/lib/')
            good = (len(a) == len(b) and diff and diff[0] >= at and diff[-1] < at + 22 and b[at:at + 22] == DENIED.encode() and
                    field(ok_o, 'STATE') == field(no_o, 'STATE') and field(ok_o, 'PTRS') == field(no_o, 'PTRS'))
            report(good, 'refusal, %d-byte pointers, %s: only the 22 name bytes change; neighbours, counters and pointers stay'
                   % (ptr, what))

    # Behaviour current keeps: the trial itself, children, and the sugid stub's changes made during the call
    KEEP = [
        ('Freeform gets the inject', dict(app='Freeform'), (), lambda o: inserts(o) == [APP]),
        ('Freeform gets both, sweetsb first, in one variable', dict(sb=1, app='Freeform'), (), lambda o: inserts(o) == [BOTH]),
        ('with the trial on, other apps get nothing new', dict(app='Freeform', path=NOTES), (), lambda o: inserts(o) == []),
        ('a child of Freeform drops the inject', dict(app='Freeform', path=X), (APP,), lambda o: inserts(o) == []),
        ('a child drops the renamed form', dict(app='Freeform', path=X), (DENIED + APP[22:],), lambda o: envs(o) == []),
        ('a launcher\'s own insert is kept and suppresses ours', dict(app='Freeform'), ('DYLD_INSERT_LIBRARIES=/mine.dylib',),
         lambda o: inserts(o) == ['DYLD_INSERT_LIBRARIES=/mine.dylib']),
        ('posix_spawn into a chroot (the new process rooted, the caller not) is refused',
         dict(app='Freeform', rdir_target=TRIAL), (), lambda o: inserts(o) == []),
        ('hardened runtime set inside exec_handle_sugid is refused', dict(app='Freeform', scs=0x10000), (), lambda o: inserts(o) == []),
        ('set-id credentials made inside exec_handle_sugid are refused', dict(sb=1, app='Freeform', ssugid=1), (),
         lambda o: envs(o) == [DENIED + BOTH[22:]]),
        ('a refusal says why on the console', dict(app='Freeform', cs=0x800), (),
         lambda o: 'SWEET EXEC: appmetal inject refused for /Applications/Freeform.app/Freeform: a restricted signature' in o),
        ('a chroot refusal says so', chroot, (), lambda o: 'refused for /Applications/Freeform.app/Freeform: a chroot' in o),
    ]
    for what, kw, inh, ok in KEEP:
        o = run(exe['current'], inh, **kw)
        report(ok(o), 'current: %s%s' % (what, '' if ok(o) else ' %r' % envs(o)))
sys.exit(bad)
