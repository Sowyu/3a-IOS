/*
 * chl-selftest: compute on the A810 alone, in a process of its own, before Freeform (plans/freeform, analysis/freeform.md).
 * No Metal.framework: libchlorophyll's C API (chl.h) with its own device, as backboardd's is made.
 *   chl-selftest [PENCILKIT_METALLIB]
 * 1. chl_selftest, hand-written AIR (chl_selftest.ll): 1000 threads in groups of 64. Every uint and every half result is checked.
 * 2. sdfPen_kernel, PencilKit's own AIR untouched, on 8 points of 4 steps: it must finish and write exactly vertices 0 to 115,
 *    every one finite, and leave the sentinel after them alone.
 * Each step prints before it runs, so a hang names the step. A GPU hang here voids every client's fences, the compositor's
 * too: the kernel's recovery count is read before and after, and a change fails the run. Exit 0 passed, 1 a check failed,
 * 2 setup failed, 3 the 60 s alarm fired.
 */
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "chl.h"
#include "chl_selftest_air.h"

#define PK_DEFAULT "/System/Library/Frameworks/PencilKit.framework/default.metallib"

/* the kernel's recovery count (2055: INFO.flags bit 5, count in bits 16-31), as bgmtl.m and the inject read it */
struct gpu_info { uint32_t abi_version, flags, gpu_id, chip_id; uint64_t va_start, va_size; uint32_t last_fence, abi_minor; };
extern int sweet_drm_info(int fd, struct gpu_info *out);
static int
recoveries(void)
{
   struct gpu_info in;
   memset(&in, 0, sizeof(in));
   if (sweet_drm_info(-1, &in) != 0 || !(in.flags & 0x20)) return -1;
   return (int)(in.flags >> 16);
}

static void
step(const char *s)
{
   printf("== %s\n", s);
   fflush(stdout);
}

static void
alarm_fired(int sig)
{
   (void)sig;
   static const char m[] = "FAIL timeout: 60 s without finishing (a GPU hang, or a compile that never ends)\n";
   write(1, m, sizeof(m) - 1);
   _exit(3);
}

static int failed;
static void
check(int ok, const char *what)
{
   printf("%s %s\n", ok ? "ok  " : "FAIL", what);
   fflush(stdout);
   if (!ok) failed = 1;
}

static float
half_to_float(uint16_t h)
{
   uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
   float v = e == 0 ? ldexpf((float)m, -24) : e == 31 ? (m ? NAN : INFINITY) : ldexpf((float)(m | 1024), (int)e - 25);
   return s ? -v : v;
}

/* one dispatch on its own command buffer, waited for */
static int
run(struct chl_device *dev, struct chl_compute_pipeline *p, struct chl_buffer **buf, unsigned nbuf, const void **bytes,
    const size_t *bytes_len, unsigned groups, unsigned threads)
{
   char why[512] = "";
   struct chl_cmdbuf *cb = chl_cmdbuf_create(dev, why, sizeof(why));
   if (!cb) { printf("FAIL command buffer: %s\n", why); return -1; }
   chl_compute_begin(cb);
   chl_set_compute_pipeline(cb, p);
   for (unsigned i = 0; i < nbuf; i++) {
      if (buf[i]) chl_set_buffer(cb, CHL_STAGE_COMPUTE, i, buf[i], 0);
      else if (bytes[i]) chl_set_bytes(cb, CHL_STAGE_COMPUTE, i, bytes[i], bytes_len[i]);
   }
   const unsigned g[3] = { groups, 1, 1 }, t[3] = { threads, 1, 1 };
   if (chl_dispatch(cb, g, t, why, sizeof(why)) != 0) { printf("FAIL dispatch: %s\n", why); chl_cmdbuf_release(cb); return -1; }
   chl_compute_end(cb);
   if (chl_cmdbuf_commit(cb, NULL, NULL, why, sizeof(why)) != 0) { printf("FAIL commit: %s\n", why); chl_cmdbuf_release(cb); return -1; }
   int rc = chl_cmdbuf_wait(cb, why, sizeof(why));
   if (rc != 0) printf("FAIL wait: %s\n", why);
   chl_cmdbuf_release(cb);
   return rc;
}

