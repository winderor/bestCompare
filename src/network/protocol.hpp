#ifndef BESTCOMPARE_PROTOCOL_HPP
#define BESTCOMPARE_PROTOCOL_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace BestCompare {

enum class MessageType : uint32_t {
    Ping = 1,
    ReadyResponse = 2,
    StartScanRequest = 3,
    ScanDataHeader = 4,
    NodeDataChunk = 5,
    ScanComplete = 6,
    Error = 7,
    CancelScanRequest = 8,
    ScanProgressUpdate = 9,
    StartFastScanRequest = 10,
    CopyToRemoteRequest = 11,
    CopyFromRemoteRequest = 12,
    DeleteRemoteRequest = 13,
    FileOpResponse = 14
};

#pragma pack(push, 1)
struct FileOpHeader {
    uint8_t isDirectory;
    uint64_t fileSize;
    uint16_t relPathLength;
    uint32_t contentPayloadLength;
    // Followed by relPath wstring, then file binary content if copying file
};
#pragma pack(pop)

#pragma pack(push, 1)
struct ProgressSerialized {
    uint32_t filesScanned;
    uint32_t dirsScanned;
    uint64_t bytesScanned;
    uint16_t itemLength;
    // Followed immediately by wchar_t item data
};
#pragma pack(pop)

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic = 0x42435032; // 'BCP2' (BestCompare Protocol v2)
    MessageType type;
    uint32_t payloadLength = 0;
};

struct NodeSerialized {
    uint64_t fileSize;
    uint64_t mtime;
    uint64_t partialHash;
    uint64_t merkleHash;
    uint8_t isDirectory;
    uint16_t pathLength;
    // Followed immediately by wchar_t path data
};
#pragma pack(pop)

} // namespace BestCompare

#endif // BESTCOMPARE_PROTOCOL_HPP
