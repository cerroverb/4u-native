#!/usr/bin/env python3
"""The game's PICA geometry-shader programs as C, for the player's code library (tools/recomp/runtime/pica_native.h).

    python3 tools/pica_native.py --romfs ROMFS [--romfs ROMFS ...] -o pica_native.c

Reads system/ltshader/LtShaderPackage.lfx from the first RomFS that has it (a RomFS image such as a game folder's
romfs.bin, or a .3ds/.cci/.cia dump: the base game's has it), finds its shader binaries (DVLB), and writes, for
each entry point of every binary that has a geometry shader, a C function that does what the CPU vertex path's
interpreter (native/src/gpu/cpuvertex/shader_interpreter.cpp) does: the same float operations in the same order,
the same register state. Entry points whose code the interpreter would run in a way a structured function cannot
follow (loops, jumps, breaks, scopes that end together, code past the binary) are left out: the interpreter runs
them. The output is made from the game's own code: it is never shared or committed, and build_game.py writes it into
the build directory only.
"""
from __future__ import annotations

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import romfs_image  # noqa: E402

PACKAGE = "system/ltshader/LtShaderPackage.lfx"

ARITH_NAMES = {0x00: "ADD", 0x01: "DP3", 0x02: "DP4", 0x03: "DPH", 0x04: "DST", 0x05: "EX2", 0x06: "LG2", 0x07: "LIT",
               0x08: "MUL", 0x09: "SGE", 0x0A: "SLT", 0x0B: "FLR", 0x0C: "MAX", 0x0D: "MIN", 0x0E: "RCP", 0x0F: "RSQ",
               0x12: "MOVA", 0x13: "MOV", 0x18: "DPHI", 0x19: "DSTI", 0x1A: "SGEI", 0x1B: "SLTI", 0x2E: "CMP", 0x2F: "CMP"}
INVERTED = {0x18, 0x19, 0x1A, 0x1B}
NOP, END, BREAKC, CALL, CALLC, CALLU, IFU, IFC, LOOP, EMIT, SETEMIT, JMPC, JMPU, BREAK = (
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x20)
CMP_C = ["==", "!=", "<", "<=", ">", ">="]


class Unsupported(Exception):
    pass


def shader_binaries(package: bytes):
    """(code words, operand descriptors, [(type, main offset)]) of each DVLB in the package."""
    at = package.find(b"DVLB")
    while at >= 0:
        count = struct.unpack_from("<I", package, at + 4)[0]
        dvle = [at + o for o in struct.unpack_from(f"<{count}I", package, at + 8)]
        dvlp = at + 8 + 4 * count
        if package[dvlp:dvlp + 4] == b"DVLP":
            _version, code_off, code_words, desc_off, desc_count = struct.unpack_from("<5I", package, dvlp + 4)
            code = list(struct.unpack_from(f"<{code_words}I", package, dvlp + code_off))
            desc = [struct.unpack_from("<I", package, dvlp + desc_off + 8 * k)[0] for k in range(desc_count)]
            entries = []
            for e in dvle:
                if package[e:e + 4] == b"DVLE":
                    kind = package[e + 6]
                    main = struct.unpack_from("<I", package, e + 8)[0]
                    entries.append((kind, main))
            yield code, desc, entries
        at = package.find(b"DVLB", at + 4)


def fnv(words: list[int]) -> int:
    h = 0xcbf29ce484222325
    for w in words:
        for b in range(4):
            h ^= (w >> (8 * b)) & 0xFF
            h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def f32(x: float) -> str:
    return f"{x:.1f}f"


