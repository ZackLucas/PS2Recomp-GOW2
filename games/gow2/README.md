# God of War II (SCUS_974.81) — PS2Recomp branch

Game-specific branch of PS2Recomp for God of War II (USA). Generic fixes land
in their own commits so they can be cherry-picked upstream; anything that only
makes sense for this game (HLE of engine functions, recompiled VU1
microprograms, renderer shortcuts) lives here.

**Never commit game data or the generated C++** (`ps2xRuntime/src/runner/*`,
`ps2xRuntime/include/ps2_recompiled_*.h`, ELF/IRX files, ISO). Everything is
reproduced from your own disc. `register_functions.cpp` is tracked upstream as
a placeholder; keep local edits out of commits with:

```bash
git update-index --skip-worktree ps2xRuntime/src/runner/register_functions.cpp
```

## Layout

| Path | What |
|---|---|
| `gow2.toml` | Recompiler config (function list, MMIO sites, entry points) |
| `scripts/vudis.py` | VU micro-instruction disassembler: `vudis.py code.bin [start] [end]` |
| `scripts/irxdis.py` | IOP module (IRX) disassembler, needs `capstone` |
| `scripts/gdb_*.py` | gdb helpers used while debugging SIF DMA |

## Build

1. Extract `SCUS_974.81` from the ISO into a work directory and copy `gow2.toml` next to it.
2. Recompile (writes `./output/`):
   ```bash
   build/ps2xRecomp/ps2_recomp gow2.toml
   ```
3. Copy the output into the runner:
   ```bash
   cp output/*.cpp ps2xRuntime/src/runner/ && cp output/*.h ps2xRuntime/include/
   ```
4. Build the runner (Release for playing, `-O1` dev tree for fast iteration):
   ```bash
   cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-msse4.1
   cmake --build build-rel --target ps2EntryRunner
   ```

## Run

```bash
PS2_HIDE_DEBUG_UI=1 build-rel/ps2xRuntime/ps2EntryRunner SCUS_974.81 "God of War II (USA).iso"
```

Keyboard: arrows/WASD = D-pad, X/Space = Cross, C/Esc = Circle, Z = Square,
V = Triangle, Enter = Start, Tab = Select. A gamepad is used when present.

Debug environment variables: `PS2_SCREENSHOT_EVERY=N`, `PS2_TRACE_SYSCALLS`
(`=2` for all), `PS2_TRACE_SIF_DMA`, `PS2_TRACE_IOP_IMPORTS`, `PS2_DUMP_THREADS`,
`PS2_TRACE_VIF1=N`, `PS2_TRACE_BIGTRI=N`.

## Status

- Boots, controller detected, main menu renders correctly.
- Slow: VU1 interpreter (~35% CPU) and the software GS rasterizer (~20%) dominate;
  the recompiled EE code itself is cheap.

## Coverage audit

Enumerate what the game uses statically instead of waiting for it to crash.
IOP imports (`scripts/irx_import_audit.py . irx/*.IRX`, modules extracted with
`scripts/isoextract.py`). Gaps as of the main menu (names from ps2sdk exports):

| Import | Used by | Notes |
|---|---|---|
| `secrman:6` SecrAuthCard | SIO2D | Memory card authentication; likely why cards report "no device" |
| `cdvdman:24` sceCdReadClock | MC2_D, MC2_S1 | Save timestamps |
| `cdvdman:15` sceCdStop, `cdvdman:39` sceCdBreak | 989NOMID | Audio streaming stop/abort |
| `thbase:43` GetSystemTimeLow | SMPD | |
| `dmacman:28,32-35` sceSetSliceDMA, sceStartDMA, sceSetDMAPriority, sceEnable/DisableDMAChannel | SIO2MAN | Stubbed to return 0; SIO2 DMA (memory card transfers) |

EE syscalls (`scripts/ee_syscall_audit.py . SCUS_974.81`): libkernel links stubs
for ~140 syscalls but only 50 are reached by a `jal`. All 50 are dispatched since
`0x6B` sceSifStopDma was wired up. Calls through function pointers are not seen.

Still to audit: VU1 microprogram opcodes, GS registers/primitives.

## Roadmap

1. Release build, cheap VU1 interpreter mode, faster IOP scheduler.
2. Multithreaded / SIMD GS rasterizer.
3. Statically recompile the game's VU1 microprograms to C++.
4. GPU GS backend.
