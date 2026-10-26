# Xenos hardware rules

Xenos (the Xbox 360 GPU, ATI C1) and the Qualcomm Adreno 200 (AMD Z430) come from the same R400 design. The shader ISA is bit-identical and most 3D registers share addresses, which is why Mesa's freedreno a2xx driver was the right base. The differences below are the ones that broke it. Almost every one of them shows up as "the GPU stops and never comes back", with no error code, so each rule comes with how we found it.

Register and packet names follow Xenia's `src/xenia/gpu/register_table.inc` and `xenos.h`. "libxenon" is Free60's homebrew GPU library, which is the only working Xenos driver we had to compare against.

Notation: "wedged" means the CP stops consuming the ring, fences never pass, `RBBM_STATUS` reads something like `0x80050110` (GUI active, memory hub busy) and only a power cycle helps. "Off the bus" means MMIO reads return `0xffffffff` and the whole console freezes.

## State setup

### 1. The GPU needs libxenon's `Xe_Init` stream after every CP initialisation

Symptom: every GL program, dEQP and Xorg wedged the GPU on its first submit. glsmoke had passed earlier only because those runs left the display's GPU blit on, and the blit replays libxenon's recorded `Xe_Init` stream before its first copy.

Found by: diffing the dEQP and glsmoke first submits (identical register sets), then noticing that the old test script never set `gpu_blit=0`.

Fix: `xenos_gpu_state_init()` in `kernel/xegpu/xenos_drm.c` runs the recorded stream before the first msm submit after every CP init. This also fixed the "first clear of a process misses x<24" bug.

### 2. Pixel shaders get 16 GPRs unless you change `SQ_GPR_MANAGEMENT`

Symptom: glamor's composite shader (23 registers) wedged the GPU on the first anti-aliased draw; dEQP's fragment dynamic-indexing tests hung; plain fragment loops passed.

Found by: running X clients one at a time (xclock hangs, `xclock -norender` doesn't), dumping glamor's shaders with `FD_MESA_DEBUG=disasm`, then `gltest/glregs`, which keeps N vec4 temporaries live. R15 works, R16 hangs.

Cause: `Xe_Init` ends with `SQ_GPR_MANAGEMENT = 0x70100`: `REG_SIZE_PIX` (bits 10:4) = 16, `REG_SIZE_VTX` (bits 18:12) = 112.

Fix: after the init stream, the kernel writes a 64/64 split the same way `Xe_Init` does: `WAIT_UNTIL` idle (`0x05c8 = 0x20000`), a type-0 packet with the one-register bit (`0x00078d00`) writing the value with bit 0 set eight times, then the final value. A direct MMIO write to `0x0d00` had no visible effect. Parameters `sq_gpr_pix`/`sq_gpr_vtx`.

### 3. ALU constants: whole 16-constant blocks

Writing three constants (12 dwords) wedges the GPU, whatever the values or alignment. Writing the same constants inside a 64-dword (16-constant) block works. libxenon always uploads whole blocks.

Fix: Mesa keeps a shadow of the constant file per context and uploads whole blocks with type-0 packets (`fd2_emit_alu_consts`).

### 4. Fetch constants: whole 6-dword groups

freedreno wrote 2-dword vertex fetch constants. That didn't hang; it made later resolves write nowhere (found with guard areas, see methodology). Writing whole 6-dword groups fixed it. Vertex fetch constants also carry bit 28 of dword 1 (`0x1000001a` in libxenon), which Xenia calls padding; we set it like libxenon.

### 5. Partial fetch/constant packets need alignment

libxenon pads command streams so constant data starts 16-byte aligned (`Xe_pAlign`). We do the same (`fd2_xenos_align`). We never proved this one is required on its own; it costs nothing.

## Shaders

### 6. The vertex shader must export every parameter it allocates, position first

freedreno's solid-color vertex shader allocated an interpolator and never exported it. Adreno tolerates that; Xenos waits forever. Found by morphing libxenon's working cube draw one step at a time toward freedreno's clear draw until it broke (m4 hangs, m6 with libxenon's CF order works).

