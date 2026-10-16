#!/usr/bin/env python3
"""Fix bounded short-branch relocation and unaligned operands in Funchook 1.1.3.

Changes are limited to downloaded build artifacts. The original source hash is
verified even on repeated application by reversing these exact substitutions.
The upstream linking exception extends to this modified version. The complete
modification is distributed here alongside the original source archive URLs.
"""
from pathlib import Path
import hashlib
import sys

ORIGINAL_SHA256 = "7476b9d63511f2b86bc78a25ec2d81b2b60bc4b4c42a212e37cabc24f252eca5"
OLD = """            size_t insn_size = funchook_insn_size(insn);
            memcpy(ctx.dst, ctx.src, insn_size);
            funchook_disasm_x86_rip_relative(&disasm, insn, &rel_disp, &rel_imm);
"""
BOUND = """            if ((size_t)(ctx.dst - ctx.dst_base) + insn_size + JUMP32_SIZE > TRAMPOLINE_SIZE) {
                funchook_set_error_message(funchook, "Widened instruction exceeds trampoline capacity");
                rv = FUNCHOOK_ERROR_CANNOT_FIX_IP_RELATIVE;
                goto cleanup;
            }
"""
NEW = """            /* Mixed Atmospheres modification, 2026-10-16: widen a single
             * external rel8 JMP/Jcc before relocation. The upstream linking
             * exception extends to this modified version. LOOP-family and
             * internal hot-region branches remain unsupported and reject. */
            size_t source_size = funchook_insn_size(insn);
            size_t insn_size = source_size;
            memcpy(ctx.dst, ctx.src, source_size);
            funchook_disasm_x86_rip_relative(&disasm, insn, &rel_disp, &rel_imm);
            if (rel_imm.size == 8 && source_size == 2 &&
                (ctx.src[0] == 0xeb || (ctx.src[0] >= 0x70 && ctx.src[0] <= 0x7f))) {
                int32_t displacement = (int32_t)rel_imm.raddr;
                if (rel_imm.addr >= func && rel_imm.addr < func + JUMP32_SIZE) {
                    funchook_set_error_message(funchook, "Short branch enters the hot-patched region");
                    rv = FUNCHOOK_ERROR_FOUND_BACK_JUMP;
                    goto cleanup;
                }
                if (ctx.src[0] == 0xeb) {
                    ctx.dst[0] = 0xe9;
                    rel_imm.offset = 1;
                    insn_size = 5;
                } else {
                    ctx.dst[0] = 0x0f;
                    ctx.dst[1] = 0x80 | (ctx.src[0] & 0x0f);
                    rel_imm.offset = 2;
                    insn_size = 6;
                }
                rel_imm.size = 32;
                memcpy(ctx.dst + rel_imm.offset, &displacement, sizeof(displacement));
            }
""" + BOUND
OLD_ADVANCE = """            ctx.src += insn_size;
            ctx.dst += insn_size;
"""
NEW_ADVANCE = """            ctx.src += source_size;
            ctx.dst += insn_size;
"""
OLD_REL = """    if (rel->size == 32) {
        if (*(int32_t*)(ctx->dst + rel->offset) != (uint32_t)rel->raddr) {
"""
NEW_REL = """    if (rel->size == 32) {
        /* One relocation slot is available besides the final jump-back. */
        if (ctx->rip_disp->disp[1].dst_addr != 0) {
            funchook_set_error_message(ctx->funchook, "Multiple relative operands in the hot-patched region");
            return FUNCHOOK_ERROR_CANNOT_FIX_IP_RELATIVE;
        }
        if (*(int32_t*)(ctx->dst + rel->offset) != (uint32_t)rel->raddr) {
"""
CHANGES = ((OLD, NEW), (OLD_ADVANCE, NEW_ADVANCE), (OLD_REL, NEW_REL))
MEMORY_CHANGES = (
    ("static int funchook_write_jump32(funchook_t *funchook,", """/* Mixed Atmospheres, 2026-10-16: x64 code operands can be unaligned. */
static int32_t ma_funchook_read_s32(const void *pointer)
{
    int32_t value;
    memcpy(&value, pointer, sizeof(value));
    return value;
}

static int funchook_write_jump32(funchook_t *funchook,""", 1),
    ("    *(int*)(out + 1) = (int)(dst - (src + 5));", """    int displacement = (int)(dst - (src + 5));
    memcpy(out + 1, &displacement, sizeof(displacement));""", 1),
    ("    *(const uint8_t**)(src + 6) = dst;", "    memcpy(src + 6, &dst, sizeof(dst));", 1),
    ("*(int32_t*)(ctx->dst + rel->offset)", "ma_funchook_read_s32(ctx->dst + rel->offset)", 2),
    ("    uint32_t *offset_addr;", "    uint8_t *offset_addr;\n    uint32_t displacement;", 1),
    ("    offset_addr = (uint32_t*)(entry->trampoline + disp->disp[0].pos_offset);\n"
     "    *offset_addr = (uint32_t)(disp->disp[0].dst_addr - src_addr);",
     "    offset_addr = entry->trampoline + disp->disp[0].pos_offset;\n"
     "    displacement = (uint32_t)(disp->disp[0].dst_addr - src_addr);\n"
     "    memcpy(offset_addr, &displacement, sizeof(displacement));", 1),
    ("        offset_addr = (uint32_t*)(entry->trampoline + disp->disp[1].pos_offset);\n"
     "        *offset_addr = (uint32_t)(disp->disp[1].dst_addr - src_addr);",
     "        offset_addr = entry->trampoline + disp->disp[1].pos_offset;\n"
     "        displacement = (uint32_t)(disp->disp[1].dst_addr - src_addr);\n"
     "        memcpy(offset_addr, &displacement, sizeof(displacement));", 1),
)


def patch(path: Path) -> None:
    source = path.read_text(encoding="utf-8")
    for old, new, count in reversed(MEMORY_CHANGES):
        if new in source:
            if source.count(new) != count:
                raise ValueError("Ambiguous unaligned-access patch")
            source = source.replace(new, old)
    # Upgrade only the exact previous local extension in existing build caches.
    # Reversing it must still recover the pinned unmodified upstream hash.
    previous = NEW.removesuffix(BOUND)
    if NEW not in source and previous in source:
        source = source.replace(previous, NEW)
    original = source
    for old, new in reversed(CHANGES):
        if new in original:
            if original.count(new) != 1:
                raise ValueError("Ambiguous patched source")
            original = original.replace(new, old)
    if hashlib.sha256(original.encode()).hexdigest() != ORIGINAL_SHA256:
        raise ValueError("Funchook source differs from pinned 1.1.3")
    modified = original
    for old, new in CHANGES:
        if modified.count(old) != 1:
            raise ValueError("Funchook patch context differs")
        modified = modified.replace(old, new)
    for old, new, count in MEMORY_CHANGES:
        if modified.count(old) != count:
            raise ValueError("Funchook unaligned-access patch context differs")
        modified = modified.replace(old, new)
    if modified != path.read_text(encoding="utf-8"):
        path.write_text(modified, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    patch(Path(sys.argv[1]))
