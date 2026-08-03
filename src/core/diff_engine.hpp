#ifndef BESTCOMPARE_DIFF_ENGINE_HPP
#define BESTCOMPARE_DIFF_ENGINE_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include "scanner.hpp"

namespace BestCompare {

enum class DiffStatus {
    Equal,       // File/Folder is identical on both PCs
    Modified,    // File exists on both PCs but sizes or hashes differ
    LeftOnly,    // Exists only on PC #1
    RightOnly    // Exists only on PC #2
};

struct DiffItem {
    std::wstring relativePath;
    DiffStatus status = DiffStatus::Equal;
    
    // Left (PC #1) metadata
    bool hasLeft = false;
    uint64_t leftSize = 0;
    uint64_t leftMtime = 0;
    uint64_t leftHash = 0;

    // Right (PC #2) metadata
    bool hasRight = false;
    uint64_t rightSize = 0;
    uint64_t rightMtime = 0;
    uint64_t rightHash = 0;

    bool isDirectory = false;
};

class DiffEngine {
public:
    static std::vector<DiffItem> Compare(const DirectoryScanResult& left, const DirectoryScanResult& right) {
        std::vector<DiffItem> result;
        std::unordered_map<std::wstring, FileNode> rightMap;

        for (const auto& node : right.nodes) {
            rightMap[node.relativePath] = node;
        }

        std::unordered_map<std::wstring, bool> matchedRightPaths;

        // Process all items in Left (PC #1)
        for (const auto& leftNode : left.nodes) {
            DiffItem item;
            item.relativePath = leftNode.relativePath;
            item.isDirectory = leftNode.isDirectory;
            item.hasLeft = true;
            item.leftSize = leftNode.fileSize;
            item.leftMtime = leftNode.mtime;
            item.leftHash = leftNode.merkleHash;

            auto it = rightMap.find(leftNode.relativePath);
            if (it != rightMap.end()) {
                const auto& rightNode = it->second;
                matchedRightPaths[rightNode.relativePath] = true;

                item.hasRight = true;
                item.rightSize = rightNode.fileSize;
                item.rightMtime = rightNode.mtime;
                item.rightHash = rightNode.merkleHash;

                if (leftNode.isDirectory) {
                    item.status = (leftNode.merkleHash == rightNode.merkleHash) ? DiffStatus::Equal : DiffStatus::Modified;
                } else {
                    bool sizeMatches = (leftNode.fileSize == rightNode.fileSize);
                    bool hashMatches = (leftNode.partialHash == 0 || rightNode.partialHash == 0) ? true : (leftNode.partialHash == rightNode.partialHash);
                    
                    if (sizeMatches && hashMatches) {
                        item.status = DiffStatus::Equal;
                    } else {
                        item.status = DiffStatus::Modified;
                    }
                }
            } else {
                item.status = DiffStatus::LeftOnly;
            }

            result.push_back(std::move(item));
        }

        // Process remaining Right Only items
        for (const auto& rightNode : right.nodes) {
            if (!matchedRightPaths[rightNode.relativePath]) {
                DiffItem item;
                item.relativePath = rightNode.relativePath;
                item.isDirectory = rightNode.isDirectory;
                item.hasRight = true;
                item.rightSize = rightNode.fileSize;
                item.rightMtime = rightNode.mtime;
                item.rightHash = rightNode.merkleHash;
                item.status = DiffStatus::RightOnly;

                result.push_back(std::move(item));
            }
        }

        return result;
    }
};

} // namespace BestCompare

#endif // BESTCOMPARE_DIFF_ENGINE_HPP
