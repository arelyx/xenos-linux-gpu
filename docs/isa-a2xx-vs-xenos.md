# Adreno 200 (Mesa a2xx / ir2) vs Xenos microcode: field-level comparison

Sources (local, read in full):
- `src/refs/instr-a2xx.h` (Mesa freedreno, referred to as **a2xx**)
- `src/refs/ucode.h` (Xenia, referred to as **Xenos**)
- `src/refs/xenos.h` (Xenia: formats, fetch constants, export limits)
- `src/linux-6.18.58/drivers/gpu/drm/msm/registers/adreno/a2xx.xml`, used only for
  `a2xx_sq_surfaceformat` and the SQ_TEX constant layout, because `instr-a2xx.h` includes
  `fd2_hw.h`, which is not in refs.

Claims about ir2's *source code* (file names, what it emits) are from memory of Mesa
and are marked **[unverified]**. No Mesa checkout exists on this machine and I did not browse the web.

Bit notation: `[lo:hi]` inclusive. CF positions are given within the 48-bit CF word;
ALU/fetch positions are given per dword (`dw0`/`dw1`/`dw2`).

---

## 1. Control flow

### 1.1 Packing
- Both use 48-bit CF instructions, packed two per 96-bit (3-dword) slot.
- Xenos `UnpackControlFlowInstructions`: CF0 = `dw0` + `dw1[0:15]`, and CF1 = `dw1[16:31]` + `dw2`
  (with `dw2 << 16`). The opcode is always in bits `[44:47]` (`opcode_value` after 32+12 bits).
- a2xx `instr_cf_t` is a `PACKED` 48-bit union (`dummy:44`, `opc:4`). Storing two of them back to back
  in little-endian memory gives exactly the Xenos dword split shown above.
- `exec.address` counts 96-bit slots from the start of the shader in both, and the ALU/fetch slots
  follow the CF slots.

### 1.2 Opcodes (4 bits, `[44:47]`): identical
| v | a2xx `instr_cf_opc_t` | Xenos `ControlFlowOpcode` |
|---|---|---|
| 0 | NOP | kNop |
| 1 / 2 | EXEC / EXEC_END | kExec / kExecEnd |
| 3 / 4 | COND_EXEC / COND_EXEC_END | kCondExec / kCondExecEnd |
| 5 / 6 | COND_PRED_EXEC / _END | kCondExecPred / kCondExecPredEnd |
| 7 / 8 | LOOP_START / LOOP_END | kLoopStart / kLoopEnd |
| 9 / 10 | COND_CALL / RETURN | kCondCall / kReturn |
| 11 | COND_JMP | kCondJmp |
| 12 | ALLOC | kAlloc |
| 13 / 14 | COND_EXEC_PRED_CLEAN / _END | kCondExecPredClean / _End |
| 15 | MARK_VS_FETCH_DONE | kMarkVsFetchDone |

Neither header has an opcode the other lacks. Xenia's notes add semantics that the hardware shares:
13/14 are bool-constant cexec with PredicateClean=1, and the `*_END` forms end only if the condition passes.
`AddressingMode` matches in both: 0 = relative (aL), 1 = absolute (a0), at bit `[43]`.

### 1.3 EXEC family
| bits | a2xx `instr_cf_exec_t` | Xenos `ControlFlow{Exec,CondExec,CondExecPred}Instruction` |
|---|---|---|
| [0:11] | `address:9` + `reserved0:3` | `address_:12` |
| [12:14] | `count:3` | `count_:3` |
| [15] | `yeild` | `is_yield_` |
| [16:27] | `serialize:12` (2 bits/instr) | `sequence_:12` ([0]=ALU0/fetch1, [1]=serialize) |
| [28:33] | `vc:6` | `vc_hi_:4` + `vc_lo_:2` |
| [34:41] | `bool_addr:8` | cond_exec: `bool_address_:8`; exec/cond_exec_pred: rsvd[34:40] + `is_predicate_clean_`[41] |
| [42] | `condition` | `condition_` (cond_exec, cond_exec_pred), rsvd in plain exec |
| [43] | `address_mode` | `address_mode_` |

