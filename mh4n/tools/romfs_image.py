#!/usr/bin/env python3
"""Read a file out of a RomFS, by its path: an IVFC image (a game folder's romfs.bin or update_romfs.bin) or the RomFS
of a decrypted dump (.3ds/.cci, .cia or .cxi: its first NCCH's).

    python3 tools/romfs_image.py ROMFS.bin system/ltshader/LtShaderPackage.lfx [OUT]

Only the level-3 data is read (the hash levels are not checked: verify_ncch.py does that).
"""
from __future__ import annotations

import os
import struct
import sys
from typing import BinaryIO

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import extract_ncch  # noqa: E402


def _level3_offset(header: bytes) -> int:
    if header[:4] != b"IVFC" or struct.unpack_from("<I", header, 4)[0] != 0x10000:
        raise ValueError("not an IVFC RomFS image")
    master_hash_size = struct.unpack_from("<I", header, 8)[0]
    level3_block = 1 << struct.unpack_from("<I", header, 0x4C)[0]
    # level 3 comes first in the image, after the header and the master hash, aligned to its block size
    return (0x60 + master_hash_size + level3_block - 1) & ~(level3_block - 1)


class RomFs:
    def __init__(self, f: BinaryIO, base: int = 0):
        self.f = f
        f.seek(base)
        self.level3 = base + _level3_offset(f.read(0x60))
        f.seek(self.level3)
        h = struct.unpack("<10I", f.read(0x28))
        self.dir_meta = self._read(h[3], h[4])
        self.file_meta = self._read(h[7], h[8])
        self.data = self.level3 + h[9]

    def _read(self, offset: int, size: int) -> bytes:
        self.f.seek(self.level3 + offset)
        return self.f.read(size)

    def _dir(self, offset: int) -> tuple[int, int, int, str]:
        parent, sibling, child, first_file, _bucket, length = struct.unpack_from("<6I", self.dir_meta, offset)
        name = self.dir_meta[offset + 0x18:offset + 0x18 + length].decode("utf-16-le")
        return sibling, child, first_file, name

    def _file(self, offset: int) -> tuple[int, int, int, str]:
        _parent, sibling, data_offset, size, _bucket, length = struct.unpack_from("<2I2Q2I", self.file_meta, offset)
        name = self.file_meta[offset + 0x20:offset + 0x20 + length].decode("utf-16-le")
        return sibling, data_offset, size, name

    def read(self, path: str) -> bytes | None:
        """The file's contents, None when the image has no such file."""
        parts = [p for p in path.replace("\\", "/").split("/") if p]
        directory = 0  # the root
        for part in parts[:-1]:
            _sibling, child, _first_file, _name = self._dir(directory)
            while child != 0xFFFFFFFF:
                sibling, grandchild, _ff, name = self._dir(child)
                if name.lower() == part.lower():
                    break
                child = sibling
            if child == 0xFFFFFFFF:
                return None
            directory = child
        _sibling, _child, entry, _name = self._dir(directory)
        while entry != 0xFFFFFFFF:
            sibling, data_offset, size, name = self._file(entry)
            if name.lower() == parts[-1].lower():
                self.f.seek(self.data + data_offset)
                return self.f.read(size)
            entry = sibling
        return None


def romfs_offset(f: BinaryIO) -> int:
    """Where the RomFS starts in `f`: 0 for an image, the first NCCH's RomFS in a dump."""
    f.seek(0)
    if f.read(4) == b"IVFC":
        return 0
    kind = extract_ncch.detect(f)
    if kind == "ncsd":
        base, _ = extract_ncch.ncsd_partition0(f)
    elif kind == "cia":
        content = extract_ncch.cia_content0(f)
        if content.encrypted:
            raise ValueError("the CIA's content is encrypted")
        base = content.offset
    else:
        base = 0
    ncch = extract_ncch.parse_ncch(f, base)
    if not ncch.no_crypto:
        raise ValueError("the NCCH is encrypted")
    if ncch.romfs_size == 0:
        raise ValueError("the NCCH has no RomFS")
    return base + ncch.romfs_offset


def read_file(images: list[str], path: str) -> bytes | None:
    """`path` from the first image or dump that has it (an update's RomFS before the base game's)."""
    for image in images:
        with open(image, "rb") as f:
            data = RomFs(f, romfs_offset(f)).read(path)
        if data is not None:
            return data
    return None


def main(argv: list[str] | None = None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    data = read_file([argv[0]], argv[1])
    if data is None:
        print(f"{argv[1]}: not in {argv[0]}", file=sys.stderr)
        return 1
    if len(argv) > 2:
        with open(argv[2], "wb") as f:
            f.write(data)
    print(f"{argv[1]}: {len(data)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