Fix: on Xenos, a VS without fragment inputs exports position, then writes a dummy parameter to slot 0; the parameter allocation size matches the exports.

### 7. Vertex fetch fields

- Stride and offset are in dwords. freedreno wrote bytes, so the fetch read four times past the vertex buffer.
- Fetch instruction "reserved0" is `prefetch_count` and "reserved3" is `is_mini_fetch`. libxenon uses 0 for both. freedreno wrote 3 into prefetch_count.
- `SQ_PROGRAM_CNTL` register counts are 6-bit fields. freedreno's "0x80 = no registers" lands in padding.

### 8. One endian swap per vertex buffer

The fetch constant's endian field covers the whole buffer. A big-endian host needs 8in32 for floats, which then reverses byte attributes (we saw RGBA come out as ABGR). Xenos now only accepts vertex formats with 32-bit components, and Mesa's u_vbuf converts the rest.

## Drawing

### 9. Index buffers

- `VGT_DRAW_INITIATOR` has a 1-bit index size (16 or 32 bits). There are no 8-bit indices; freedreno's 8-bit encoding sets bit 13, which is padding on Xenos. Mesa now reports `R8_UINT` index buffers unsupported.
- `VGT_DMA_SIZE` counts 16-bit words (libxenon: `count << indextype`), not bytes. With bytes, every indexed draw read twice the index buffer, and dEQP's buffer-write tests wedged the GPU.

### 10. Primitive types