Differences:
- The address is 12 bits on Xenos vs 9 bits on a2xx. a2xx keeps the top 3 bits reserved, so the layout matches.
- For EXEC and COND_PRED_EXEC, a2xx bit `bool_addr[7]` (bit 41) is Xenos `is_predicate_clean`.

### 1.4 LOOP_START / LOOP_END
| bits | a2xx `instr_cf_loop_t` | Xenos loop_start | Xenos loop_end |
|---|---|---|---|
| [0:12] | `address:10` + rsvd[10:12] | `address_:13` | `address_:13` |
| [13] | rsvd | `is_repeat_` | rsvd |
| [16:20] | `loop_id:5` | `loop_id_:5` | `loop_id_:5` |
| [21] | rsvd | rsvd | `is_predicated_break_` |
| [42] | rsvd | rsvd | `condition_` |

`loop_id` selects an integer constant (`xenos::LoopConstant`: `count:8`, `start:8`, `step:8`).

### 1.5 COND_CALL / COND_JMP / RETURN
| bits | a2xx `instr_cf_jmp_call_t` | Xenos cond_jmp / cond_call |
|---|---|---|
| [0:12] | `address:10` + `reserved0:3` | `address_:13` |
| [13] | `force_call` | `is_unconditional_` |
| [14] | `predicated_jmp` | `is_predicated_` |
| [33] | `direction` | `direction_` (jmp only, rsvd in call) |
| [34:41] | `bool_addr:8` | `bool_address_:8` |
| [42] | `condition` | `condition_` |

RETURN on Xenos has only `address_mode_` and the opcode.

### 1.6 ALLOC
| bits | a2xx `instr_cf_alloc_t` | Xenos `ControlFlowAllocInstruction` |
|---|---|---|
| [0:3] | `size:4` | `size_:3` [0:2] + rsvd [3] |
| [40] | `no_serial` | `is_unserialized_` |
| [41:42] | `buffer_select:2` | `alloc_type_:2` |
| [43] | `alloc_mode` | rsvd |

The alloc types match: 0 none, 1 position (`SQ_POSITION`/`kVsPosition`),
2 param/pixel (`SQ_PARAMETER_PIXEL`/`kVsInterpolators`=`kPsColors`), 3 memory (`SQ_MEMORY`/`kMemory`).
Xenos treats size bit 3 and `alloc_mode` as reserved. ir2 should write 0 to both.

---

## 2. ALU (96 bits, 3 dwords)

### 2.1 Layout: identical bit-for-bit
| dword | bits | a2xx `instr_alu_t` | Xenos `AluInstruction::Data` |
|---|---|---|---|
| dw0 | [0:5] | `vector_dest` | `vector_dest` |
| | [6] | `vector_dest_rel` | `vector_dest_rel` |
| | [7] | **`low_precision_16b_fp`** | **`abs_constants`** |
| | [8:13] | `scalar_dest` | `scalar_dest` |
| | [14] | `scalar_dest_rel` | `scalar_dest_rel` |
| | [15] | `export_data` | `export_data` |
| | [16:19] | `vector_write_mask` | same |
| | [20:23] | `scalar_write_mask` | same |
| | [24] / [25] | `vector_clamp` / `scalar_clamp` | same |
| | [26:31] | `scalar_opc:6` | `scalar_opc:6` |
| dw1 | [0:7] / [8:15] / [16:23] | `src3_swiz` / `src2_swiz` / `src1_swiz` | same |
| | [24] / [25] / [26] | `src3/2/1_reg_negate` | same |
| | [27:28] | `pred_select:2` | `pred_condition`[27], `is_predicated`[28] |
| | [29] | `relative_addr` | `const_address_register_relative` |
| | [30] / [31] | `const_1_rel_abs` / `const_0_rel_abs` | same |
| dw2 | [0:7] / [8:15] / [16:23] | `src3/2/1_reg` (6 reg + `_reg_select` + `_reg_abs`), or `_reg_byte:8` for consts | `src3/2/1_reg:8`. For temps, [0:5] index, [6] aL-relative, [7] abs |
| | [24:28] | `vector_opc:5` | `vector_opc:5` |
| | [29] / [30] / [31] | `src3/2/1_sel` | same (1 = temp, 0 = const) |

