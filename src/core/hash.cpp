#include "core/hash.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <limits>
#include <stdexcept>

namespace renderlab
{
std::string Sha256(std::span<const std::byte> bytes)
{
    if (bytes.size() > std::numeric_limits<ULONG>::max())
        throw std::runtime_error("Hash input exceeds 4GiB");
    std::array<unsigned char, 32> digest{};
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                   reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())),
                   static_cast<ULONG>(bytes.size()), digest.data(),
                   static_cast<ULONG>(digest.size())) < 0)
        throw std::runtime_error("SHA-256 failed");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (auto value : digest)
    {
        result += hex[value >> 4];
        result += hex[value & 15];
    }
    return result;
}
std::string Sha256(std::string_view text)
{
    return Sha256(std::as_bytes(std::span{text.data(), text.size()}));
}
} // namespace renderlab
