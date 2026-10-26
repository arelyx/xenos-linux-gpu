# Methodology: debugging a GPU you can't reset

The Xenos has no working software reset, and it reads physical memory with no MMU. A bad command stream either wedges the GPU until power is cut or writes over random memory and takes the console down. Every experiment therefore costs a power cycle, and early on a power cycle meant a person pressing buttons. Most of the work went into making experiments cheap, isolated and attributable. This is what ended up working, roughly in the order we built it.

## 1. Make power cycles free

- **IR blaster.** Arduino Uno and a KY-005 IR LED on pin 3 (`irblaster/xbox_ir`). It sends RC6 mode-6 codes: the Xbox remote (`0x800F74xx`/`0x800FF4xx` with the toggle bit) and the Windows Media Center remote. `irblaster/xir` talks to it over USB serial. IR power-off works even when the GPU has frozen everything else, because the SMC handles it.
- **Boot straight into Linux.** IR power-on boots the normal dashboard, since Peer Pressure only boots Xell for the physical eject button (power-on reason `0x12`). The SMC reports Media Center IR power as reason `0x31`. Peer Pressure's `KernelExpansion.bin` compares the reason with `cmplwi cr6,r11,0x12`; changing that immediate to `0x31` makes the Media Center power button boot Xell, which netboots Linux. One byte in a file on the HDD, no NAND writes. `softmod/fatxcfg.py` edits FATX files in place from Linux.
- **`irblaster/xbox-cycle`.** Power off, power on with the MCE code, wait for SSH and the GPU service, retry up to five times (sometimes it lands in Aurora first). About 80 seconds per cycle. It takes a lock: two cycle scripts pressing buttons at the same time undo each other (see footguns).

## 2. Isolate every experiment

- One experiment per boot when the question is "does this hang". State left by an earlier stream changed outcomes several times (vertex cache, GPU state init).
- `irblaster/fresh-steps a.xrp b.xrp`: power-cycle before each step, run it, record "fence ok" or "timeout".
- The kernel counts `fence_timeouts` and `fence_stalls` (no fence progress for 5 s, tracked globally so waits that restart on signals still count). Harnesses treat any nonzero value as "power-cycle before the next test".

## 3. Replay and bisect command streams

- freedreno writes `.rd` dumps (`FD_RD_DUMP=enable,full`; full mode is needed for buffer contents). A patch in our Mesa series numbers dump files per device instance, since dEQP opens several devices per process.
- `tools/rdstep.py dump.rd out.xrp edit1,edit2`: turns the last command buffer of a dump into a relocatable blob with named edits: drop indirect buffers, drop opcodes, replace or set registers (`rep2201=0x10001`, `set2200=0`), swap in libxenon's resolve, keep the first N packets, and more. `userspace/tests/xereplay` runs a blob through the raw xegpu submit.
- `tools/libxreplay.py`: libxenon streams recorded under qemu with a fake GPU backend (`userspace/fake/xe_fake.c`), so we could compare libxenon's own packets with freedreno's.
- **Morphing.** When freedreno's clear hung and libxenon's cube worked, we changed the cube one property at a time toward the clear (its pixel shader, its vertex shader, its register set, its fetch slot...) and ran each step on a fresh boot. The step that broke named the cause (the unexported VS parameter).
- **Guard areas.** Put the resolve destination in the middle of 2 MB of `0xdeadbeef` and see which words change. That showed resolves writing nowhere after a 2-dword fetch-constant write.

Limits: replays drift from the live system. Texture fetch constants aren't relocated, only one command buffer is replayed, and the replay starts from libxenon state. Twice a stream passed in replay and hung live, and the difference was state set up by earlier work (the missing state init, the GPR split). When replay and live disagree, stop replaying and look for state.

## 4. Check streams against Xenia's documentation

Xenia's `register_table.inc`, `registers.h` (bitfield layouts), `xenos.h` (PM4 opcodes, enums) and `ucode.h` (shader ISA notes) are the closest thing to a Xenos manual.

- `tools/xenoslint.py dumps...`: registers and opcodes used, registers Xenia doesn't know. `--diff A... -- B...` lists what B uses that A never did: opcodes, registers, and values. Diffing a hanging submit against every passing stream narrows the search to a handful of registers.
- `tools/xenosfields.py`: decodes each written value with Xenia's bitfields and flags bits set in padding.
- `tools/statediff.py`, `tools/rddecode.py`: register state at a draw, PM4 decode.

## 5. Test with a conformance suite, not just hand-written tests

Hand-written tests only check what you already thought of. Khronos dEQP-GLES2 has 17,165 must-pass cases and found classes we would not have guessed: index DMA units, line loops, small render targets, register pressure, a CPU instruction the Xenon lacks.

`conformance/deqp-run.py caselist outdir`:

- runs batches of 100 cases over SSH; the console reads the binary and data from its NFS root, and results come back through NFS;
- `run-watched.sh` on the console polls the kernel's fence counters while dEQP runs, kills it at the first missed fence and names the running case;
- on a hang it records the case as Hang, power-cycles over IR, and re-queues the rest of the batch;
- crashes, timeouts and "no results" are recorded too; five empty batches in a row stop the run (that's a setup problem, not a GPU one);
- `--skip known-hangs.txt` skips classes that are known to wedge the GPU, so one class doesn't cost a power cycle per case;
- `report.py run [older]` prints per-group pass rates and fixed/regressed cases.

Run a 1-in-20 sample of the list first (859 cases, about 20 minutes) to map the failure classes, fix the big ones, then run everything. Confirm each hang class by running one case alone on a fresh boot: attribution can be one case late.

## 6. Write the smallest probe that tests one hypothesis

`gltest/` holds small GLES programs, one question each: `glgrad` (do varyings interpolate, do byte attributes survive), `glsub` (viewport/scissor/readback offsets), `glpb` (pbuffer readback through the CPU path), `glmip` (render to each mip size), `glregs` (pixel-shader register pressure), `gltexsub`, `glsten`, `glpoint`, `glfs`, `glgbm` (glamor's front-buffer path). `conformance/seq.sh` runs a list of them, power-cycling after any hang.

When a dEQP class hangs, write a probe for the suspected feature. If the probe passes, the hypothesis is wrong; don't keep fixing it. `glfs` and `glsten` passing while dEQP hung was what sent us looking for register pressure instead of control flow.

## 7. For X, bisect clients, not frames

`conformance/xg-one.sh "<client>"` starts a fresh Xorg with glamor and one client, then reports fence stalls. xclock hung and `xclock -norender` didn't, so the problem was in Render composites. `FD_MESA_DEBUG=disasm` then gave the exact shader. Reading the scanout back (`tools/grab.py` from `/dev/mem`, detiled with the tiling formula) shows what the TV shows, independent of X's own readback.

## 8. Hygiene

- Log every result in a running file with the time (we used `PROGRESS.md`; [timeline.md](timeline.md) is the cleaned-up version).
- One background job at a time. Track it. Stop it before starting another.
- Commit after each fix, with the evidence in the message.
- When two attempts at the same idea fail, stop and read: Xenia's sources, libxenon, Mesa's CI configs, mailing lists. Several fixes here came from one line of Xenia source.
