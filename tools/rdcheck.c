#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
typedef uint32_t u32; typedef uint64_t u64; typedef uint8_t u8;
#define pr_warn_ratelimited(...) printf(__VA_ARGS__)
static int msm_validate = 1, msm_rejects;
struct xe_range { u32 start, end; void *vaddr; };

static bool mmio_ok(u32 reg)
{
	/* dword register indices */
	if (reg >= 0x0578 && reg <= 0x057f)	/* SCRATCH_REG0..7 (fence uses 5) */
		return true;
	if (reg >= 0x2000 && reg < 0x4000)	/* 3D context registers (also reachable via SET_CONSTANT) */
		return true;
	/* ALU (0x4000) and fetch (0x4800) constants, as SET_CONSTANT also allows.
	 * TODO: fetch constants carry texture/vertex base addresses that are not yet checked. */
	if (reg >= 0x4000 && reg < 0x4a00)
		return true;
	if (reg >= 0x5000 && reg <= 0x5002)	/* SHADER_CONSTANT_FLUSH_FETCH_* */
		return true;
	switch (reg) {
	case 0x05c8:	/* WAIT_UNTIL */
	case 0x0d02:	/* SQ_INST_STORE_MANAGMENT */
	case 0x0d00: case 0x0d01: case 0x0d04:	/* SQ setup used by libxenon */
	case 0x0a2f: case 0x0a30: case 0x0a31:	/* COHER_SIZE/BASE/STATUS_HOST (cache flush), used by libxenon */
	case 0x0c85: case 0x0e42:	/* libxenon init */
		return true;
	}
	return false;
}

static bool op_ok(u32 op)
{
	/* values from Xenia's xenos.h PM4 list. Deliberately absent: REG_RMW (0x21, arbitrary
	 * register writes), WAIT_REG_EQ/WAIT_REG_MEM (0x52/0x3c, can wait forever), ME_INIT (0x48,
	 * kernel's job), LOAD_CONSTANT_CONTEXT (0x2e, reads unchecked memory), DRAW_INDX_BIN and
	 * SET_BIN_BASE_OFFSET (0x34/0x4b, a2xx binning), INTERRUPT (0x54). */
	switch (op) {
	case 0x10:	/* NOP */
	case 0x22:	/* DRAW_INDX */
	case 0x36:	/* DRAW_INDX_2 */
	case 0x26:	/* WAIT_FOR_IDLE */
	case 0x27:	/* IM_LOAD (address checked) */
	case 0x2b:	/* IM_LOAD_IMMEDIATE */
	case 0x2d:	/* SET_CONSTANT */
	case 0x3b:	/* INVALIDATE_STATE */
	case 0x3d:	/* MEM_WRITE (destination checked) */
	case 0x3f:	/* INDIRECT_BUFFER (target checked) */
	case 0x37:	/* INDIRECT_BUFFER_PFD (target checked) */
	case 0x46:	/* EVENT_WRITE (memory form checked) */
	case 0x56:	/* SET_SHADER_CONSTANTS */
	case 0x4a:	/* SET_SHADER_BASES */
	/* 0x58/0x59 EVENT_WRITE_SHD/_CFL refused: CACHE_FLUSH_TS through it wedged the memory hub (26 Oct) */
		return true;
	}
	return false;
}

/* 3D context registers the checker follows to bound resolve (EDRAM -> memory) writes */
struct xe_cstate {
	u32 modecontrol, copy_base, copy_pitch, copy_info;
};

static void xe_track(struct xe_cstate *st, u32 reg, u32 v)
{
	switch (reg) {
	case 0x2208: st->modecontrol = v; break;	/* RB_MODECONTROL */
	case 0x2319: st->copy_base = v; break;		/* RB_COPY_DEST_BASE */
	case 0x231a: st->copy_pitch = v; break;		/* RB_COPY_DEST_PITCH: pitch | height << 16 */
	case 0x231b: st->copy_info = v; break;		/* RB_COPY_DEST_INFO */
	}
}

static bool in_ranges(const struct xe_range *r, int n, u32 addr, u32 len, void **vaddr);

/* a draw in EDRAM copy mode writes pitch x height pixels at copy_base */
static bool xe_resolve_ok(const struct xe_range *r, int n, const struct xe_cstate *st)
{
	u32 pitch = st->copy_pitch & 0x3fff, height = (st->copy_pitch >> 16) & 0x3fff;
	u32 fmt = (st->copy_info >> 7) & 0x3f;
	u32 bpp = (fmt == 6) ? 4 : 16;	/* k_8_8_8_8; anything else gets the worst case */
	/* tiled destinations are padded to 32x32 */
	u32 h = (height + 31) & ~31u, p = (pitch + 31) & ~31u;
	return in_ranges(r, n, st->copy_base & ~0xfffu, p * h * bpp, NULL);
}

static bool in_ranges(const struct xe_range *r, int n, u32 addr, u32 len, void **vaddr)
{
	int i;
	for (i = 0; i < n; i++)
		if (addr >= r[i].start && (u64)addr + len <= r[i].end) {
			if (vaddr)
				*vaddr = r[i].vaddr ? (u8 *)r[i].vaddr + (addr - r[i].start) : NULL;
			return true;
		}
	return false;
}

static int xe_check_stream(const struct xe_range *r, int n, const u32 *w, u32 ndw, int depth,
			   struct xe_cstate *st)
{
	u32 k = 0;