Notes:
- **dw0[7]** is the only bit the two headers name differently. Xenia's assembler-verified meaning is
  "abs applied to constant operands". Mesa's name looks like a guess. Emit 0 unless abs-const is wanted.
- `pred_select` = `(is_predicated << 1) | pred_condition`, so the same encoding.
- a2xx `src*_reg_select` (bit 6) is what Xenia calls the aL-relative flag for temps.
- Swizzles are 2 bits per component and **component-relative** (`GetSwizzledComponentIndex`:
  `((swz >> 2i) + i) & 3`), so 0x00 = .xyzw.
- Constants: an 8-bit index gives c0..c255 per operand. `const_0_rel_abs`/`const_1_rel_abs` select the
  addressing for the 1st/2nd *constant* operand, with "min(#prior const operands, 1)" logic (`src_const_is_addressed`).
  Bit `[29]` selects a0 over aL. Temps can only be aL-relative.
- Scalar operands: `a` = src3 component `(3 + swz[6:7]) & 3`, `b` = `(0 + swz[0:1]) & 3`.
  For MUL/ADD/SUB_CONST_0/1 the temp index is packed as `opc[0] | src3_sel<<1 | src3_swiz[2:5]`.
  The same hardware and the same opcodes are in a2xx, but only Xenia documents this packing.
- Export write masking uses the `export_data` + vector/scalar masks + `scalar_dest_rel` to produce
  constant 0/1 per component (`GetConstant0/1WriteMask`). The a2xx header does not document this, but it is the same field set.

### 2.2 Vector opcodes (5 bits, dw2[24:28]): identical values
| v | a2xx | Xenos | v | a2xx | Xenos |
|---|---|---|---|---|---|
| 0 | ADDv | kAdd | 16 | DOT3v | kDp3 |
| 1 | MULv | kMul | 17 | DOT2ADDv | kDp2Add |
| 2 | MAXv | kMax | 18 | CUBEv | kCube |
| 3 | MINv | kMin | 19 | MAX4v | kMax4 |
| 4 | SETEv | kSeq | 20 | PRED_SETE_PUSHv | kSetpEqPush |
| 5 | SETGTv | kSgt | 21 | PRED_SETNE_PUSHv | kSetpNePush |
| 6 | SETGTEv | kSge | 22 | PRED_SETGT_PUSHv | kSetpGtPush |
| 7 | SETNEv | kSne | 23 | PRED_SETGTE_PUSHv | kSetpGePush |
| 8 | FRACv | kFrc | 24 | KILLEv | kKillEq |
| 9 | TRUNCv | kTrunc | 25 | KILLGTv | kKillGt |
| 10 | FLOORv | kFloor | 26 | KILLGTEv | kKillGe |
| 11 | MULADDv | kMad | 27 | KILLNEv | kKillNe |
| 12 | CNDEv | kCndEq | 28 | DSTv | kDst |
| 13 | CNDGTEv | kCndGe | 29 | MOVAv | **kMaxA** (max + a0 := src0.w) |
| 14 | CNDGTv | kCndGt | 30 | — | — |
| 15 | DOT4v | kDp4 | 31 | VECTOR_NONE | — (not defined) |

