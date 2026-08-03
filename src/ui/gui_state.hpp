// Dear ImGui: standalone header file for BestCompare UI engine
// Minimal embedded configuration wrapper for ImGui DirectX11/Win32
#ifndef BESTCOMPARE_GUI_ENGINE_HPP
#define BESTCOMPARE_GUI_ENGINE_HPP

#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <set>
#include "../core/scanner.hpp"
#include "../core/diff_engine.hpp"
#include "../network/network_server.hpp"
#include "../network/network_client.hpp"
#include "tree_node.hpp"

namespace BestCompare {

struct GUIState : public IGUIStateReporter {
    char localPath[512] = "D:\\test";
    char remoteIp[128] = "10.100.102.91";
    char remotePath[512] = "D:\\test";
    
    bool isScanning = false;
    std::atomic<bool> cancelScan{false};
    bool activeOperation = false;
    std::string currentOpDetails = "";
    bool showMatches = true;
    bool showDiffs = true;
    bool showOrphans = true;
    std::string statusMessage = "Ready. Select folders or enter remote IP and click 'Run Compare'.";

    std::mutex stateMutex;

    void ReportStatus(const std::string& msg, bool scanning) override {
        std::lock_guard<std::mutex> lock(stateMutex);
        statusMessage = msg;
        isScanning = scanning;
    }
    
    std::vector<DiffItem> diffItems;
    std::shared_ptr<TreeNode> rootNode = nullptr;
    std::set<std::wstring> selectedPaths;
    std::wstring lastSelectedPath;
    std::wstring selectionAnchorPath;

    size_t equalCount = 0;
    size_t modifiedCount = 0;
    size_t leftOnlyCount = 0;
    size_t rightOnlyCount = 0;
    double scanTimeSec = 0.0;
    size_t totalBytes = 0;
};

} // namespace BestCompare

#endif // BESTCOMPARE_GUI_ENGINE_HPP
