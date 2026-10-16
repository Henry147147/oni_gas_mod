#!/usr/bin/env python3
"""Fix signed flag shifts and unaligned reads/stores in pinned diStorm."""
from pathlib import Path
import hashlib
import sys

ORIGINAL_SHA256 = "28756beac323e81118fa64f41736be643f1678d94c7f3a631a7420eb7618820b"
BEFORE = b"#define INST_DST_WR (1 << 31)"
AFTER = (b"/* Mixed Atmospheres, 2026-10-16: construct the bit-31 flag without signed-shift UB. */\r\n"
         b"#define INST_DST_WR (1u << 31)")


def edit(path: Path, expected: str, changes: list[tuple[bytes, bytes]]) -> None:
    data = path.read_bytes()
    original = data
    for before, after in reversed(changes):
        original = original.replace(after, before)
    if hashlib.sha256(original).hexdigest() != expected or any(original.count(before) != 1 for before, _ in changes):
        raise ValueError(f"Unexpected diStorm {path.name}; refusing an unverified dependency edit")
    updated = original
    for before, after in changes:
        updated = updated.replace(before, after)
    if updated != data:
        path.write_bytes(updated)


def patch(root: Path) -> None:
    edit(root / "src/instructions.h", ORIGINAL_SHA256, [(BEFORE, AFTER)])
    old = b"\r\n".join([b"/* Little endian macro's will just make the cast. */"] +
        [f"#define {name}(x) *({kind} *)x".encode() for name, kind in (
            ("RSHORT", "int16_t"), ("RUSHORT", "uint16_t"), ("RLONG", "int32_t"),
            ("RULONG", "uint32_t"), ("RLLONG", "int64_t"), ("RULLONG", "uint64_t"))])
    lines = ["/* Mixed Atmospheres, 2026-10-16: memcpy preserves unaligned opcode access without C alignment UB. */",
             "#ifdef _MSC_VER", "#define MA_DISTORM_INLINE static __inline", "#else",
             "#define MA_DISTORM_INLINE static inline", "#endif"]
    for name, kind in (("RSHORT", "int16_t"), ("RUSHORT", "uint16_t"), ("RLONG", "int32_t"),
                       ("RULONG", "uint32_t"), ("RLLONG", "int64_t"), ("RULLONG", "uint64_t")):
        lines.append(f"MA_DISTORM_INLINE {kind} {name}(const void* pointer) {{ {kind} value; memcpy(&value, pointer, sizeof(value)); return value; }}")
    lines.append("#undef MA_DISTORM_INLINE")
    edit(root / "src/config.h", "91a4a6819dd52c1595a86ffa403c6e41a07cd33067c9ec484a95e613716c16e9",
         [(old, "\r\n".join(lines).encode())])
    stores = []
    for kind, read in (("uint16_t", "RUSHORT"), ("uint32_t", "RULONG"), ("uint64_t", "RULLONG")):
        before = f"*({kind}*)result = {read}(ci->code);".encode()
        after = f"{{ {kind} value = {read}(ci->code); memcpy(result, &value, sizeof(value)); }} /* Mixed Atmospheres: unaligned output */".encode()
        stores.append((before, after))
    edit(root / "src/operands.c", "a60c14552de9a93227ef19bb4b6bb70726b9b1c3b119e9dd7b3edf77d5527ad3", stores)
    edit(root / "src/textdefs.c", "0a42548475048c5a19a947426ff27f6c34ec732a014ac46af122a258bc3fa475", [(
        b"RSHORT(&s->p[i]) = RSHORT(&TextBTable[(*buf) * 2]);",
        b"memcpy(&s->p[i], &TextBTable[(*buf) * 2], sizeof(int16_t)); /* Mixed Atmospheres: unaligned text store */")])


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_distorm.py /build/distorm")
    patch(Path(sys.argv[1]))