class Generator:
    """One entry point's function body."""

    def __init__(self, code: list[int], desc: list[int]):
        self.code, self.desc = code, desc
        self.lines: list[str] = []
        self.depth = 0
        self.temp = 0

    def out(self, line: str) -> None:
        self.lines.append("    " * (self.depth + 1) + line)

    # ---- operands
    def source(self, value: int, address: int) -> str:
        if value < 0x10:
            return f"s->input[{value}]"
        if value < 0x20:
            return f"s->temporary[{value - 0x10}]"
        index = value - 0x20
        if address == 0:
            return f"s->uniform_f[{index}]"
        return f"pn_uniform(s, {index}, {address})"

    def dest(self, value: int) -> str:
        return f"s->output[*s->output_bank][{value}]" if value < 0x10 else f"s->temporary[{value - 0x10}]"

    def load(self, name: str, ptr: str, selectors: list[int], negate: bool) -> None:
        self.out(f"const float *{name}p = {ptr};")
        sign = "-" if negate else ""
        self.out(f"const float {name}[4] = {{{', '.join(f'{sign}{name}p[{k}]' for k in selectors)}}};")

    def desc_word(self, desc_id: int) -> int:
        if desc_id >= len(self.desc):
            raise Unsupported(f"operand descriptor {desc_id} past the binary's {len(self.desc)}")
        return self.desc[desc_id]

    # ---- one arithmetic or multiply-add instruction
    def arithmetic(self, pc: int, word: int) -> None:
        op = word >> 26
        self.out(f"{{ /* {pc}: {word:08x} */")
        self.depth += 1
        if op >= 0x30:
            inverted = op < 0x38
            d = self.desc_word(word & 0x1F)
            address = (word >> 22) & 3
            src1 = (word >> 17) & 0x1F
            src2 = ((word >> 12) & 0x1F) if inverted else ((word >> 10) & 0x7F)
            src3 = ((word >> 5) & 0x7F) if inverted else ((word >> 5) & 0x1F)
            self.load("a", self.source(src1, 0), [(d >> (11 - 2 * k)) & 3 for k in range(4)], bool((d >> 4) & 1))
            self.load("b", self.source(src2, 0 if inverted else address), [(d >> (20 - 2 * k)) & 3 for k in range(4)],
                      bool((d >> 13) & 1))
            self.load("c", self.source(src3, address if inverted else 0), [(d >> (29 - 2 * k)) & 3 for k in range(4)],
                      bool((d >> 22) & 1))
            self.out(f"float *d = {self.dest((word >> 24) & 0x1F)};")
            for k in range(4):
                if d & (8 >> k):
                    self.out(f"d[{k}] = pn_mul(a[{k}], b[{k}]) + c[{k}];")
        else:
            name = ARITH_NAMES.get(op)
            if name is None or name in ("DST", "DSTI", "LIT"):
                raise Unsupported(f"instruction {word:08x} at {pc}")
            inverted = op in INVERTED
            d = self.desc_word(word & 0x7F)
            address = (word >> 19) & 3
            src1 = ((word >> 14) & 0x1F) if inverted else ((word >> 12) & 0x7F)
            src2 = ((word >> 7) & 0x7F) if inverted else ((word >> 7) & 0x1F)
            self.load("a", self.source(src1, 0 if inverted else address), [(d >> (11 - 2 * k)) & 3 for k in range(4)],
                      bool((d >> 4) & 1))
            self.load("b", self.source(src2, address if inverted else 0), [(d >> (20 - 2 * k)) & 3 for k in range(4)],
                      bool((d >> 13) & 1))
            mask = [k for k in range(4) if d & (8 >> k)]
            if name == "CMP":
                cx, cy = (word >> 24) & 7, (word >> 21) & 7
                if cx > 5 or cy > 5:
                    raise Unsupported(f"CMP with compare mode {cx}/{cy} at {pc}")
                self.out(f"s->conditional[0] = a[0] {CMP_C[cx]} b[0];")
                self.out(f"s->conditional[1] = a[1] {CMP_C[cy]} b[1];")
            elif name == "MOVA":
                for k in mask:
                    if k < 2:
                        self.out(f"s->address[{k}] = pn_f2s(a[{k}]);")
            else:
                self.out(f"float *d = {self.dest((word >> 21) & 0x1F)};")
                if name in ("DP3", "DP4", "DPH", "DPHI"):
                    n = 3 if name == "DP3" else 4
                    terms = ["a[0]", "a[1]", "a[2]", "1.0f" if name in ("DPH", "DPHI") else "a[3]"]
                    self.out("float dot = 0.0f;")
                    for k in range(n):
                        self.out(f"dot = dot + pn_mul({terms[k]}, b[{k}]);")
                    for k in mask:
                        self.out(f"d[{k}] = dot;")
                elif name in ("RCP", "RSQ", "EX2", "LG2"):
                    expr = {"RCP": "1.0f / a[0]", "RSQ": "1.0f / sqrtf(a[0])", "EX2": "s->exp2(a[0])",
                            "LG2": "s->log2(a[0])"}[name]
                    self.out(f"const float r = {expr};")
                    for k in mask:
                        self.out(f"d[{k}] = r;")
                else:
                    expr = {"ADD": "a[{k}] + b[{k}]", "MUL": "pn_mul(a[{k}], b[{k}])", "FLR": "floorf(a[{k}])",
                            "MAX": "(a[{k}] > b[{k}]) ? a[{k}] : b[{k}]", "MIN": "(a[{k}] < b[{k}]) ? a[{k}] : b[{k}]",
                            "MOV": "a[{k}]", "SGE": "(a[{k}] >= b[{k}]) ? 1.0f : 0.0f",
                            "SGEI": "(a[{k}] >= b[{k}]) ? 1.0f : 0.0f", "SLT": "(a[{k}] < b[{k}]) ? 1.0f : 0.0f",
                            "SLTI": "(a[{k}] < b[{k}]) ? 1.0f : 0.0f"}[name]
                    for k in mask:
                        self.out(f"d[{k}] = {expr.format(k=k)};")
        self.depth -= 1
        self.out("}")

    @staticmethod
    def condition(word: int) -> str:
        refx, refy, op = (word >> 25) & 1, (word >> 24) & 1, (word >> 22) & 3
        x, y = f"s->conditional[0] == {refx}", f"s->conditional[1] == {refy}"
        return [f"({x}) || ({y})", f"({x}) && ({y})", x, y][op]

    # ---- a region of code: [start, end), run from start; True when every way through it reaches END
    def block(self, start: int, end: int, scope_ends: list[int]) -> bool:
        pc = start
        while pc < end:
            if pc >= len(self.code) or pc >= 4095:  # (the interpreter takes the last word of memory as END)
                raise Unsupported(f"the code runs past the binary at {pc}")
            word = self.code[pc]
            op = word >> 26
            if op in ARITH_NAMES or op >= 0x30:
                self.arithmetic(pc, word)
            elif op == NOP:
                pass
            elif op == END:
                self.out(f"return 1; /* {pc}: end */")
                return True
            elif op == EMIT:
                self.out(f"if (!s->emit(s->ctx)) return -{1 + pc};")
                self.out("*s->output_bank = !*s->output_bank;")
            elif op == SETEMIT:
                self.out(f"if (!s->setemit(s->ctx, {(word >> 24) & 3}u, {(word >> 23) & 1}u, {(word >> 22) & 1}u)) "
                         f"return -{1 + pc};")
            elif op in (IFU, IFC, CALL, CALLU, CALLC):
                num, dst = word & 0xFF, (word >> 10) & 0xFFF
                if op == IFU:
                    cond = f"s->uniform_b[{(word >> 22) & 0xF}]"
                elif op == CALLU:
                    cond = f"s->uniform_b[{(word >> 22) & 0xF}]"
                elif op in (IFC, CALLC):
                    cond = self.condition(word)
                else:
                    cond = "1"
                if op in (IFU, IFC):
                    # then [pc + 1, dst), else [dst, dst + num), on at dst + num; the interpreter closes an if scope at
                    # its else address, one scope an instruction: a scope that ends where another one ends is refused
                    if not (pc < dst and dst + num <= end):
                        raise Unsupported(f"an if at {pc} whose scope is not inside its block")
                    if dst in scope_ends or (num and dst + num in scope_ends):
                        raise Unsupported(f"an if at {pc} whose scope ends with another one")
                    self.out(f"if ({cond}) {{ /* {pc} */")
                    self.depth += 1
                    then_ends = self.block(pc + 1, dst, scope_ends + [dst])
                    self.depth -= 1
                    self.out("} else {")
                    self.depth += 1
                    else_ends = self.block(dst, dst + num, scope_ends + [dst + num]) if num else False
                    self.depth -= 1
                    self.out("}")
                    if then_ends and else_ends:
                        return True
                    pc = dst + num
                    continue
                # a call: [dst, dst + num), then back to pc + 1
                if len(scope_ends) >= 6:
                    raise Unsupported(f"calls nested too deep at {pc}")
                if dst + num in scope_ends or dst + num > len(self.code) or num == 0:
                    raise Unsupported(f"a call at {pc} whose scope ends with another one")
                self.out(f"if ({cond}) {{ /* {pc}: call {dst}..{dst + num} */")
                self.depth += 1
                self.block(dst, dst + num, scope_ends + [dst + num])
                self.depth -= 1
                self.out("}")
            else:
                raise Unsupported(f"flow instruction {word:08x} at {pc}")
            pc += 1
        return False


