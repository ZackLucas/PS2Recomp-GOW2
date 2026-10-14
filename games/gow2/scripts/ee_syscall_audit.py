"""List the EE syscalls an ELF can make and check them against the runtime dispatcher.

    ee_syscall_audit.py REPO_ROOT GAME.ELF

Kernel stubs load the number into $v1 right before `syscall`, so each syscall
site is resolved by looking back a few instructions for `addiu/ori/daddiu $v1, $zero, imm`.
"""
import re
import struct
import sys
from collections import Counter
from pathlib import Path


def exec_segments(elf):
    phoff = struct.unpack_from('<I', elf, 28)[0]
    phentsize, phnum = struct.unpack_from('<HH', elf, 42)
    for i in range(phnum):
        p_type, p_offset, p_vaddr, _, p_filesz, _, p_flags, _ = struct.unpack_from('<8I', elf, phoff + i * phentsize)
        if p_type == 1 and p_flags & 1:
            yield p_vaddr, elf[p_offset:p_offset + p_filesz]


def jal_targets(elf):
    targets = set()
    for vaddr, code in exec_segments(elf):
        for i, w in enumerate(struct.unpack_from(f'<{len(code) // 4}I', code)):
            if w >> 26 == 0x03:  # JAL
                targets.add(((vaddr + i * 4 + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2))
    return targets


def syscall_numbers(elf):
    """Map syscall number -> number of stubs that some JAL actually calls."""
    called = jal_targets(elf)
    found, unresolved = Counter(), []
    for vaddr, code in exec_segments(elf):
        words = struct.unpack_from(f'<{len(code) // 4}I', code)
        for i, w in enumerate(words):
            if (w & 0xFC00003F) != 0x0000000C:
                continue
            number = None
            for back in range(1, 5):
                if i - back < 0:
                    break
                prev = words[i - back]
                op, rs, rt = prev >> 26, (prev >> 21) & 31, (prev >> 16) & 31
                if rt == 3 and rs == 0 and op in (0x09, 0x19):  # addiu / daddiu: sign-extended
                    number = struct.unpack('<h', struct.pack('<H', prev & 0xFFFF))[0]
                    break
                if rt == 3 and rs == 0 and op == 0x0D:  # ori: zero-extended
                    number = prev & 0xFFFF
                    break
            if number is None:
                unresolved.append(vaddr + i * 4)
                continue
            # libkernel stubs are `li $v1, N; syscall`; count the stub only when it is called.
            stub = vaddr + (i - back) * 4
            found[number] += 1 if stub in called else 0
    return found, unresolved


def dispatcher_cases(repo):
    src = (repo / 'ps2xRuntime/src/lib/Kernel/Syscalls/Dispatcher.cpp').read_text()
    body = src[src.index('switch (syscallNumber)'):]
    handled = {}
    pending = []
    for line in body.splitlines():
        for m in re.finditer(r'case\s+(?:static_cast<uint32_t>\((-?0x[0-9A-Fa-f]+|-?\d+)\)|(0x[0-9A-Fa-f]+|\d+))\s*:', line):
            pending.append(int(m.group(1) or m.group(2), 0))
        call = re.match(r'\s*([\w:]+)\(rdram', line)
        if call and pending:
            for n in pending:
                handled[n] = call.group(1)
            pending = []
    return handled


def main():
    repo = Path(sys.argv[1])
    found, unresolved = syscall_numbers(Path(sys.argv[2]).read_bytes())
    handled = dispatcher_cases(repo)
    missing = 0
    linked_only = sorted(n for n, c in found.items() if c == 0)
    for number in sorted((n for n, c in found.items() if c), key=lambda n: (abs(n), n < 0)):
        name = handled.get(number) or handled.get(number & 0xFFFFFFFF)
        label = f'-0x{-number:02X}' if number < 0 else f'0x{number:02X}'
        if name is None:
            missing += 1
        print(f'{label:>7}  sites={found[number]:<3} {name or "MISSING"}')
    unused_missing = [n for n in linked_only if (handled.get(n) or handled.get(n & 0xFFFFFFFF)) is None]
    print(f'\n{len(found) - len(linked_only)} syscalls called, {missing} of them missing; '
          f'{len(linked_only)} linked but never called ({len(unused_missing)} of those unimplemented); '
          f'{len(unresolved)} syscall site(s) without a nearby $v1 load')


if __name__ == '__main__':
    main()