### 2.3 Scalar opcodes (6 bits, dw0[26:31]): identical values
| v | a2xx | Xenos | v | a2xx | Xenos |
|---|---|---|---|---|---|
| 0 | ADDs | kAdds | 26 | SUB_PREVs | kSubsPrev |
| 1 | ADD_PREVs | kAddsPrev | 27 | PRED_SETEs | kSetpEq |
| 2 | MULs | kMuls | 28 | PRED_SETNEs | kSetpNe |
| 3 | MUL_PREVs | kMulsPrev | 29 | PRED_SETGTs | kSetpGt |
| 4 | MUL_PREV2s | kMulsPrev2 | 30 | PRED_SETGTEs | kSetpGe |
| 5 | MAXs | kMaxs | 31 | PRED_SET_INVs | kSetpInv |
| 6 | MINs | kMins | 32 | PRED_SET_POPs | kSetpPop |
| 7 | SETEs | kSeqs | 33 | PRED_SET_CLRs | kSetpClr |
| 8 | SETGTs | kSgts | 34 | PRED_SET_RESTOREs | kSetpRstr |
| 9 | SETGTEs | kSges | 35 | KILLEs | kKillsEq |
| 10 | SETNEs | kSnes | 36 | KILLGTs | kKillsGt |
| 11 | FRACs | kFrcs | 37 | KILLGTEs | kKillsGe |
| 12 | TRUNCs | kTruncs | 38 | KILLNEs | kKillsNe |
| 13 | FLOORs | kFloors | 39 | KILLONEs | kKillsOne |
| 14 | EXP_IEEE | kExp | 40 | SQRT_IEEE | kSqrt |
| 15 | LOG_CLAMP | kLogc | 41 | — | — |
| 16 | LOG_IEEE | kLog | 42/43 | MUL_CONST_0/1 | kMulsc0/1 |
| 17 | RECIP_CLAMP | kRcpc | 44/45 | ADD_CONST_0/1 | kAddsc0/1 |
| 18 | RECIP_FF | kRcpf | 46/47 | SUB_CONST_0/1 | kSubsc0/1 |
| 19 | RECIP_IEEE | kRcp | 48 | SIN | kSin |
| 20 | RECIPSQ_CLAMP | kRsqc | 49 | COS | kCos |
| 21 | RECIPSQ_FF | kRsqf | 50 | RETAIN_PREV | kRetainPrev |
| 22 | RECIPSQ_IEEE | kRsq | 51-62 | — | — |
| 23 | MOVAs | **kMaxAs** | 63 | SCALAR_NONE | — (table has 64 slots, 63 unnamed) |
| 24 | MOVA_FLOORs | **kMaxAsf** | | | |
| 25 | SUBs | kSubs | | | |

Semantic notes from Xenia that matter to a code generator:
- MOVA* are really `maxa*`: the result is `max(a,b)`, so `mova` must use src `.aa` / src0==src1.
- Every scalar op except RETAIN_PREV updates `ps`, so a "no-op" scalar slot must be RETAIN_PREV
  or have its mask cleared.
- Multiplies follow D3D9 rules (0 * anything = +0).

---

## 3. Fetch (96 bits, opcode at dw0[0:4], same for both kinds)

Fetch opcodes are identical: 0 VTX_FETCH/kVertexFetch, 1 TEX_FETCH/kTextureFetch, 16-19 get border-frac / comp-LOD /
gradients / weights, 24-26 set LOD / gradients H / V. a2xx also defines `TEX_RESERVED_4 = 27`, which has no Xenos counterpart.
Predication is the same in both kinds: dw1[31] is a2xx `pred_select` = Xenos `is_predicated`, and dw2[31] is `pred_condition`.

### 3.1 Vertex fetch
| dword | bits | a2xx `instr_fetch_vtx_t` | Xenos `VertexFetchInstruction` |
|---|---|---|---|
| dw0 | [5:10] / [11] | `src_reg` / `src_reg_am` | same |
| | [12:17] / [18] | `dst_reg` / `dst_reg_am` | same |
| | [19] | `must_be_one` | `must_be_one` |
| | [20:24] / [25:26] | `const_index` / `const_index_sel` | same |
| | [27:29] | `reserved0:3` | **`prefetch_count`** (count-1) |
| | [30:31] | `src_swiz:2` | `src_swiz:2` (absolute, 1 comp) |
| dw1 | [0:11] | `dst_swiz:12` | same (3 bits/comp: 0-3 xyzw, 4 = 0, 5 = 1, 7 = keep) |
| | [12] / [13] / [14] | `format_comp_all` / `num_format_all` / `signed_rf_mode_all` | same (`fomat_comp_all`) |
| | [15] | `reserved1` | **`is_index_rounded`** |
| | [16:21] | `format:6` (`a2xx_sq_surfaceformat`) | `format:6` (`xenos::VertexFormat`) |
| | [24:29] | `exp_adjust_all:6` (uint) | `exp_adjust:6` (signed) |
| | [30] | `reserved3` | **`is_mini_fetch`** |
| dw2 | [0:7] | `stride:8` (dwords) | same |
| | [8:30] | `offset:22` + `reserved4` | **`offset:23` signed** |

