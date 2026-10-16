#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace ma::oni {
// SHA-256 as specified by FIPS 180-4. Used for identity, never for passwords.
class Sha256 {
    std::array<std::uint32_t, 8> state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t used_{};
    std::uint64_t bytes_{};
    void block(const std::uint8_t* data) {
        constexpr std::uint32_t k[]{
            0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
            0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
            0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
            0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
            0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
            0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
            0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
            0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(data[4*i]) << 24U) | (std::uint32_t(data[4*i+1]) << 16U)
                 | (std::uint32_t(data[4*i+2]) << 8U) | std::uint32_t(data[4*i+3]);
        for (std::size_t i = 16; i < 64; ++i) {
            const auto a = std::rotr(w[i-15],7) ^ std::rotr(w[i-15],18) ^ (w[i-15] >> 3U);
            const auto b = std::rotr(w[i-2],17) ^ std::rotr(w[i-2],19) ^ (w[i-2] >> 10U);
            w[i] = w[i-16] + a + w[i-7] + b;
        }
        auto [a,b,c,d,e,f,g,h] = state_;
        for (std::size_t i = 0; i < 64; ++i) {
            const auto t1 = h + (std::rotr(e,6) ^ std::rotr(e,11) ^ std::rotr(e,25))
                + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const auto t2 = (std::rotr(a,2) ^ std::rotr(a,13) ^ std::rotr(a,22))
                + ((a & b) ^ (a & c) ^ (b & c));
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
        state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
    }
public:
    void update(const void* input, std::size_t count) {
        const auto* data = static_cast<const std::uint8_t*>(input);
        bytes_ += count;
        for (std::size_t i = 0; i < count; ++i) {
            buffer_[used_++] = data[i];
            if (used_ == 64) { block(buffer_.data()); used_ = 0; }
        }
    }
    std::string finish() {
        const auto bits = bytes_ * 8U;
        const std::uint8_t one=0x80, zero=0;
        update(&one, 1);
        while (used_ != 56) update(&zero, 1);
        std::array<std::uint8_t,8> length{};
        for (std::size_t i=0; i<8; ++i) length[7-i] = static_cast<std::uint8_t>(bits >> (i*8));
        update(length.data(), length.size());
        constexpr char hex[]="0123456789abcdef";
        std::string result;
        result.reserve(64);
        for (auto value : state_)
            for (int shift=28; shift>=0; shift-=4) result.push_back(hex[(value >> shift) & 15U]);
        return result;
    }
};
inline std::string hash_memory(const void* data, std::size_t count) {
    Sha256 hash; hash.update(data,count); return hash.finish();
}
inline std::string hash_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read required game image");
    Sha256 hash;
    std::array<char,65536> buffer{};
    while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount())
        hash.update(buffer.data(), static_cast<std::size_t>(file.gcount()));
    if (!file.eof()) throw std::runtime_error("Incomplete game image read");
    return hash.finish();
}
}
