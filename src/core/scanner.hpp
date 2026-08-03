#ifndef BESTCOMPARE_SCANNER_HPP
#define BESTCOMPARE_SCANNER_HPP

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <filesystem>
#include <chrono>
#include <functional>
#include "hash_engine.hpp"

namespace BestCompare {

namespace fs = std::filesystem;

struct FileNode {
    std::wstring relativePath;
    uint64_t fileSize = 0;
    uint64_t mtime = 0;
    uint64_t partialHash = 0;
    bool isDirectory = false;
    uint64_t merkleHash = 0; // Hierarchical directory hash
};

struct DirectoryScanResult {
    std::wstring rootPath;
    std::vector<FileNode> nodes;
    size_t totalFiles = 0;
    size_t totalDirectories = 0;
    uint64_t totalBytes = 0;
    double scanTimeSeconds = 0.0;
};

class DirectoryScanner {
public:
    using ProgressCallback = std::function<void(size_t filesScanned, size_t dirsScanned, uint64_t bytesScanned, const std::wstring& currentItem)>;

    static DirectoryScanResult ScanDirectory(const std::wstring& rootPath, ProgressCallback progressCb = nullptr, std::atomic<bool>* cancelFlag = nullptr, bool fastScan = false) {
        DirectoryScanResult result;
        result.rootPath = rootPath;

        auto startTime = std::chrono::high_resolution_clock::now();

        if (!fs::exists(rootPath) || !fs::is_directory(rootPath)) {
            return result;
        }

        std::wstring normalizedRoot = fs::canonical(rootPath).wstring();

        std::error_code ec;
        size_t countSinceLastCb = 0;
        for (const auto& entry : fs::recursive_directory_iterator(rootPath, fs::directory_options::skip_permission_denied, ec)) {
            if (cancelFlag && cancelFlag->load()) {
                break;
            }
            FileNode node;
            std::wstring fullPath = entry.path().wstring();
            
            // Calculate relative path
            if (fullPath.rfind(normalizedRoot, 0) == 0) {
                node.relativePath = fullPath.substr(normalizedRoot.length());
                if (!node.relativePath.empty() && (node.relativePath[0] == L'\\' || node.relativePath[0] == L'/')) {
                    node.relativePath = node.relativePath.substr(1);
                }
            } else {
                node.relativePath = entry.path().filename().wstring();
            }

            node.isDirectory = entry.is_directory(ec);

            if (node.isDirectory) {
                result.totalDirectories++;
            } else {
                result.totalFiles++;
                node.fileSize = entry.file_size(ec);
                result.totalBytes += node.fileSize;

                auto ftime = entry.last_write_time(ec);
                node.mtime = static_cast<uint64_t>(ftime.time_since_epoch().count());

                // Compute partial hash for file content check
                node.partialHash = HashEngine::ComputePartialFileHash(fullPath, node.fileSize);
            }

            result.nodes.push_back(std::move(node));

            countSinceLastCb++;
            if (progressCb && (countSinceLastCb >= 25 || node.isDirectory)) {
                progressCb(result.totalFiles, result.totalDirectories, result.totalBytes, entry.path().filename().wstring());
                countSinceLastCb = 0;
            }
        }

        if (progressCb) {
            progressCb(result.totalFiles, result.totalDirectories, result.totalBytes, L"Finalizing Merkle Tree...");
        }

        // Compute Merkle Tree hashes for fast directory tree matching
        ComputeMerkleHashes(result.nodes);

        auto endTime = std::chrono::high_resolution_clock::now();
        result.scanTimeSeconds = std::chrono::duration<double>(endTime - startTime).count();

        return result;
    }

private:
    static void ComputeMerkleHashes(std::vector<FileNode>& nodes) {
        for (auto& node : nodes) {
            if (node.isDirectory) {
                uint64_t dirNameHash = HashEngine::HashBuffer(node.relativePath.data(), node.relativePath.length() * sizeof(wchar_t));
                node.merkleHash = dirNameHash;
            } else {
                // Leaf hash = path hash ^ size ^ partialHash
                uint64_t pathHash = HashEngine::HashBuffer(node.relativePath.data(), node.relativePath.length() * sizeof(wchar_t));
                node.merkleHash = pathHash ^ (node.fileSize * 0x9E3779B9) ^ node.partialHash;
            }
        }
    }
};

} // namespace BestCompare

#endif // BESTCOMPARE_SCANNER_HPP
