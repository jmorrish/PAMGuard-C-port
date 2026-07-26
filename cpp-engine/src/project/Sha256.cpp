#include "Sha256.h"

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace pamguard::project::detail {

namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

constexpr std::uint32_t rotate_right(
    std::uint32_t value,
    std::uint32_t count) noexcept {
    return (value >> count) | (value << (32U - count));
}

void transform(
    std::array<std::uint32_t, 8>& state,
    const std::uint8_t* block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        const auto offset = index * 4;
        words[index] =
            (static_cast<std::uint32_t>(block[offset]) << 24U) |
            (static_cast<std::uint32_t>(block[offset + 1]) << 16U) |
            (static_cast<std::uint32_t>(block[offset + 2]) << 8U) |
            static_cast<std::uint32_t>(block[offset + 3]);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
        const auto sigma0 =
            rotate_right(words[index - 15], 7U) ^
            rotate_right(words[index - 15], 18U) ^
            (words[index - 15] >> 3U);
        const auto sigma1 =
            rotate_right(words[index - 2], 17U) ^
            rotate_right(words[index - 2], 19U) ^
            (words[index - 2] >> 10U);
        words[index] =
            words[index - 16] + sigma0 +
            words[index - 7] + sigma1;
    }

    auto a = state[0];
    auto b = state[1];
    auto c = state[2];
    auto d = state[3];
    auto e = state[4];
    auto f = state[5];
    auto g = state[6];
    auto h = state[7];

    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto sum1 =
            rotate_right(e, 6U) ^
            rotate_right(e, 11U) ^
            rotate_right(e, 25U);
        const auto choose = (e & f) ^ ((~e) & g);
        const auto temporary1 =
            h + sum1 + choose +
            kRoundConstants[index] + words[index];
        const auto sum0 =
            rotate_right(a, 2U) ^
            rotate_right(a, 13U) ^
            rotate_right(a, 22U);
        const auto majority =
            (a & b) ^ (a & c) ^ (b & c);
        const auto temporary2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

} // namespace

std::string sha256_hex_digest(std::string_view bytes) {
    if (bytes.size() >
        std::numeric_limits<std::uint64_t>::max() / 8U) {
        throw std::invalid_argument("SHA-256 input is too large");
    }

    std::vector<std::uint8_t> padded;
    padded.reserve(bytes.size() + 72);
    for (const auto byte : bytes) {
        padded.push_back(
            static_cast<std::uint8_t>(
                static_cast<unsigned char>(byte)));
    }
    const auto bit_length =
        static_cast<std::uint64_t>(bytes.size()) * 8U;
    padded.push_back(0x80U);
    while ((padded.size() % 64U) != 56U) {
        padded.push_back(0);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        padded.push_back(
            static_cast<std::uint8_t>(
                (bit_length >> static_cast<unsigned int>(shift)) &
                0xFFU));
    }

    std::array<std::uint32_t, 8> state = {
        0x6a09e667U,
        0xbb67ae85U,
        0x3c6ef372U,
        0xa54ff53aU,
        0x510e527fU,
        0x9b05688cU,
        0x1f83d9abU,
        0x5be0cd19U,
    };
    for (std::size_t offset = 0;
         offset < padded.size();
         offset += 64) {
        transform(state, padded.data() + offset);
    }

    constexpr char hex[] = "0123456789abcdef";
    std::string digest;
    digest.reserve(64);
    for (const auto word : state) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            digest.push_back(
                hex[(word >> static_cast<unsigned int>(shift)) &
                    0x0FU]);
        }
    }
    return digest;
}

} // namespace pamguard::project::detail
