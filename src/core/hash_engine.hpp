#ifndef BESTCOMPARE_HASH_ENGINE_HPP
#define BESTCOMPARE_HASH_ENGINE_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <array>
#include <cstring>

namespace BestCompare {

class HashEngine {
public:
    // Fast non-cryptographic 64-bit seed-based hash (xxHash3 derivative)
    static uint64_t HashBuffer(const void* buffer, size_t length, uint64_t seed = 0) {
        const uint8_t* data = static_cast<const uint8_t*>(buffer);
        uint64_t hash = seed ^ 0x9E3779B97F4A7C15ULL;
        
        size_t i = 0;
        for (; i + 8 <= length; i += 8) {
            uint64_t val;
            std::memcpy(&val, data + i, 8);
            hash ^= val;
            hash = (hash << 31) | (hash >> 33);
            hash *= 0xC6A4A7935BD1E995ULL;
        }
        
        for (; i < length; ++i) {
            hash ^= static_cast<uint64_t>(data[i]);
            hash *= 0x100000001B3ULL;
        }
        
        return hash;
    }

    // Computes partial hash (Header 4KB + Middle 4KB + Tail 4KB) for rapid early matching
    static uint64_t ComputePartialFileHash(const std::wstring& filePath, uint64_t fileSize) {
        if (fileSize == 0) return 0;
        
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return 0;

        constexpr size_t CHUNK_SIZE = 4096;
        std::array<char, CHUNK_SIZE * 3> buffer{};
        size_t totalBytesRead = 0;

        if (fileSize <= CHUNK_SIZE * 3) {
            file.read(buffer.data(), fileSize);
            totalBytesRead = static_cast<size_t>(file.gcount());
        } else {
            // 1. Read Header 4KB
            file.read(buffer.data(), CHUNK_SIZE);
            totalBytesRead += file.gcount();

            // 2. Read Middle 4KB
            file.seekg(fileSize / 2, std::ios::beg);
            file.read(buffer.data() + CHUNK_SIZE, CHUNK_SIZE);
            totalBytesRead += file.gcount();

            // 3. Read Tail 4KB
            file.seekg(fileSize - CHUNK_SIZE, std::ios::beg);
            file.read(buffer.data() + CHUNK_SIZE * 2, CHUNK_SIZE);
            totalBytesRead += file.gcount();
        }

        return HashBuffer(buffer.data(), totalBytesRead, fileSize);
    }
};

} // namespace BestCompare

#endif // BESTCOMPARE_HASH_ENGINE_HPP
