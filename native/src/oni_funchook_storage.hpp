#pragma once
#if defined(__linux__)
#include <elf.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

// Private extensions to the hash-pinned Funchook build, not the mod's ABI.
extern "C" {
int ma_funchook_register_trampoline_buffer(void*, std::size_t);
std::size_t ma_funchook_trampoline_capacity();
std::size_t ma_funchook_trampoline_used();
}

namespace ma::oni {
struct TrampolineStorage {
    std::uintptr_t rva, bytes, page_bytes, segment_rva, segment_file_offset, segment_memory_bytes;
};

inline bool verified_trampoline_segment(std::span<const Elf64_Phdr> headers,
                                       const TrampolineStorage& storage) noexcept {
    constexpr auto maximum=std::numeric_limits<std::uintptr_t>::max();
    const auto [first,bytes,page,segment,raw,memory]=storage;
    if (page!=4096 || !bytes || bytes>page || first%16 || !segment || !memory ||
        segment>maximum-memory || first>maximum-bytes || raw%page!=segment%page) return false;
    const auto end=segment+memory, last=first+bytes;
    if (end>maximum-page || first!=((end+15)&~std::uintptr_t{15})) return false;
    if (last!=((end+page-1)&~(page-1)) || first/page!=(last-1)/page) return false;
    unsigned matches{};
    for (const auto& header : headers) {
        if (header.p_type!=PT_LOAD) continue;
        if (header.p_vaddr>maximum-header.p_memsz) return false;
        if (header.p_vaddr==segment && header.p_memsz==memory && header.p_filesz==memory &&
            header.p_offset==raw && header.p_flags==(PF_R|PF_X) && header.p_align==page) {
            ++matches;
        } else if (header.p_vaddr<last && header.p_vaddr+header.p_memsz>first) return false;
    }
    return matches==1;
}
}
#endif