The fetch constant index is `const_index*3 + const_index_sel`, giving 0..95 in both (`kVertexFetchConstantCount` = 96).
The vertex fetch *constant* (`xe_gpu_vertex_fetch_t`, 2 dwords) carries `type:2`, `address:30` (dwords),
**`endian:2`** (`Endian`: none / 8in16 / 8in32 / 16in32), and `size:24`. The endian field is in the constant,
not the instruction. Big-endian vertex buffers on Xenos need `k8in32` there, which is driver state, not compiler output.

Vertex formats: every `xenos::VertexFormat` value has the same number and name in `a2xx_sq_surfaceformat`
(6 8_8_8_8, 7 2_10_10_10, 25/26 16_16(_16_16), 31/32 16 float, 33-38 32-bit int/float, 57 32_32_32_FLOAT),
**except 16/17**: Xenos 16 = `k_10_11_11` and 17 = `k_11_11_10`, while a2xx 16 = `FMT_8_8_8` and 17 is undefined.
Xenos has no 8_8_8 vertex format.

### 3.2 Texture fetch
| dword | bits | a2xx `instr_fetch_tex_t` | Xenos `TextureFetchInstruction` |
|---|---|---|---|
| dw0 | [5:18] | src/dst reg + `_am` | same |
| | [19] | `fetch_valid_only` | same |
| | [20:24] | `const_idx:5` (tf0-31) | `const_index:5` |
| | [25] | `tx_coord_denorm` | same |
| | [26:31] | `src_swiz:6` (3 comps x 2 bits, absolute) | same |
| dw1 | [0:11] | `dst_swiz:12` | same |
| | [12:27] | mag/min/mip(2 each), aniso(3), arbitrary(3), vol_mag/vol_min(2 each) | same order and widths |
| | [28] | `use_comp_lod` | same |
| | [29:30] | **`use_reg_lod:2`** ("0 for cube, 1 for 2d") | **`use_reg_lod`[29] + `unk`[30]** |
| dw2 | [0] / [1] | `use_reg_gradients` / `sample_location` | same |
| | [2:8] | `lod_bias:7` (uint) | `lod_bias:7` signed, 1/16 units |
| | [9:15] | `unused:7` | `unused:5` + **`dimension:2`** [14:15] (1D / 2D / 3D-stacked / cube) |
| | [16:30] | `offset_x/y/z:5` each (uint) | same, signed, 1/2-texel units |

The texture fetch constant (6 dwords, `xe_gpu_texture_fetch_t`) matches `a2xx.xml` SQ_TEX_0..5 bit-for-bit
(TYPE, SIGN_*, CLAMP_*, PITCH, TILED / FORMAT, ENDIANNESS[6:7], ... / DIMENSION[9:10], MIP_ADDRESS).
Xenos adds `stacked`, `packed_mips`, and more texture formats (EDRAM, MPEG, interlaced, `_AS_16_16_16_16`).
32 fetch groups of 6 dwords each hold either one texture constant or three vertex constants, in both.

---

## 4. Register file and exports

| item | a2xx | Xenos |
|---|---|---|
| Temp GPRs per shader | 6-bit fields; `SQ_PROGRAM_CNTL.VS_REGS/PS_REGS` (0x3f valid) | `kMaxShaderTempRegisters` = 64 (6-bit) |
| Float consts per operand | 8-bit index (256) + a0/aL relative | same |
| Bool / loop consts | `bool_addr:8` (256 bools), `loop_id:5` (32) | same |
| Interpolators | `VS_EXPORT_COUNT` 4 bits | `kMaxInterpolators` = 16 |
| VS export index | `vector_dest` with `export_data`=1 | interp 0-15, eA 32, eM0-4 33-37, pos 62, psize/edge/kill 63 |
| PS export index | same | color 0-3, depth 61 (X) |
| VS export mode | `VS_EXPORT_MODE` `a2xx_sq_ps_vtx_mode` | `VertexShaderExportMode` (same enum, comment cites `a2xx_sq_ps_vtx_mode`) |
| memexport | `SQ_MEMORY` alloc type present | documented: `alloc export`, `mad eA, r, c0100, c_stream`, then eM# writes; stream constant `xe_gpu_memexport_stream_t` |

