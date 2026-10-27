#pragma once
// Normalised identity of a native game image (docs/game-update-resilience.md):
// SHA-256 with only the linker's build stamps zeroed. Mirrors
// tools/oni_hook_profiles.py normalized_identity byte for byte; both are
// checked against the same fixtures (native/tests/oni_image_identity_tests.cpp,
// tools/tests/test_oni_hook_profiles.py).
#include "oni_sha256.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace ma::oni {
namespace image_identity_detail {
struct Image {
    std::vector<std::uint8_t>& bytes;
    void require(bool condition, const char* reason) const {
        if (!condition) throw std::runtime_error(std::string("Cannot normalise native image: ") + reason);
    }
    std::uint64_t read(std::uint64_t at, unsigned size) const {
        require(at <= bytes.size() && size <= bytes.size() - at, "truncated field");
        std::uint64_t value = 0;
        for (unsigned i = 0; i < size; ++i) value |= std::uint64_t{bytes[at + i]} << (8U * i);
        return value;
    }
    std::uint32_t u16(std::uint64_t at) const { return static_cast<std::uint32_t>(read(at, 2)); }
    std::uint32_t u32(std::uint64_t at) const { return static_cast<std::uint32_t>(read(at, 4)); }
    std::uint64_t u64(std::uint64_t at) const { return read(at, 8); }
    void zero(std::uint64_t at, std::uint64_t count) const {
        require(at <= bytes.size() && count <= bytes.size() - at, "range outside the image");
        std::memset(bytes.data() + at, 0, count);
    }
};

inline void normalise_pe(const Image& image) {
    const auto pe = image.u32(60);
    image.require(image.u32(pe) == 0x00004550U, "missing PE signature");
    const auto count = image.u16(pe + 6), optional_size = image.u16(pe + 20);
    const std::uint64_t optional = pe + 24;
    const auto magic = image.u16(optional);
    image.require(magic == 0x10BU || magic == 0x20BU, "unknown optional header");
    const std::uint64_t directories = optional + (magic == 0x10BU ? 96U : 112U);
    const auto directory_count = image.u32(optional + (magic == 0x10BU ? 92U : 108U));
    struct Section { std::uint64_t address, extent, raw, raw_size; };
    std::vector<Section> sections;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto at = optional + optional_size + 40ULL * index;
        const auto virtual_size = image.u32(at + 8), raw_size = image.u32(at + 16);
        sections.push_back({image.u32(at + 12), virtual_size > raw_size ? virtual_size : raw_size, image.u32(at + 20), raw_size});
    }
    const auto offset = [&](std::uint64_t rva, std::uint64_t size) {
        for (const auto& section : sections) {
            const auto limit = section.address + (section.extent < section.raw_size ? section.extent : section.raw_size);
            if (section.address <= rva && rva + size <= limit) return section.raw + rva - section.address;
        }
        image.require(false, "directory outside file-backed sections");
        return std::uint64_t{};
    };
    image.zero(pe + 8, 4);
    image.zero(optional + 64, 4);
    for (const std::uint32_t index : {0U, 2U})
        if (index < directory_count && image.u32(directories + 8ULL * index))
            image.zero(offset(image.u32(directories + 8ULL * index), 8) + 4, 4);
    if (6 < directory_count && image.u32(directories + 48)) {
        const auto size = image.u32(directories + 52);
        const auto start = offset(image.u32(directories + 48), size);
        image.require(size % 28U == 0, "debug directory size");
        for (std::uint64_t entry = start; entry < start + size; entry += 28) {
            image.zero(entry + 4, 4);
            const auto length = image.u32(entry + 16), raw = image.u32(entry + 24);
            if (raw) image.zero(raw, length);
        }
    }
}

inline void normalise_elf(const Image& image) {
    const auto header = image.u64(32);
    const auto header_size = image.u16(54), header_count = image.u16(56);
    image.require(header_size == 56, "program header size");
    for (std::uint32_t index = 0; index < header_count; ++index) {
        const auto at = header + static_cast<std::uint64_t>(index) * header_size;
        if (image.u32(at) != 4U) continue; // PT_NOTE
        auto note = image.u64(at + 8);
        const auto end = note + image.u64(at + 32);
        image.require(end >= note && end <= image.bytes.size(), "note outside the image");
        while (note + 12 <= end) {
            const auto name_size = image.u32(note), descriptor_size = image.u32(note + 4), kind = image.u32(note + 8);
            const auto name = note + 12;
            const auto descriptor = name + ((static_cast<std::uint64_t>(name_size) + 3U) & ~std::uint64_t{3});
            image.require(descriptor + descriptor_size <= end, "truncated note");
            if (kind == 3U && name_size == 4 && std::memcmp(image.bytes.data() + name, "GNU", 4) == 0) image.zero(descriptor, descriptor_size);
            note = descriptor + ((static_cast<std::uint64_t>(descriptor_size) + 3U) & ~std::uint64_t{3});
        }
    }
}
} // namespace image_identity_detail

inline std::string normalized_image_identity(std::vector<std::uint8_t> bytes) {
    const image_identity_detail::Image image{bytes};
    if (bytes.size() >= 2 && bytes[0] == 'M' && bytes[1] == 'Z') image_identity_detail::normalise_pe(image);
    else if (bytes.size() >= 6 && std::memcmp(bytes.data(), "\x7f" "ELF\x02\x01", 6) == 0) image_identity_detail::normalise_elf(image);
    else image.require(false, "neither PE nor ELF64");
    return hash_memory(bytes.data(), bytes.size());
}

inline std::string normalized_file_identity(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read native image " + path.string());
    return normalized_image_identity(std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), {}));
}
} // namespace ma::oni
