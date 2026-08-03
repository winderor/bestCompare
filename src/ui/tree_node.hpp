#ifndef BESTCOMPARE_TREE_NODE_HPP
#define BESTCOMPARE_TREE_NODE_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <sstream>
#include "../core/diff_engine.hpp"

namespace BestCompare {

struct TreeNode {
    std::wstring name;
    std::wstring relativePath;
    bool isDirectory = false;
    bool isExpanded = false; // Folders collapsed by default
    DiffItem diffData;
    
    // Derived overall status for parent directories
    DiffStatus aggregatedStatus = DiffStatus::Equal;
    
    std::map<std::wstring, std::shared_ptr<TreeNode>> children;

    static std::shared_ptr<TreeNode> BuildTree(const std::vector<DiffItem>& items) {
        auto root = std::make_shared<TreeNode>();
        root->name = L"Root";
        root->relativePath = L"";
        root->isDirectory = true;

        for (const auto& item : items) {
            std::wstringstream ss(item.relativePath);
            std::wstring segment;
            std::vector<std::wstring> parts;
            while (std::getline(ss, segment, L'/')) {
                if (!segment.empty()) {
                    parts.push_back(segment);
                }
            }

            if (parts.empty()) continue;

            std::shared_ptr<TreeNode> current = root;
            std::wstring currentPath = L"";

            for (size_t i = 0; i < parts.size(); ++i) {
                const auto& part = parts[i];
                bool isLast = (i == parts.size() - 1);
                currentPath += (currentPath.empty() ? L"" : L"/") + part;

                auto it = current->children.find(part);
                if (it == current->children.end()) {
                    auto newNode = std::make_shared<TreeNode>();
                    newNode->name = part;
                    newNode->relativePath = currentPath;
                    newNode->isDirectory = isLast ? item.isDirectory : true;
                    if (isLast) {
                        newNode->diffData = item;
                        newNode->aggregatedStatus = item.status;
                    }
                    current->children[part] = newNode;
                    current = newNode;
                } else {
                    current = it->second;
                    if (isLast) {
                        current->diffData = item;
                        current->aggregatedStatus = item.status;
                        current->isDirectory = item.isDirectory;
                    }
                }
            }
        }

        // Compute aggregated status recursively
        ComputeAggregatedStatus(root);
        return root;
    }

    static void GetVisibleFlatList(const std::shared_ptr<TreeNode>& node, std::vector<std::shared_ptr<TreeNode>>& list) {
        if (!node) return;
        for (const auto& pair : node->children) {
            const auto& child = pair.second;
            list.push_back(child);
            if (child->isDirectory && child->isExpanded) {
                GetVisibleFlatList(child, list);
            }
        }
    }

private:
    static void ComputeAggregatedStatus(const std::shared_ptr<TreeNode>& node) {
        if (node->children.empty()) return;

        bool hasModified = false;
        bool hasLeftOnly = false;
        bool hasRightOnly = false;

        for (auto& pair : node->children) {
            ComputeAggregatedStatus(pair.second);
            if (pair.second->aggregatedStatus == DiffStatus::Modified) hasModified = true;
            else if (pair.second->aggregatedStatus == DiffStatus::LeftOnly) hasLeftOnly = true;
            else if (pair.second->aggregatedStatus == DiffStatus::RightOnly) hasRightOnly = true;
        }

        if (hasModified || (hasLeftOnly && hasRightOnly)) {
            node->aggregatedStatus = DiffStatus::Modified;
        } else if (hasLeftOnly) {
            node->aggregatedStatus = DiffStatus::LeftOnly;
        } else if (hasRightOnly) {
            node->aggregatedStatus = DiffStatus::RightOnly;
        } else {
            node->aggregatedStatus = DiffStatus::Equal;
        }
    }
};

} // namespace BestCompare

#endif // BESTCOMPARE_TREE_NODE_HPP
