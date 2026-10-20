# MMI audit

Checks every R5900 MMI instruction the recompiler emits against a scalar reference written from the
EE Core Instruction Set Manual (cross-checked with the PCSX2 interpreter where the manual is vague).

It tests the **real path**: the instruction word goes through `R5900Decoder` + `CodeGenerator`, and the
emitted C++ is compiled against the runtime headers (`ps2_runtime_macros.h`). For each instruction it runs
20,000 random + edge inputs and compares rd, rs, rt, LO (128 bits) and HI (128 bits).

- `mmi_ops.h`: the instructions under test and their encoding (rs=2, rt=3, rd=4).
- `mmi_audit_gen.cpp`: writes `mmi_gen.inc` (one function per instruction) and, given the ELF, counts the
  MMI words in its executable segments (words that are data also show up there, as "?unknown").
- `mmi_audit_check.cpp`: the reference model and the comparison. `mmi_audit_check NAME` runs one op.

Words of LO/HI that the manual leaves undefined (odd words of PHMADH/PHMSBH) are not compared.
x/0 of PDIVBW is not tested (not specified).

## Build and run (from the repo root)

```bash
B=build; D=~/gow2/logs/mmi_audit; mkdir -p $D
ninja -C $B ps2_recomp_lib
c++ -std=gnu++20 -O1 -msse4.1 -flto=auto -w -DLIBDWARF_STATIC -I$B/_deps/elfio-src -I$B/_deps/fmt-src/include \
  -I$B/_deps/libdwarf-src/src/lib/libdwarf -I$B/_deps/rabbitizer-src/include -I$B/_deps/rabbitizer-src/tables \
  -I$B/_deps/toml11-src/include -Ips2xRecomp/include -Ips2xRuntime/include games/gow2/tools/mmi_audit/mmi_audit_gen.cpp \
  $B/ps2xRecomp/libps2_recomp_lib.a $B/_deps/fmt-build/libfmt.a $B/_deps/libdwarf-build/src/lib/libdwarf/libdwarf.a \
  $B/ps2xRecomp/librabbitizer.a -o $D/mmi_audit_gen
$D/mmi_audit_gen $D/mmi_gen.inc ~/gow2/SCUS_974.81
INC=$(ninja -C build-o2 -t commands ps2_runtime | grep -m1 "ps2_runtime.cpp" | grep -o " -I[^ ]*" | tr '\n' ' ')
g++ -std=gnu++20 -O1 -msse4.1 -w $INC -DMMI_GEN_INC="\"$D/mmi_gen.inc\"" games/gow2/tools/mmi_audit/mmi_audit_check.cpp -o $D/mmi_audit_check
$D/mmi_audit_check
```

## Result (2026-10-20)

Before the fix: 74 ok, 34 wrong, 1 crashing the host (PDIVBW, x86 `idiv` trap on INT_MIN / -1).
After: 109 / 109 ok. In the GoW2 ELF only 4 sites changed (all PMULTH, in sub_00116850, sub_00116BD8, sub_00117158).