Xenos type 7 is "triangle with W flags", not a line loop (that's 0x0c). freedreno's a22x table maps `GL_LINE_LOOP` to 7, which hung `dEQP...draw.random.49`. Xenos gets its own table without line loops; Mesa converts them to strips.

### 11. Render targets smaller than 8x8 wedge the GPU

`gltest/glmip`: clearing a 4x4, 2x2 or 1x1 render target (or a mip level that size) hangs; 8x8 works. This broke `glGenerateMipmap`, `glCopyTexImage2D` into small levels and stencil-only FBOs. Xenia rounds resolves to 8 pixels (`kResolveAlignmentPixels`), which pointed the way, but rounding only the resolve rectangle wasn't enough. The tile size, screen scissor and window scissor must be at least 8x8 too. Mip levels are padded to 32x32 in memory, so the extra pixels stay inside the allocation.

### 12. No copy-destination offset

Xenos has no `RB_COPY_DEST_OFFSET`. A bin that doesn't start at (0,0) resolves to the top-left of the destination. glamor's partial updates all landed in the corner until Xenos always used one whole-framebuffer bin.

### 13. Blend

- There is no blend-disable bit. "Blend off" is `RB_BLENDCONTROL = 0x00010001` (ONE, ZERO), as in libxenon. freedreno's 0 meant ZERO/ZERO, which draws black.
- `RB_BLEND_RED..ALPHA` are floats on Xenos; Adreno takes bytes.
- Three A220-only registers sit on Xenos MRT blend controls: `0x2209` (A220 LRZ_VSC_CONTROL) is `RB_BLENDCONTROL1`, `0x220a` (SAMPLE_POS) is `RB_BLENDCONTROL2`, `0x220b` (CLEAR_COLOR) is `RB_BLENDCONTROL3`. Xenos clears take the a20x path: clear color through a shader constant.

## Memory, caches and resolves

### 14. `COHER_STATUS_HOST` bit 25 invalidates the vertex cache

libxenon invalidates with `0x01000000`, which only covers the texture cache. With it, the first draw of a process rendered and every later draw used stale vertices. `0x03000000` fixed it (found with an `XENOS_COHER` override on hardware). Mesa invalidates each vertex buffer range before draws and the whole of memory around clears.

The all-of-DRAM form (size `0x20000000`, base 0) doesn't reliably reach the texture cache either. With it before every draw, a cube texture the CPU had written into a recycled buffer sampled stale texels, but only after other dEQP cases had run. Invalidating the texture's exact range fixed it. libxenon only uses the all-of-DRAM form once at init. Mesa now invalidates exact ranges: vertex and index buffers before every draw, textures and restored render targets after the CPU writes them or they get a new buffer.

### 15. Resolves always write the tiled layout

Every resolve writes the 32x32-macro-tiled layout (`XGAddress2DTiledOffset`), whatever the destination is used for. freedreno's a2xx never enabled tiling, so readbacks came back scrambled. Xenos render targets are always tiled, the CPU (de)tiles on map/unmap, and restores sample with the tiled bit set.

### 16. Resolve byte order

With `RB_COPY_DEST_INFO` endian 0, swap 0 writes R,G,B,A bytes for an RGBA target, and swap 1 writes B,G,R,A. Endian 2 reverses the bytes of each dword (8in32) and endian 3 swaps its halves (16in32), as Xenia's `Endian128` says. A8R8G8B8 is endian 2, swap 1. We got this wrong once because the dEQP build had little-endian reference images (see footguns).

### 17. Resolve follow-up

After each resolve: `EVENT_WRITE CACHE_FLUSH`, then `COHER_STATUS_HOST = 0x03000100` over the destination and `WAIT_REG_MEM` on `COHER_STATUS_HOST`. That's libxenon's `Xe_ResolveInto`. Without it the fence can pass before the pixels reach memory.

## Command processor

### 18. `EVENT_WRITE_SHD` with `CACHE_FLUSH_TS` wedges the memory hub

Our first Xenos fence used it. The GPU finished the draws and then sat with `RBBM_STATUS 0x80050110`. Fences are now `WAIT_UNTIL` idle (`0x05c8 = 0x20000`) plus `MEM_WRITE` of the fence value. `MEM_WRITE` stores the value little-endian. The kernel's command checker refuses opcodes `0x58`/`0x59`.

### 19. Indirect buffers nest two deep

Ring to IB to IB works. A third level wedges the GPU (libxenon's cube replayed with one more level: hang; inlined: fine).

### 20. Interrupts

`CP_INTERRUPT` with opcode `0x40` raises the fence interrupt on this console. Xenia's `0x54` raises nothing.

### 21. No software reset

libxenon's RBBM soft-reset sequence hung the whole console when tried. A wedged GPU needs a power cycle. Plan everything around that.

### 22. MMIO only with aligned 32-bit accesses

A Python `mmap` slice did byte loads from the register BAR and froze the console.

## Formats

- Texture fetch endian: 8in16 for packed 16-bit formats (565, 4444), 8in32 for 32-bit components, none for arrays of bytes (L8A8 is bytes; swapping it swapped L and A).
- The EDRAM restore texture must use the surface format's channel order (`fd2_tex_swiz`), not a fixed XYZW.
- Open: RGBA4444 textures currently sample like 565.

## CPU side (Xenon)

- The kernel's Xenon CPU table lacks AltiVec, so any vector instruction gets SIGILL from the vector-unavailable trap. Patched (`kernel/patches/0002-xenon-altivec.patch`).
- VMX128 lacks the AltiVec integer multiply and sum family (`vmulouh`, `vmsumuhm` and friends). GCC auto-vectorises integer code with them. pixman's VMX paths crash; use `PIXMAN_DISABLE=vmx`.
- The core lacks `ldbrx`/`stdbrx`. `-mcpu=cell` emits them for `__builtin_bswap64` (GCC bug 43871 describes the same thing on a G5).
- Use `-mcpu=970 -mtune=cell -mno-altivec` for user space. Ubuntu's ppc64 cross GCC defaults to POWER7, which SIGILLs immediately.
- An idle SMT sibling spins and steals about 45% of the core, because Xenon has no power-save hook. `kernel/xenon_idle` installs a low-priority idle loop.
- The SMC reports power-on reasons: physical eject `0x12`, Xbox remote IR `0x20`, Media Center remote IR power `0x31` (Free60 lists `0x24`; this SMC said `0x31`).