PRELUDE = r"""/* Generated by tools/pica_native.py from the game's shader package: never share or commit this file. */
#include <limits.h>
#include <math.h>
#include "pica_native.h"

#if defined(_WIN32)
#define PN_EXPORT __declspec(dllexport)
#else
#define PN_EXPORT __attribute__((visibility("default")))
#endif

/* f24's multiplication: 0 x inf is 0 */
static inline float pn_mul(float a, float b) {
    float r = a * b;
    if (__builtin_isnan(r) && !__builtin_isnan(a) && !__builtin_isnan(b)) {
        r = 0.0f;
    }
    return r;
}

static const float pn_ones[4] = {1.0f, 1.0f, 1.0f, 1.0f};

/* a float uniform with relative addressing (address 1 a0.x, 2 a0.y, 3 aL) */
static inline const float *pn_uniform(const PicaNativeState *s, int index, int address) {
    int offset = s->address[address - 1];
    if (offset < -128 || offset > 127) {
        offset = 0;
    }
    index = (index + offset) & 0x7F;
    return index >= 96 ? pn_ones : s->uniform_f[index];
}

/* static_cast<s32>(float) as x86-64 does it: out of range and NaN give the most negative integer */
static inline int pn_f2s(float v) {
    if (!(v > -2147483648.0f && v < 2147483648.0f)) {
        return INT_MIN;
    }
    return (int)v;
}
"""


