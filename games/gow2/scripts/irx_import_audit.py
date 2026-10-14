"""Cross-check every import of the game's IRX modules against ps2xIOP.

    irx_import_audit.py REPO_ROOT MODULE.IRX...

An import is covered when ps2xIOP handles the (library, ordinal) natively or
when one of the given modules exports that library (it then runs as guest code).
"""
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

IMPORT_MAGIC = 0x41E00000
EXPORT_MAGIC = 0x41C00000
JR_RA = 0x03E00008

# library -> (source file, dispatch function), relative to ps2xIOP/src/emulator
NATIVE = {
    'sysmem': ('imports/iop_sysmem.cpp', 'IopSysmem::dispatchImport'),
    'cdvdman': ('imports/iop_cdvd.cpp', 'bool dispatchImport'),  # the Impl class does the work
    'loadcore': ('imports/iop_loadcore.cpp', 'IopLoadcore::dispatchImport'),
    'thbase': ('core/iop_kernel.cpp', 'IopKernel::dispatchThreadImport'),
    'threadman': ('core/iop_kernel.cpp', 'IopKernel::dispatchThreadImport'),
    'thsemap': ('core/iop_kernel.cpp', 'IopKernel::dispatchSemaphoreImport'),
    'thevent': ('core/iop_kernel.cpp', 'IopKernel::dispatchEventImport'),
    'sifcmd': ('services/iop_rpc.cpp', 'IopRpcBridge::dispatchSifCmdImport'),
    'sifman': ('services/iop_rpc.cpp', 'IopRpcBridge::dispatchSifManImport'),
    'intrman': ('imports/iop_intrman.cpp', 'IopIntrman::dispatchImport'),
    'ioman': ('imports/iop_ioman.cpp', 'IopIoman::dispatchImport'),
    'vblank': ('imports/iop_vblank.cpp', 'IopVblank::dispatchImport'),
    'timrman': ('imports/iop_timrman.cpp', 'IopTimrman::dispatchImport'),
    'stdio': ('imports/iop_stdio.cpp', 'IopStdio::dispatchImport'),
    'sysclib': ('imports/iop_sysclib.cpp', 'IopSysclib::dispatchImport'),
    'heaplib': ('imports/iop_heaplib.cpp', 'IopHeaplib::dispatchImport'),
}
# Handled inline in iop_emulator.cpp.
INLINE = {'secrman': {4, 5}, 'modload': {13}}
STUBBED = {'dmacman'}  # every ordinal returns 0 without doing anything


def function_cases(path, name):
    src = path.read_text()
    start = src.index(name + '(')
    body_start = src.index('{', start)
    depth, i = 0, body_start
    while True:
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0:
                break
        i += 1
    body = src[body_start:i]
    return {int(m, 0) for m in re.findall(r'case\s+(0x[0-9a-fA-F]+|\d+)u?\s*:', body)}


def parse_tables(data):
    imports, exports = defaultdict(set), set()
    words = len(data) // 4
    for w in range(words - 6):
        magic = struct.unpack_from('<I', data, w * 4)[0]
        if magic not in (IMPORT_MAGIC, EXPORT_MAGIC):
            continue
        name = data[w * 4 + 12:w * 4 + 20].split(b'\0')[0]
        if not name or not all(32 < c < 127 for c in name):
            continue
        lib = name.decode()
        if magic == EXPORT_MAGIC:
            exports.add(lib)
            continue
        p = w * 4 + 20
        while p + 8 <= len(data):
            a, b = struct.unpack_from('<II', data, p)
            if a != JR_RA or (b & 0xFFFF0000) != 0x24000000:
                break
            imports[lib].add(b & 0xFFFF)
            p += 8
    return imports, exports


def main():
    root = Path(sys.argv[1]) / 'ps2xIOP/src/emulator'
    native = {lib: function_cases(root / f, fn) for lib, (f, fn) in NATIVE.items()}
    native.update(INLINE)

    module_imports, exported_by = {}, {}
    for path in sys.argv[2:]:
        imports, exports = parse_tables(Path(path).read_bytes())
        module_imports[Path(path).name] = imports
        for lib in exports:
            exported_by[lib] = Path(path).name

    missing = 0
    for module, imports in module_imports.items():
        print(f'== {module}')
        for lib in sorted(imports):
            ordinals = sorted(imports[lib])
            if lib in exported_by:
                print(f'   {lib:10s} guest ({exported_by[lib]}) {ordinals}')
                continue
            if lib in STUBBED:
                print(f'   {lib:10s} STUB (returns 0) {ordinals}')
                missing += len(ordinals)
                continue
            handled = native.get(lib, set())
            absent = [o for o in ordinals if o not in handled]
            missing += len(absent)
            status = 'ok' if not absent else f'MISSING {absent}'
            print(f'   {lib:10s} {status:30s} uses {ordinals}')
    print(f'\n{missing} import(s) not implemented or stubbed')


if __name__ == '__main__':
    main()
