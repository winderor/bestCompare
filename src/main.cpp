#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include "core/scanner.hpp"
#include "core/diff_engine.hpp"
#include "network/network_server.hpp"
#include "network/network_client.hpp"
#include "version.hpp"

using namespace BestCompare;

void DisplayBeyondCompareTree(const std::vector<DiffItem>& diffItems) {
    std::cout << "\n========================================================================================\n";
    std::cout << "                         BestCompare Side-by-Side Diff Tree                             \n";
    std::cout << "========================================================================================\n";
    std::cout << std::left << std::setw(40) << "PC #1 (Local Path)"
              << std::setw(12) << "Status"
              << std::setw(40) << "PC #2 (Remote Path)" << "\n";
    std::cout << "----------------------------------------------------------------------------------------\n";

    size_t equalCount = 0;
    size_t modifiedCount = 0;
    size_t leftOnlyCount = 0;
    size_t rightOnlyCount = 0;

    for (const auto& item : diffItems) {
        std::wstring pathStr = item.relativePath;
        std::string narrowPath;
        for (wchar_t wc : pathStr) {
            narrowPath += (wc < 128) ? static_cast<char>(wc) : '?';
        }
        if (item.isDirectory) narrowPath += "/";

        std::string leftDisplay = item.hasLeft ? narrowPath + " (" + std::to_string(item.leftSize) + " B)" : "-";
        std::string rightDisplay = item.hasRight ? narrowPath + " (" + std::to_string(item.rightSize) + " B)" : "-";
        std::string statusStr;

        switch (item.status) {
            case DiffStatus::Equal:
                statusStr = "[ EQUAL ]";
                equalCount++;
                break;
            case DiffStatus::Modified:
                statusStr = "[MODIFIED]";
                modifiedCount++;
                break;
            case DiffStatus::LeftOnly:
                statusStr = "[LEFT ONLY]";
                leftOnlyCount++;
                break;
            case DiffStatus::RightOnly:
                statusStr = "[RIGHT ONLY]";
                rightOnlyCount++;
                break;
        }

        std::cout << std::left << std::setw(40) << leftDisplay.substr(0, 38)
                  << std::setw(12) << statusStr
                  << std::setw(40) << rightDisplay.substr(0, 38) << "\n";
    }

    std::cout << "----------------------------------------------------------------------------------------\n";
    std::cout << "Summary: " << equalCount << " Equal | "
              << modifiedCount << " Modified | "
              << leftOnlyCount << " Left-Only | "
              << rightOnlyCount << " Right-Only | Total: " << diffItems.size() << " items\n";
    std::cout << "========================================================================================\n\n";
}

#include "ui/app_window.hpp"

int main(int argc, char* argv[]) {
    // 1. Start background TCP daemon listener on port 9090
    NetworkServer server(9090);
    if (!server.Start()) {
        std::cerr << "Failed to start background server daemon on port 9090.\n";
        return 1;
    }

    // CLI mode execution
    if (argc >= 4) {
        std::string mode = argv[1];
        if (mode == "compare") {
            std::string remoteIp = argv[2];
            std::wstring localPath = fs::path(argv[3]).wstring();
            std::wstring remotePath = (argc >= 5) ? fs::path(argv[4]).wstring() : localPath;

            std::cout << "[Step 1] Checking readiness of remote PC (" << remoteIp << ")... ";
            if (!NetworkClient::CheckRemoteReady(remoteIp)) {
                std::cout << "FAILED!\n";
                return 1;
            }
            std::cout << "READY!\n";

            DirectoryScanResult localResult = DirectoryScanner::ScanDirectory(localPath);
            DirectoryScanResult remoteResult = NetworkClient::RequestRemoteScan(remoteIp, remotePath);
            std::vector<DiffItem> diffItems = DiffEngine::Compare(localResult, remoteResult);

            DisplayBeyondCompareTree(diffItems);
        }
    } else {
        // Launch Graphical User Interface (Option B: Dear ImGui + DirectX 11)
        GUIState state;
        server.SetStatusReporter(&state);
        wchar_t titleBuf[128];
        swprintf_s(titleBuf, L"BestCompare v%.2f - Dual-PC Folder Compare Engine", AppVersion);
        if (AppWindow::Instance().Initialize(GetModuleHandle(NULL), titleBuf, 1280, 768)) {
            AppWindow::Instance().RunLoop(state, server);
        }
    }

    server.Stop();
    return 0;
}

#if defined(_WIN32) && defined(BESTCOMPARE_WIN32_GUI)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return main(__argc, __argv);
}
#endif


