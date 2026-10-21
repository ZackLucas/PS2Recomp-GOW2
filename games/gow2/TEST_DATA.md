# God of War II — heavy test data

Catalog of the large files used to check the GS and the VU1 against PCSX2. None of
them is committed (they are game data, see `README.md`). Paths are relative to the
work directory (the one holding `SCUS_974.81`); the PCSX2 reference data lives in
its `pcsx2-ref/` folder. Several scripts hardcode that folder (`R` in
`scripts/pcsx2_exact.py`, `scripts/trunc_bisect.py`, `scripts/vram_bisect.py`).

**Open on demand only.** Use these files when the task is GS replay, the PCSX2
oracle or VU1 validation. Never read a file whole: go through the tool listed for it.

Sizes measured on 2026-10-20.

## Catalogued

| Data | Path (under `pcsx2-ref/`) | Size | Contents | Read with |
|---|---|---|---|---|
| Main menu dump | `God of War II_SCUS-97481_20261015000734.gs.zst` | 7 MB | PCSX2 GS dump of the main menu, 4 VSyncs | `gow2_gs_replay`, `gow2_gs_gpu_replay`, `gsdump.py draws\|tris`, `gsdump_drawn.py`; `trunc_bisect.py`, `trunc_exact.py` and `vram_bisect.py` use it by default |
| Scene dumps | `scenes/` | 31 MB | 4 dumps (difficulty menu and 3 FMVs) and 1 pause screen in `pause/`, next to our GPU replay frames (`*_gpuN.ppm`) | `gow2_gs_replay`, `gow2_gs_gpu_replay`, `gsdump.py` |
| Gameplay dumps | `gameplay/` | 261 MB | 19 dumps of the Colossus fight (recorded 2026-10-16), our GPU replay frames, `pares/` (the 18 PCSX2 × GPU pairs checked by eye on 2026-10-16) and `hud/` (HUD crops) | `gow2_gs_gpu_replay`, `hud_exact.py` |
| Captured VU1 calls | `vu1progs/` | 19 MB | 845 calls (`in_<image>_pc<N>_<k>.bin`, from `PS2_VU1_CAPTURE`) and 142 code images (`vu1_<image>.bin`) | `gow2_vu1_prog_test`, `gow2_vu1_run_one`, `vu1_in_dump.py`, `pcsx2_vu1_batch.py` |
| PCSX2 savestates | `sstates/` | 43 MB | `menu/menu_boot.p2s` and `user/user_save.p2s`, plus the VU1 state taken from them (`<tag>_internal.dat`, `<tag>_vu1Memory.bin`, `<tag>_vu1MicroMem.bin`) | `p2s_extract.py` (any member, e.g. `eeMemory.bin` to compare with `PS2_STATE_DUMP`), `vu1_ss_inputs.py`, `gow2_vu1_ss_test`; base state for `pcsx2_vu1_inject.py` and `pcsx2_vu1_batch.py` |
| VU1 oracle runs | `inj/` | 321 MB | see below | `pcsx2_vu1_batch.py`, `gow2_vu1_run_one`, `vu1_in_dump.py --raw` |

`inj/` holds:

| Folder | Size | Contents |
|---|---|---|
| `scan/` | 31 MB | `gow2_vu1_run_one` output for all 845 captured calls (`*_interp_mem.bin`, `*_interp_packets.bin`, `*_native_mem.bin`) |
| `ss/` | 760 KB | inputs built from the savestates by `vu1_ss_inputs.py` (`in_aaaa…` = menu) |
| `batch/`, `ssbatch/`, `macchk/` | 47 MB, 183 MB, 61 MB | `pcsx2_vu1_batch.py` cases, one folder each (`injected.p2s`, `pcsx2_after.p2s`, `pcsx2_mem.bin`, `ours_*.bin`): captured calls, savestate inputs and two more cases (purpose not recorded) |

`inj/clean_list.txt`, `inj/sane_list.txt` (empty) and `inj/ssbatch.log`: purpose not recorded.

## SPU2 audio (2026-10-21)

Outside `pcsx2-ref/`, under the work directory: `item21-2026-10-21/`.
Open these only for audio validation. `2026-10-21-spu2.pcm` and
`2026-10-21-final.pcm` are bounded native SPU2 output captures (s16le, 48 kHz,
stereo); matching `.log` files contain bounded NAX/DMA traces and driver errors.
Read them with `scripts/spu2_capture_summary.py PCM --log LOG --json SUMMARY`.
`--wav PATH --seconds 8` exports a listening excerpt at the original gain/rate,
trimming initial silence. `2026-10-21-menu-spu2.wav` is the review excerpt.
The small curated JSON is in the vault; audio, screenshots (`frame_*.png`) and
raw logs stay outside git. The longer capture predates only the final volume
readback UB fix and optional DMA hash trace; `final` checks the final build.

## VU1 native segments (2026-10-21)

Generated C++ for the native VU1 plugin, configured with `-DGOW2_VU1_GEN_DIR=<dir>`
(see `tools/CMakeLists.txt`). It is derived from game code, so it stays out of git.
The set measured in item 25 (menu, 10.7 VSync/s) is
`~/gow2/item25-2026-10-21/vu1gen-merged/`: 193 `seg_*.cpp` files and
`vu1_registry.cpp`, 5.4 MB. Proposed home, not moved yet: `~/gow2/vu1gen/`.

- Inputs for `gow2_vu1_gen <dir> image.bin ...`: `pcsx2-ref/vu1progs/vu1_*.bin`
  (142), `pcsx2-ref/vu1progs_fix/*.bin` (55) and the 10 images captured in item 25
  (`item25-2026-10-21/cap/vu1_*.bin`). 207 images in all.
- From these images, 173 of the 183 older segments and all 10 segments of item 25
  are rebuilt under the same names. The generator writes 1,134 segments in all, so
  the 193-segment set is a subset of its output.
- 10 older segments cannot be rebuilt, because their images are no longer on disk:
  `seg_22_8719a073085a39eb`, `seg_23_a254c1730a1f8a3c`, `seg_24_3d03c57d2fea874d`,
  `seg_25_cbc3b8e5e0dceebb`, `seg_26_7aa654911c2e387e`, `seg_27_e729368019bdad9e`,
  `seg_28_edc88bbe82dfa81d`, `seg_29_e5a4c07229825d43`, `seg_30_b15e65bb7539de0b`,
  `seg_31_d7be3dec6934484b` (all `.cpp`). They exist only in the copy, so that copy
  must be kept and must not be regenerated over.
- The older copy, `~/gow2/vu1gen/` (183 segments), is byte-identical to the new set
  for all 183 files. Only `vu1_registry.cpp` differs.

## Other uncatalogued data (probably regenerable, kept for now)

`logs/` (1.6 GB), `run8.log` (161 MB), `shots/**/ps2_log.txt`, `output_v2` … `output_v8`
(141 MB each), `pcsx2-ref/bis/` (1.2 GB) and `pcsx2-ref/syn/` (249 MB).

## Not classified yet

`pcsx2-ref/vu1progs_fix/` (1.1 MB, 55 files), `pcsx2-ref/out/` (65 MB, replay frames)
and the loose files at the root of `pcsx2-ref/` (screenshots, `ref_*.rgba`, `frame*.tris`).