static int
trivial(struct chl_device *dev)
{
   enum { N = 1000, TG = 64 };
   char why[512] = "";
   step("trivial: pipeline");
   struct chl_function fn = { chl_selftest_bc, chl_selftest_bc_len, "chl_selftest", NULL, 0, NULL, 0 };
   struct chl_pipeline_info info;
   struct chl_compute_pipeline *p = chl_compute_pipeline_create(dev, &fn, &info, why, sizeof(why));
   if (!p) { printf("FAIL pipeline: %s\n", why); return -1; }
   printf("pipeline: buffers %#x, %u threads per group at most, width %u\n", info.buffers_used[0], info.max_threads_per_group,
          info.thread_execution_width);
   struct chl_buffer *in = chl_buffer_create(dev, N * 4, why, sizeof(why)), *out = chl_buffer_create(dev, (N + 64) * 4, why, sizeof(why)),
                     *outh = chl_buffer_create(dev, (N + 64) * 2, why, sizeof(why));
   if (!in || !out || !outh) { printf("FAIL buffers: %s\n", why); return -1; }
   uint32_t *pi = chl_buffer_contents(in), *po = chl_buffer_contents(out);
   uint16_t *ph = chl_buffer_contents(outh);
   for (unsigned i = 0; i < N; i++) pi[i] = i * 7 + 3;
   memset(po, 0xa5, (N + 64) * 4);
   memset(ph, 0xa5, (N + 64) * 2);
   const uint32_t u[2] = { N, 5 };
   struct chl_buffer *b[4] = { NULL, in, out, outh };
   const void *by[4] = { u, NULL, NULL, NULL };
   const size_t bl[4] = { sizeof(u), 0, 0, 0 };
   step("trivial: dispatch 16 x 64 threads for 1000");
   if (run(dev, p, b, 4, by, bl, (N + TG - 1) / TG, TG) != 0) return -1;
   unsigned bad = 0, badh = 0, first = ~0u;
   for (unsigned i = 0; i < N; i++) {
      if (po[i] != (i * 7 + 3) * 5 + i) { bad++; if (first == ~0u) first = i; }
      if (half_to_float(ph[i]) != (float)i) badh++;   /* exact: every integer below 2048 is a half */
   }
   unsigned guard = 0;
   for (unsigned i = N; i < N + 64; i++) guard += po[i] != 0xa5a5a5a5u || ph[i] != 0xa5a5;
   char line[160];
   snprintf(line, sizeof(line), "trivial: %u of %u uint results wrong (first %d, got %u)", bad, N, first == ~0u ? -1 : (int)first,
            first == ~0u ? 0 : po[first]);
   check(!bad, line);
   snprintf(line, sizeof(line), "trivial: %u of %u half results wrong (16-bit stores two to a word)", badh, N);
   check(!badh, line);
   snprintf(line, sizeof(line), "trivial: %u writes past the 1000 threads (threads 1000-1023 must stop at gid < n)", guard);
   check(!guard, line);
   chl_buffer_release(in); chl_buffer_release(out); chl_buffer_release(outh);
   chl_compute_pipeline_release(p);
   return 0;
}

/* a module of an MTLB container, as plans/appmetal/metallib-list.py reads it */
static const void *
metallib_function(const uint8_t *b, size_t n, const char *want, size_t *size)
{
   if (n < 0x50 || memcmp(b, "MTLB", 4)) return NULL;
   uint64_t funcs, bc;
   memcpy(&funcs, b + 0x18, 8);
   memcpy(&bc, b + 0x48, 8);
   if (funcs + 4 > n) return NULL;
   uint32_t count;
   memcpy(&count, b + funcs, 4);
   size_t p = funcs + 4;
   for (uint32_t i = 0; i < count && p + 4 <= n; i++) {
      uint32_t len;
      memcpy(&len, b + p, 4);
      size_t q = p + 4, end = p + len;
      const char *name = NULL;
      uint64_t off = 0, sz = 0;
      while (q + 6 <= end && end <= n && memcmp(b + q, "ENDT", 4)) {
         uint16_t vl;
         memcpy(&vl, b + q + 4, 2);
         const uint8_t *v = b + q + 6;
         if (!memcmp(b + q, "NAME", 4)) name = (const char *)v;
         else if (!memcmp(b + q, "OFFT", 4) && vl >= 24) memcpy(&off, v + 16, 8);
         else if (!memcmp(b + q, "MDSZ", 4) && vl >= 8) memcpy(&sz, v, 8);
         q += 6 + vl;
      }
      if (name && !strcmp(name, want) && bc + off + sz <= n) {
         *size = sz;
         return b + bc + off;
      }
      p = end;
   }
   return NULL;
}

/* PencilKit's PKMetalSDFPenStrokePoint (24 bytes) and PKMetalSDFStrokeVertex (36 bytes), from the AIR's struct_type_info */
struct pk_point { float x, y, radius; uint16_t rotation, aspect; uint32_t steps_dest, pad; };   /* float2 aligns it to 8 */
_Static_assert(sizeof(struct pk_point) == 24, "PKMetalSDFPenStrokePoint");
enum { PK_VERTEX = 36 };