**[unverified]** As I recall, ir2 exports position to 62, point size to 63, varyings from 0, and fragment color to 0.
That would match Xenos as-is. instr-a2xx.h does not list the export indices, so this needs checking against Mesa.

---

## 5. Verdict

| block | verdict |
|---|---|
| CF | **Identical.** Same opcodes and layout. Xenos names bits that Mesa leaves reserved (address 12/13 bits, `is_repeat`, `is_predicated_break`, loop_end `condition`, `is_predicate_clean`). |
| ALU | **Identical.** Same opcode tables and the same 96-bit layout. Only dw0[7] is named differently (`low_precision_16b_fp` vs `abs_constants`). |
| Fetch | **Identical layout, with a few extra Xenos fields.** Xenos adds tfetch `dimension` (dw2[14:15]), and vfetch `prefetch_count` / `is_index_rounded` / `is_mini_fetch` / 23-bit offset. a2xx `use_reg_lod:2` overlaps Xenos `use_reg_lod`+`unk`. Formats match except 16/17. |

None of the three blocks is "renumbered" or "different". The same ISA revision family is encoded the same way.

**Reuse estimate.** About 90% of ir2 carries over unchanged: NIR lowering, scheduling into CF/exec clauses, register allocation,
copy propagation, swizzle and export handling. **[unverified]** In Mesa these are ir2.c, ir2_nir.c, ir2_ra.c, ir2_cp.c, ir2_nir_lower_scalar.c.
The remaining ~10% is the encoder (ir2_assemble.c) plus a few emission decisions. Most of that cost is the packing method, not the ISA.

**Concrete changes for a Xenos emitter**

1. **Endianness (largest change).** Xenon Linux is ppc64 big-endian. GCC on BE allocates bitfields from the MSB,
   so `instr-a2xx.h`'s `PACKED` mixed-width bitfield structs will produce **wrong encodings** if compiled natively.
   Replace them with explicit shift/mask packers that build `uint32_t dw[3]` using the bit positions above.
   Store the words natively (BE). **[unverified]** Xenia byte-swaps guest microcode dwords on load, which implies the
   GPU consumes 32-bit words in big-endian memory order. Cross-compiling on x86 and byteswapping each dword would also work.
2. **CF pair split.** Pack as `dw1 = cf0[32:47] | cf1[0:15] << 16` and `dw2 = cf1[16:47]`. This matches the Xenos unpacker.
3. **Texture fetch.** Set `dimension` (dw2[14:15]) from the sampler dimension: 2D = 1, 3D/array = 2, cube = 3.
   Write `use_reg_lod` as 1 bit and `unk` (dw1[30]) as 0. For implicit LOD, use `use_comp_lod`=1.
   Check ir2's current `use_reg_lod` values against this.
4. **Vertex fetch.** Keep `must_be_one`=1. Write `prefetch_count`, `is_index_rounded`, and `is_mini_fetch` as 0, or use them
   for mini-fetch optimizations later. Encode `offset` as 23-bit signed. Never emit format 16 meaning 8_8_8.
5. **ALU dw0[7].** Emit 0 unless an abs-constant source is required. Do not reuse a2xx low-precision semantics.
6. **MOVA.** Emit as `maxa` with identical sources (vector 29 / scalar 23-24). Only a matter of naming.
7. **CF reserved bits.** Write as 0 (exec `is_predicate_clean` may be set as an optimization).
   Use the wider address fields when the program exceeds 512 slots (exec) or 1024 (loop/jmp).
8. **ALLOC.** `size` is 3 bits on Xenos. Keep `alloc_mode`=0.
9. **Driver-side, outside ir2.** Set `k8in32` endian in vertex fetch constants and the texture `endianness` field, configure
   `SQ_PROGRAM_CNTL`-equivalent registers, and upload with PM4 `IM_LOAD`/`IM_LOAD_IMMEDIATE` (0x27/0x2b) and `SET_SHADER_BASES` (0x4A).
10. **Optional Xenos extras.** memexport (eA/eM#, alloc type 3), PS depth export 61, `is_predicated_break` loops, and the `dimension`
    needed for 3D-stacked textures.