def generate(package: bytes) -> tuple[str, int, int]:
    """The C file, the functions in it and the entry points left out."""
    parts = [PRELUDE]
    table = []
    skipped = 0
    for b, (code, desc, entries) in enumerate(shader_binaries(package)):
        if not any(kind == 1 for kind, _ in entries):
            continue
        for kind, main in entries:
            g = Generator(code, desc)
            try:
                if not g.block(main, len(code), []):
                    raise Unsupported("a way through the code reaches no END")
            except Unsupported:
                skipped += 1
                continue
            name = f"pn_{b}_{main}"
            parts.append(f"static int {name}(PicaNativeState *s) {{")
            parts.extend(g.lines)
            parts.append("    return 1;")
            parts.append("}\n")
            table.append(f"    {{{main}u, {len(code)}u, {len(desc)}u, {1 if kind == 1 else 0}u, 0x{fnv(code):016x}ull, "
                         f"0x{fnv(desc):016x}ull, {name}}},")
    parts.append("static const PicaNativeEntry pn_table[] = {")
    parts.extend(table if table else ["    {0u, 0u, 0u, 0u, 0ull, 0ull, 0},"])
    parts.append("};\n")
    parts.append("PN_EXPORT const PicaNativeEntry *pica_native_table(unsigned *count, unsigned *version) {")
    parts.append(f"    *count = {len(table)}u;")
    parts.append("    *version = PICA_NATIVE_VERSION;")
    parts.append("    return pn_table;")
    parts.append("}")
    return "\n".join(parts) + "\n", len(table), skipped


def find_package(romfs: list[str]) -> bytes | None:
    return romfs_image.read_file(romfs, PACKAGE)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--romfs", action="append", required=True,
                    help="a RomFS image (a game folder's romfs.bin) or the base game's .3ds/.cci/.cia dump")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args(argv)
    package = find_package(args.romfs)
    if package is None:
        print(f"{PACKAGE}: in none of {', '.join(args.romfs)}", file=sys.stderr)
        return 1
    text, functions, skipped = generate(package)
    with open(args.output, "w", encoding="ascii", newline="\n") as f:
        f.write(text)
    print(f"{args.output}: {functions} shader entry points as C ({skipped} left to the interpreter)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