static int
pencilkit(struct chl_device *dev, const char *path)
{
   char why[512] = "", line[200];
   step("pencilkit: sdfPen_kernel from the metallib");
   int fd = open(path, O_RDONLY);
   struct stat st;
   if (fd < 0 || fstat(fd, &st) != 0) { printf("FAIL cannot open %s\n", path); return -1; }
   const uint8_t *m = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
   close(fd);
   size_t size = 0;
   const void *air = m == MAP_FAILED ? NULL : metallib_function(m, st.st_size, "sdfPen_kernel", &size);
   if (!air) { printf("FAIL no sdfPen_kernel in %s\n", path); return -1; }
   printf("sdfPen_kernel: %zu bytes of AIR\n", size);
   struct chl_function fn = { air, size, "sdfPen_kernel", NULL, 0, NULL, 0 };
   struct chl_pipeline_info info;
   step("pencilkit: pipeline");
   struct chl_compute_pipeline *p = chl_compute_pipeline_create(dev, &fn, &info, why, sizeof(why));
   if (!p) { printf("FAIL pipeline: %s\n", why); return -1; }
   /* 8 points, 4 steps each; gid 0..6 run (gid + 1 < n). gid 0 writes a circle quad and 4 tangent quads, 20 vertices; the
    * others 16 each from their destination index: 20 + 6 * 16 = 116 vertices */
   enum { NP = 8, STEPS = 4, NV = 116, CAP = 256 };
   struct chl_buffer *pts = chl_buffer_create(dev, (NP + 4) * sizeof(struct pk_point), why, sizeof(why)),
                     *verts = chl_buffer_create(dev, CAP * PK_VERTEX, why, sizeof(why));
   if (!pts || !verts) { printf("FAIL buffers: %s\n", why); return -1; }
   struct pk_point *pp = chl_buffer_contents(pts);
   memset(pp, 0, (NP + 4) * sizeof(*pp));
   for (unsigned i = 0; i < NP; i++) {
      unsigned dest = i == 0 ? 0 : 20 + (i - 1) * 16;
      pp[i] = (struct pk_point){ 40.0f + 30.0f * i, 300.0f + 5.0f * (i & 1), 6.0f, 0, 0x3c00 /* 1.0 */, STEPS | dest << 16, 0 };
   }
   uint8_t *vv = chl_buffer_contents(verts);
   memset(vv, 0xff, CAP * PK_VERTEX);
   const uint32_t u[2] = { 0, NP };
   const float aa = 1.0f;
   struct chl_buffer *b[4] = { NULL, pts, verts, NULL };
   const void *by[4] = { u, NULL, NULL, &aa };
   const size_t bl[4] = { sizeof(u), 0, 0, sizeof(aa) };
   step("pencilkit: dispatch 1 x 64 threads for 8 points");
   if (run(dev, p, b, 4, by, bl, 1, 64) != 0) return -1;
   unsigned untouched = 0, nonfinite = 0, past = 0;
   for (unsigned v = 0; v < CAP; v++) {
      const uint8_t *e = vv + v * PK_VERTEX;
      int all_ff = 1;
      for (unsigned k = 0; k < PK_VERTEX; k++) all_ff &= e[k] == 0xff;
      if (v >= NV) { past += !all_ff; continue; }
      if (all_ff) { untouched++; continue; }
      float f[6];
      memcpy(f, e, sizeof(f));
      for (unsigned k = 0; k < 6; k++) nonfinite += !isfinite(f[k]);
      for (unsigned k = 0; k < 6; k++) {
         uint16_t h;
         memcpy(&h, e + 24 + 2 * k, 2);
         nonfinite += !isfinite(half_to_float(h));
      }
   }
   const float *v0 = (const float *)vv;
   printf("vertex 0: location %.2f %.2f, segment %.2f %.2f to %.2f %.2f\n", v0[0], v0[1], v0[2], v0[3], v0[4], v0[5]);
   snprintf(line, sizeof(line), "pencilkit: %u of %u vertices never written", untouched, NV);
   check(!untouched, line);
   snprintf(line, sizeof(line), "pencilkit: %u non-finite values in the written vertices", nonfinite);
   check(!nonfinite, line);
   snprintf(line, sizeof(line), "pencilkit: %u vertices written past index %u", past, NV - 1);
   check(!past, line);
   chl_buffer_release(pts); chl_buffer_release(verts);
   chl_compute_pipeline_release(p);
   munmap((void *)m, st.st_size);
   return 0;
}

int
main(int argc, char **argv)
{
   setvbuf(stdout, NULL, _IOLBF, 0);
   signal(SIGALRM, alarm_fired);
   alarm(60);
   /* what bgl-launch gives backboardd and the inject gives the trial app, except the cache: none, so nothing is shared */
   setenv("IR3_SHADER_DEBUG", "nofp16", 0);
   setenv("MESA_SHADER_CACHE_DISABLE", "true", 0);
   setenv("CHL_FETCH_SYNC", "0", 0);
   step("device");
   int before = recoveries();
   printf("recoveries before: %d\n", before);
   char why[512] = "";
   struct chl_device *dev = chl_device_open(why, sizeof(why));
   if (!dev) { printf("FAIL no device: %s\n", why); return 2; }
   printf("device: %s, TU_DEBUG=%s\n", chl_device_name(dev), getenv("TU_DEBUG") ? getenv("TU_DEBUG") : "-");
   int rc = trivial(dev);
   if (rc == 0 && !failed) rc = pencilkit(dev, argc > 1 ? argv[1] : PK_DEFAULT);
   else printf("skip pencilkit: the trivial kernel did not pass\n");
   int after = recoveries();
   printf("recoveries after: %d\n", after);
   check(before == after, "no GPU recovery during the run");
   chl_device_close(dev);
   printf("RESULT %s\n", rc != 0 ? "SETUP-FAILED" : failed ? "FAILED" : "PASSED");
   return rc != 0 ? 2 : failed ? 1 : 0;
}
