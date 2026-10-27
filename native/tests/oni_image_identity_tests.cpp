// Normalised SimDLL identity (native/src/oni_image_identity.hpp). The
// fixtures and expected identities are the same as
// tools/tests/test_oni_hook_profiles.py, so the C++ and Python
// implementations agree byte for byte (docs/game-update-resilience.md).
#include "oni_image_identity.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr char pe_identity[] = "46826a46eff2245d599daf8a4d07190d561347d63c188e18e265bea5c6838a25";
constexpr char elf_identity[] = "78245165da47cdd954305d070ccdf123e9fa4721c9433930335c0147f399f623";

void put(std::vector<std::uint8_t>& image, std::size_t at, std::uint64_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i) image[at + i] = static_cast<std::uint8_t>(value >> (8U * i));
}
std::vector<std::uint8_t> pattern(std::size_t size) {
    std::vector<std::uint8_t> image(size);
    for (std::size_t i = 0; i < size; ++i) image[i] = static_cast<std::uint8_t>((i * 7 + 3) & 0xFFU);
    return image;
}
std::vector<std::uint8_t> pe(std::uint32_t stamp = 0x11111111U, std::uint32_t checksum = 0x22222222U, std::uint8_t guid = 0x33, int code = -1) {
    auto image = pattern(0x400);
    image[0] = 'M'; image[1] = 'Z'; put(image, 60, 0x80, 4); std::memcpy(image.data() + 0x80, "PE\0\0", 4);
    put(image, 0x84, 0x8664, 2); put(image, 0x86, 1, 2); put(image, 0x88, stamp, 4); put(image, 0x8C, 0, 4); put(image, 0x90, 0, 4);
    put(image, 0x94, 240, 2); put(image, 0x96, 0x2022, 2);
    put(image, 0x98, 0x20B, 2); put(image, 0x98 + 64, checksum, 4); put(image, 0x98 + 108, 16, 4);
    for (unsigned index = 0; index < 16; ++index) {
        const std::uint32_t rva = index == 0 ? 0x1000U : index == 6 ? 0x1100U : 0U, size = index == 0 ? 40U : index == 6 ? 28U : 0U;
        put(image, 0x98 + 112 + 8 * index, rva, 4); put(image, 0x98 + 116 + 8 * index, size, 4);
    }
    std::memcpy(image.data() + 0x188, ".text\0\0\0", 8);
    put(image, 0x188 + 8, 0x200, 4); put(image, 0x188 + 12, 0x1000, 4); put(image, 0x188 + 16, 0x200, 4); put(image, 0x188 + 20, 0x200, 4);
    put(image, 0x204, stamp, 4);
    put(image, 0x300, 0, 4); put(image, 0x304, stamp, 4); put(image, 0x308, 0, 2); put(image, 0x30A, 0, 2);
    put(image, 0x30C, 2, 4); put(image, 0x310, 24, 4); put(image, 0x314, 0x1180, 4); put(image, 0x318, 0x380, 4);
    std::memcpy(image.data() + 0x380, "RSDS", 4); std::memset(image.data() + 0x384, guid, 16); put(image, 0x394, 7, 4);
    if (code >= 0) image[0x3F0] = static_cast<std::uint8_t>(code);
    return image;
}
std::vector<std::uint8_t> elf(std::uint8_t build_id = 0x44, int code = -1) {
    auto image = pattern(0x200);
    std::memcpy(image.data(), "\x7f" "ELF\x02\x01\x01", 7);
    put(image, 16, 3, 2); put(image, 18, 62, 2); put(image, 20, 1, 4); put(image, 32, 64, 8);
    put(image, 52, 64, 2); put(image, 54, 56, 2); put(image, 56, 1, 2);
    put(image, 64, 4, 4); put(image, 68, 4, 4); put(image, 72, 0x100, 8); put(image, 80, 0x100, 8); put(image, 88, 0x100, 8);
    put(image, 96, 36, 8); put(image, 104, 36, 8); put(image, 112, 4, 8);
    put(image, 0x100, 4, 4); put(image, 0x104, 20, 4); put(image, 0x108, 3, 4); std::memcpy(image.data() + 0x10C, "GNU\0", 4);
    std::memset(image.data() + 0x110, build_id, 20);
    if (code >= 0) image[0x1F0] = static_cast<std::uint8_t>(code);
    return image;
}
void check(bool condition, const char* what) {
    if (!condition) { std::fprintf(stderr, "FAILED: %s\n", what); std::exit(1); }
}
bool refused(std::vector<std::uint8_t> image) {
    try { (void)ma::oni::normalized_image_identity(std::move(image)); return false; }
    catch (const std::runtime_error&) { return true; }
}
} // namespace

int main() {
    using ma::oni::normalized_image_identity;
    const auto base_pe = normalized_image_identity(pe());
    check(base_pe == pe_identity, "PE identity matches the Python implementation");
    check(normalized_image_identity(pe(0x5EED, 9, 0x99)) == base_pe, "PE build stamps are ignored");
    check(normalized_image_identity(pe(0x11111111U, 0x22222222U, 0x33, 0)) != base_pe, "a PE code byte changes the identity");
    const auto base_elf = normalized_image_identity(elf());
    check(base_elf == elf_identity, "ELF identity matches the Python implementation");
    check(normalized_image_identity(elf(0x01)) == base_elf, "ELF build-id is ignored");
    check(normalized_image_identity(elf(0x44, 0)) != base_elf, "an ELF code byte changes the identity");
    check(refused({}), "empty image refused");
    auto truncated = pe(); truncated.resize(0x300);
    check(refused(truncated), "truncated PE refused");
    auto elf_short = elf(); elf_short.resize(0x110);
    check(refused(elf_short), "truncated ELF note refused");
    check(refused(std::vector<std::uint8_t>{'p', 'l', 'a', 'i', 'n'}), "unknown format refused");
    std::puts("native image identity: all checks passed");
    return 0;
}
