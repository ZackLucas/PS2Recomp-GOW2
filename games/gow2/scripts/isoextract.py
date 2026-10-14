"""Minimal ISO9660 reader: list or extract files from a PS2 disc image.

    isoextract.py image.iso                 # list every file
    isoextract.py image.iso OUTDIR NAME...  # extract files by name (case-insensitive, no ';1')
"""
import os
import struct
import sys

SECTOR = 2048


def read_dir(f, lba, size, prefix=''):
    f.seek(lba * SECTOR)
    data = f.read(size)
    off = 0
    while off < len(data):
        length = data[off]
        if length == 0:  # records never straddle sectors; skip to the next one
            off = (off // SECTOR + 1) * SECTOR
            continue
        rec = data[off:off + length]
        off += length
        ext_lba, ext_size = struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
        flags, name_len = rec[25], rec[32]
        name = rec[33:33 + name_len]
        if name in (b'\x00', b'\x01'):
            continue
        name = name.decode('ascii', 'replace').split(';')[0]
        path = prefix + name
        if flags & 2:
            yield from read_dir(f, ext_lba, ext_size, path + '/')
        else:
            yield path, ext_lba, ext_size


def main():
    iso = sys.argv[1]
    with open(iso, 'rb') as f:
        f.seek(16 * SECTOR)
        pvd = f.read(SECTOR)
        assert pvd[1:6] == b'CD001', 'not an ISO9660 image'
        root = pvd[156:156 + 34]
        entries = list(read_dir(f, struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0]))
        if len(sys.argv) < 4:
            for path, lba, size in entries:
                print(f'{lba:8d} {size:10d} {path}')
            return
        out, wanted = sys.argv[2], {n.upper() for n in sys.argv[3:]}
        os.makedirs(out, exist_ok=True)
        for path, lba, size in entries:
            if os.path.basename(path).upper() in wanted:
                f.seek(lba * SECTOR)
                with open(os.path.join(out, os.path.basename(path)), 'wb') as o:
                    o.write(f.read(size))
                print('extracted', path, size)


if __name__ == '__main__':
    main()
