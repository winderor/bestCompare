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

    static bool ShouldIgnore(const std::wstring& relPath, const std::wstring& filename, const std::wstring& ignoreFilter) {
        if (ignoreFilter.empty()) return false;

        std::wstringstream ss(ignoreFilter);
        std::wstring pattern;
        while (std::getline(ss, pattern, L',')) {
            // Trim whitespace
            size_t first = pattern.find_first_not_of(L" \t");
            if (first == std::wstring::npos) continue;
            size_t last = pattern.find_last_of(L" \t");
            pattern = pattern.substr(first, (last == std::wstring::npos || last < first) ? std::wstring::npos : (last - first + 1));
            if (pattern.empty()) continue;

            // Lowercase string conversion for case-insensitive pattern matching
            std::wstring targetName = filename;
            std::wstring pat = pattern;
            for (auto& c : targetName) c = towlower(c);
            for (auto& c : pat) c = towlower(c);

            // Extension match (*.db, *.tmp, *.bak)
            if (pat.length() > 1 && pat[0] == L'*' && pat[1] == L'.') {
                std::wstring ext = pat.substr(1); // e.g. ".db"
                if (targetName.length() >= ext.length() &&
                    targetName.compare(targetName.length() - ext.length(), ext.length(), ext) == 0) {
                    return true;
                }
            }
            // Direct substring/name match (e.g. .git, thumbs.db)
            else if (targetName == pat || relPath.find(pat) != std::wstring::npos) {
                return true;
            }
        }
        return false;
    }

    static bool MatchesInclude(const std::wstring& relPath, const std::wstring& filename, bool isDirectory, const std::wstring& includeFilter) {
        if (isDirectory) return true; // Always allow traversing directory hierarchy
        if (includeFilter.empty() || includeFilter == L"*") return true;

        std::wstringstream ss(includeFilter);
        std::wstring pattern;
        while (std::getline(ss, pattern, L',')) {
            size_t first = pattern.find_first_not_of(L" \t");
            if (first == std::wstring::npos) continue;
            size_t last = pattern.find_last_of(L" \t");
            pattern = pattern.substr(first, (last == std::wstring::npos || last < first) ? std::wstring::npos : (last - first + 1));
            if (pattern.empty() || pattern == L"*") return true;

            std::wstring targetName = filename;
            std::wstring pat = pattern;
            for (auto& c : targetName) c = towlower(c);
            for (auto& c : pat) c = towlower(c);

            if (pat.length() > 1 && pat[0] == L'*' && pat[1] == L'.') {
                std::wstring ext = pat.substr(1); // e.g. ".cpp"
                if (targetName.length() >= ext.length() &&
                    targetName.compare(targetName.length() - ext.length(), ext.length(), ext) == 0) {
                    return true;
                }
            } else if (targetName == pat || relPath.find(pat) != std::wstring::npos) {
                return true;
            }
        }
        return false;
    }

    static DirectoryScanResult ScanDirectory(const std::wstring& rootPath, ProgressCallback progressCb = nullptr, std::atomic<bool>* cancelFlag = nullptr, bool fastScan = false, const std::wstring& ignoreFilter = L"", const std::wstring& includeFilter = L"*") {
        (void)fastScan;
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
            
            std::wstring filename = entry.path().filename().wstring();
            std::wstring fullPath = entry.path().wstring();

            std::wstring relPath = L"";
            if (fullPath.rfind(normalizedRoot, 0) == 0) {
                relPath = fullPath.substr(normalizedRoot.length());
                if (!relPath.empty() && (relPath[0] == L'\\' || relPath[0] == L'/')) {
                    relPath = relPath.substr(1);
                }
            } else {
                relPath = filename;
            }

            bool isDir = entry.is_directory(ec);

            if (ShouldIgnore(relPath, filename, ignoreFilter)) {
                continue;
            }

            if (!MatchesInclude(relPath, filename, isDir, includeFilter)) {
                continue;
            }

            FileNode node;
            node.relativePath = relPath;
            node.isDirectory = isDir;

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