	if (depth > 2)
		return -EINVAL;
	while (k < ndw) {
		u32 h = w[k], type = h >> 30;
		if (type == 2) {
			k++;
			continue;
		}
		if (type == 0) {
			u32 cnt = ((h >> 16) & 0x3fff) + 1, reg = h & 0x7fff, j;
			bool one = h & (1 << 15);
			for (j = 0; j < cnt; j++)
				xe_track(st, one ? reg : reg + j, w[k + 1 + j]);
			for (j = 0; j < (one ? 1 : cnt); j++)
				if (!mmio_ok(reg + j)) {
					pr_warn_ratelimited("xegpu: submit rejected: MMIO write to reg 0x%04x (byte 0x%04x)\n",
							    reg + j, (reg + j) * 4);
					return -EPERM;
				}
			k += 1 + cnt;
			continue;
		}
		if (type == 3) {
			u32 cnt = ((h >> 16) & 0x3fff) + 1, op = (h >> 8) & 0x7f;
			if (k + 1 + cnt > ndw)
				return -EINVAL;
			/* WAIT_REG_MEM is allowed only in libxenon's form: wait on COHER_STATUS_HOST (0x0a31),
			 * which always completes once the cache flush it follows is done */
			if (op == 0x3c && !(cnt == 5 && (w[k + 1] & 0x10) == 0 && w[k + 2] == 0x0a31)) {
				pr_warn_ratelimited("xegpu: submit rejected: WAIT_REG_MEM on %08x\n", w[k + 2]);
				return -EPERM;
			}
			if (op != 0x3c && !op_ok(op)) {
				pr_warn_ratelimited("xegpu: submit rejected: PM4 opcode 0x%02x\n", op);
				return -EPERM;
			}
			if (op == 0x3d) {	/* MEM_WRITE: addr, data... */
				if (!in_ranges(r, n, w[k + 1] & ~3u, (cnt - 1) * 4, NULL)) {
					pr_warn_ratelimited("xegpu: submit rejected: MEM_WRITE to %08x\n", w[k + 1]);
					return -EPERM;
				}
			}
			if ((op == 0x46 || op == 0x58 || op == 0x59) && cnt >= 2) {	/* event + address [+ data] */
				if (!in_ranges(r, n, w[k + 2] & ~3u, 4, NULL)) {
					pr_warn_ratelimited("xegpu: submit rejected: event write to %08x\n", w[k + 2]);
					return -EPERM;
				}
			}
			if (op == 0x27) {	/* IM_LOAD: addr | shader type, size in dwords (libxenon Xe_pLoadShader) */
				if (cnt < 2 || !in_ranges(r, n, w[k + 1] & ~3u, w[k + 2] * 4, NULL)) {
					pr_warn_ratelimited("xegpu: submit rejected: IM_LOAD from %08x\n", w[k + 1]);
					return -EPERM;
				}
			}
			if (op == 0x2d && cnt >= 2 && (w[k + 1] >> 16) == 4) {	/* SET_CONSTANT: registers */
				u32 base = 0x2000 + (w[k + 1] & 0x7ff), j;
				for (j = 0; j + 1 < cnt; j++)
					xe_track(st, base + j, w[k + 2 + j]);
			}
			if ((op == 0x22 || op == 0x36) && (st->modecontrol & 7) == 6) {	/* draw in copy mode */
				if (!xe_resolve_ok(r, n, st)) {
					pr_warn_ratelimited("xegpu: submit rejected: resolve to %08x (pitch/height %08x) outside the submit\n",
							    st->copy_base, st->copy_pitch);
					return -EPERM;
				}
			}
			if (op == 0x3f || op == 0x37) {	/* IB: addr, size */
				void *v;
				u32 addr = w[k + 1], size = w[k + 2] & 0xfffff;
				int rc;
				if (!in_ranges(r, n, addr, size * 4, &v) || !v) {
					pr_warn_ratelimited("xegpu: submit rejected: IB at %08x outside the submit\n", addr);
					return -EPERM;
				}
				rc = xe_check_stream(r, n, v, size, depth + 1, st);
				if (rc)
					return rc;
			}
			k += 1 + cnt;
			continue;
		}
		pr_warn_ratelimited("xegpu: submit rejected: type1 packet %08x\n", h);
		return -EPERM;
	}
	return 0;
}


#include <stdlib.h>
#include <string.h>
#include <zlib.h>
/* load an rd dump (BE ppc64 capture): buffers + cmdstream addrs */
int main(int argc, char **argv) {
	gzFile g = gzopen(argv[1], "rb"); static unsigned char d[4<<20]; int len = gzread(g, d, sizeof d); gzclose(g);
	struct xe_range r[64]; int n = 0; u32 pend = 0; int havep = 0; u32 cs[16][2]; int ncs = 0;
	for (int i = 0; i + 8 <= len;) {
		u32 t = __builtin_bswap32(*(u32*)(d+i)), sz = __builtin_bswap32(*(u32*)(d+i+4)); i += 8;
		if (t == 3) { pend = __builtin_bswap32(*(u32*)(d+i)); havep = 1; }
		else if (t == 12 && havep) { u32 *w = malloc(sz); for (u32 k = 0; k < sz/4; k++) w[k] = __builtin_bswap32(((u32*)(d+i))[k]);
			r[n].start = pend; r[n].end = pend + sz; r[n].vaddr = w; n++; havep = 0; }
		else if (t == 6) { cs[ncs][0] = __builtin_bswap32(*(u32*)(d+i)); cs[ncs][1] = __builtin_bswap32(*(u32*)(d+i+4)); ncs++; }
		i += sz;
	}
	for (int c = 0; c < ncs; c++) { void *v; if (!in_ranges(r, n, cs[c][0], cs[c][1]*4, &v)) { printf("cmd outside\n"); continue; }
		struct xe_cstate st = {0}; int rc = xe_check_stream(r, n, v, cs[c][1], 0, &st); printf("%s cmdstream %08x: %s\n", argv[1], cs[c][0], rc ? "REJECTED" : "accepted"); }
	return 0;
}
