#ifndef BESTCOMPARE_APP_WINDOW_HPP
#define BESTCOMPARE_APP_WINDOW_HPP

#include <chrono>
#include <d3d11.h>
#include <mutex>
#include <shobjidl.h>
#include <string>
#include <tchar.h>
#include <thread>
#include <vector>
#include <windows.h>

#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"

#include "../core/diff_engine.hpp"
#include "../core/file_ops.hpp"
#include "../core/scanner.hpp"
#include "../network/network_client.hpp"
#include "../network/network_server.hpp"
#include "version.hpp"
#include "gui_state.hpp"
#include "tree_node.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace BestCompare {

inline bool IsHebrewChar(wchar_t ch) {
  return (ch >= 0x0590 && ch <= 0x05FF);
}

inline std::wstring FixBiDiHebrew(const std::wstring& input) {
  std::wstring result = input;
  size_t i = 0;
  size_t len = result.length();

  while (i < len) {
    if (IsHebrewChar(result[i])) {
      size_t start = i;
      while (i < len && (IsHebrewChar(result[i]) || result[i] == L' ' || (result[i] >= L'0' && result[i] <= L'9'))) {
        // Stop if space is followed by non-Hebrew
        if (result[i] == L' ' && (i + 1 >= len || !IsHebrewChar(result[i + 1]))) {
          break;
        }
        i++;
      }
      size_t end = i;
      // Reverse Hebrew segment so ImGui LTR renderer displays it visually correctly
      std::reverse(result.begin() + start, result.begin() + end);
    } else {
      i++;
    }
  }
  return result;
}

inline std::string WStringFormatToUTF8(const std::wstring& wstr) {
  if (wstr.empty()) return std::string();
  std::wstring bidiFixed = FixBiDiHebrew(wstr);
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, &bidiFixed[0], (int)bidiFixed.size(), NULL, 0, NULL, NULL);
  std::string strTo(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, &bidiFixed[0], (int)bidiFixed.size(), &strTo[0], size_needed, NULL, NULL);
  return strTo;
}

class AppWindow {
public:
  static AppWindow &Instance() {
    static AppWindow instance;
    return instance;
  }

  bool Initialize(HINSTANCE hInstance, const wchar_t *title, int width,
                  int height) {
    WNDCLASSEXW wc = {sizeof(wc),
                      CS_CLASSDC,
                      WndProc,
                      0L,
                      0L,
                      hInstance,
                      NULL,
                      NULL,
                      NULL,
                      NULL,
                      L"BestCompareImGuiClass",
                      NULL};
    ::RegisterClassExW(&wc);

    m_hwnd =
        ::CreateWindowW(wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, 100, 100,
                        width, height, NULL, NULL, wc.hInstance, NULL);

    if (!CreateDeviceD3D(m_hwnd)) {
      CleanupDeviceD3D();
      ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
      return false;
    }

    ::ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(m_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    // Build Glyph Ranges for ASCII + Hebrew (0x0590-0x05FF)
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    static const ImWchar hebrewRanges[] = { 0x0590, 0x05FF, 0 };
    builder.AddRanges(hebrewRanges);
    static ImVector<ImWchar> ranges;
    builder.BuildRanges(&ranges);

    // Attempt loading Windows system fonts that support Hebrew (Segoe UI, Arial, Tahoma)
    const char *systemFonts[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf"
    };

    for (const char *fontPath : systemFonts) {
      FILE *f = nullptr;
      if (fopen_s(&f, fontPath, "rb") == 0 && f != nullptr) {
        fclose(f);
        io.Fonts->AddFontFromFileTTF(fontPath, 16.0f, NULL, ranges.Data);
        break;
      }
    }

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_pd3dDevice, m_pd3dDeviceContext);

    return true;
  }

  void RunLoop(GUIState &state, NetworkServer &server) {
    (void)server;
    bool done = false;
    ImVec4 clear_color = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);

    while (!done) {
      MSG msg;
      while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
        if (msg.message == WM_QUIT)
          done = true;
      }
      if (done)
        break;

      if (m_ResizeWidth != 0 && m_ResizeHeight != 0) {
        CleanupRenderTarget();
        m_pSwapChain->ResizeBuffers(0, m_ResizeWidth, m_ResizeHeight,
                                    DXGI_FORMAT_UNKNOWN, 0);
        m_ResizeWidth = m_ResizeHeight = 0;
        CreateRenderTarget();
      }

      ImGui_ImplDX11_NewFrame();
      ImGui_ImplWin32_NewFrame();
      ImGui::NewFrame();

      RenderUI(state);

      ImGui::Render();
      const float clear_color_with_alpha[4] = {
          clear_color.x * clear_color.w, clear_color.y * clear_color.w,
          clear_color.z * clear_color.w, clear_color.w};
      m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, NULL);
      m_pd3dDeviceContext->ClearRenderTargetView(m_mainRenderTargetView,
                                                 clear_color_with_alpha);
      ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

      m_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(m_hwnd);
  }

private:
  AppWindow() = default;

  static std::string OpenFolderPickerWin32(HWND parent) {
    std::string result = "";
    IFileOpenDialog *pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL,
                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
      DWORD dwOptions;
      if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
        pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);
      }
      if (SUCCEEDED(pfd->Show(parent))) {
        IShellItem *psi = nullptr;
        if (SUCCEEDED(pfd->GetResult(&psi))) {
          PWSTR pszPath = nullptr;
          if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
            std::wstring ws(pszPath);
            result = "";
            for (wchar_t wc : ws) {
              result += (wc < 128) ? static_cast<char>(wc) : '?';
            }
            CoTaskMemFree(pszPath);
          }
          psi->Release();
        }
      }
      pfd->Release();
    }
    return result;
  }

  void StartAsyncCompare(GUIState &state, bool fastScan = false) {
    state.isScanning = true;
    state.cancelScan = false;
    state.statusMessage = std::string(fastScan ? "[FAST REFRESH] " : "[FULL COMPARE] ") +
        "Connecting to PC #2 and scanning target directories...";
    state.selectedPaths.clear();
    state.diffItems.clear();
    state.rootNode = nullptr;
    state.equalCount = 0;
    state.modifiedCount = 0;
    state.leftOnlyCount = 0;
    state.rightOnlyCount = 0;

    std::string ip = state.remoteIp;
    std::string localPStr = state.localPath;
    std::string remotePStr = state.remotePath;

    std::thread([this, &state, ip, localPStr, remotePStr, fastScan]() {
      if (!NetworkClient::CheckRemoteReady(ip, &state.cancelScan)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (state.cancelScan) {
          state.statusMessage = "[CANCELLED] Connection attempt cancelled by user.";
        } else {
          state.statusMessage = "Error: Could not connect to remote PC at " + ip +
                                " (Daemon not running or blocked).";
        }
        state.isScanning = false;
        return;
      }

      std::wstring localP(localPStr.begin(), localPStr.end());
      std::wstring remoteP(remotePStr.begin(), remotePStr.end());
      std::string ignoreStr(state.ignorePatterns);
      std::wstring ignoreFilter(ignoreStr.begin(), ignoreStr.end());
      std::string includeStr(state.includePatterns);
      std::wstring includeFilter(includeStr.begin(), includeStr.end());

      auto start = std::chrono::high_resolution_clock::now();

      DirectoryScanResult localRes;
      DirectoryScanResult remoteRes;

      {
        std::lock_guard<std::mutex> lock(m_mutex);
        state.statusMessage = "[SCANNING] Scanning local & remote file trees in parallel...";
      }

      // Launch local scan and remote network scan concurrently in parallel
      std::thread localThread([this, &state, localP, &localRes, fastScan, ignoreFilter, includeFilter]() {
        localRes = DirectoryScanner::ScanDirectory(localP, [this, &state](size_t files, size_t dirs, uint64_t bytes, const std::wstring& currentItem) {
          std::string narrowItem = WStringFormatToUTF8(currentItem);
          (void)bytes;
          std::lock_guard<std::mutex> lock(m_mutex);
          state.statusMessage = "[LOCAL SCANNING] Files: " + std::to_string(files) + ", Dirs: " + std::to_string(dirs) + " | Current: " + narrowItem;
        }, &state.cancelScan, fastScan, ignoreFilter, includeFilter);
      });

      std::thread remoteThread([this, &state, ip, remoteP, &remoteRes, fastScan, ignoreFilter, includeFilter]() {
        remoteRes = NetworkClient::RequestRemoteScan(ip, remoteP, [this, &state](size_t files, size_t dirs, uint64_t bytes, const std::wstring& currentItem) {
          std::string narrowItem = WStringFormatToUTF8(currentItem);
          (void)bytes;
          std::lock_guard<std::mutex> lock(m_mutex);
          state.statusMessage = "[REMOTE SCANNING] Files: " + std::to_string(files) + ", Dirs: " + std::to_string(dirs) + " | Current: " + narrowItem;
        }, &state.cancelScan, fastScan, ignoreFilter, includeFilter);
      });

      // Synchronize parallel scanning threads (Wait until both sides finish scanning)
      localThread.join();
      remoteThread.join();

      if (state.cancelScan) {
        std::lock_guard<std::mutex> lock(m_mutex);
        state.statusMessage = "[CANCELLED] Scan cancelled by user.";
        state.isScanning = false;
        return;
      }

      // Build complete tree and compare only when both sides have finished scanning
      {
        std::lock_guard<std::mutex> lock(m_mutex);
        state.statusMessage = "[COMPARING] Parallel scan complete. Synchronizing side-by-side comparison tree...";
        state.diffItems = DiffEngine::Compare(localRes, remoteRes);
        state.rootNode = TreeNode::BuildTree(state.diffItems);
      }
      auto end = std::chrono::high_resolution_clock::now();

      std::lock_guard<std::mutex> lock(m_mutex);
      state.scanTimeSec = std::chrono::duration<double>(end - start).count();

      state.equalCount = 0;
      state.modifiedCount = 0;
      state.leftOnlyCount = 0;
      state.rightOnlyCount = 0;

      for (const auto &item : state.diffItems) {
        if (item.status == DiffStatus::Equal)
          state.equalCount++;
        else if (item.status == DiffStatus::Modified)
          state.modifiedCount++;
        else if (item.status == DiffStatus::LeftOnly)
          state.leftOnlyCount++;
        else if (item.status == DiffStatus::RightOnly)
          state.rightOnlyCount++;
      }

      state.statusMessage =
          "Scan complete! Duration: " + std::to_string(state.scanTimeSec) +
          "s.";
      state.isScanning = false;
    }).detach();
  }

  void ExecuteBatchCopyLeftToRight(GUIState &state) {
    if (state.selectedPaths.empty() || state.activeOperation)
      return;
    state.activeOperation = true;
    state.isScanning = true;

    std::set<std::wstring> targets = state.selectedPaths;
    std::string ip = state.remoteIp;
    fs::path localRoot = state.localPath;
    std::wstring remoteRootStr = std::wstring(state.remotePath, state.remotePath + strlen(state.remotePath));

    std::thread([this, &state, targets, ip, localRoot, remoteRootStr]() {
      size_t count = 0;
      size_t total = targets.size();

      for (const auto &relPath : targets) {
        std::string narrowRel = WStringFormatToUTF8(relPath);

        {
          std::lock_guard<std::mutex> lock(m_mutex);
          state.statusMessage = "[COPYING LEFT -> RIGHT] (" + std::to_string(count + 1) + "/" + std::to_string(total) + "): " + narrowRel;
        }

        fs::path localSrc = localRoot / relPath;
        if (NetworkClient::RequestRemoteCopyFile(ip, remoteRootStr, relPath, localSrc, true))
          count++;
      }

      {
        std::lock_guard<std::mutex> lock(m_mutex);
        state.statusMessage = "Copied " + std::to_string(count) + " / " + std::to_string(total) + " items [Left -> Right]. Refreshing scan...";
      }

      state.activeOperation = false;
      StartAsyncCompare(state, true);
    }).detach();
  }

  void ExecuteBatchCopyRightToLeft(GUIState &state) {
    if (state.selectedPaths.empty() || state.activeOperation)
      return;
    state.activeOperation = true;
    state.isScanning = true;

    std::set<std::wstring> targets = state.selectedPaths;
    std::string ip = state.remoteIp;
    fs::path localRoot = state.localPath;
    std::wstring remoteRootStr = std::wstring(state.remotePath, state.remotePath + strlen(state.remotePath));

    std::thread([this, &state, targets, ip, localRoot, remoteRootStr]() {
      size_t count = 0;
      size_t total = targets.size();

      for (const auto &relPath : targets) {
        std::string narrowRel = WStringFormatToUTF8(relPath);

        {
          std::lock_guard<std::mutex> lock(m_mutex);
          state.statusMessage = "[COPYING RIGHT -> LEFT] (" + std::to_string(count + 1) + "/" + std::to_string(total) + "): " + narrowRel;
        }

        fs::path localDst = localRoot / relPath;
        if (NetworkClient::RequestRemoteCopyFile(ip, remoteRootStr, relPath, localDst, false))
          count++;
      }

      {
        std::lock_guard<std::mutex> lock(m_mutex);
        state.statusMessage = "Copied " + std::to_string(count) + " / " + std::to_string(total) + " items [Right -> Left]. Refreshing scan...";
      }

      state.activeOperation = false;
      StartAsyncCompare(state, true);
    }).detach();
  }

  void ExecuteBatchDeleteLeft(GUIState &state) {
    if (state.selectedPaths.empty() || state.activeOperation)
      return;
    state.activeOperation = true;
    state.isScanning = true;

    std::set<std::wstring> targets = state.selectedPaths;
    fs::path localRoot = state.localPath;

    std::thread([this, &state, targets, localRoot]() {
      size_t count = 0;
      size_t total = targets.size();

      for (const auto &relPath : targets) {
        std::string narrowRel = WStringFormatToUTF8(relPath);

        {
          std::lock_guard<std::mutex> lock(m_mutex);
          state.statusMessage = "[DELETING LOCAL] (" + std::to_string(count + 1) + "/" + std::to_string(total) + "): " + narrowRel;
        }

        fs::path target = localRoot / relPath;
        if (FileOps::DeletePath(target))
          count++;
      }

      {
        std::lock_guard<std::mutex> lock(m_mutex);
        state.statusMessage = "Deleted " + std::to_string(count) + " / " + std::to_string(total) + " items [Local]. Refreshing scan...";
      }

      state.activeOperation = false;
      StartAsyncCompare(state, true);
    }).detach();
  }

  void ExecuteBatchDeleteRight(GUIState &state) {
    if (state.selectedPaths.empty() || state.activeOperation)
      return;
    state.activeOperation = true;
    state.isScanning = true;

    std::set<std::wstring> targets = state.selectedPaths;
    std::string ip = state.remoteIp;
    std::wstring remoteRootStr = std::wstring(state.remotePath, state.remotePath + strlen(state.remotePath));

    std::thread([this, &state, targets, ip, remoteRootStr]() {
      size_t count = 0;
      size_t total = targets.size();

      for (const auto &relPath : targets) {
        std::string narrowRel = WStringFormatToUTF8(relPath);

        {
          std::lock_guard<std::mutex> lock(m_mutex);
          state.statusMessage = "[DELETING REMOTE] (" + std::to_string(count + 1) + "/" + std::to_string(total) + "): " + narrowRel;
        }

        if (NetworkClient::RequestRemoteDelete(ip, remoteRootStr, relPath))
          count++;
      }

      {
        std::lock_guard<std::mutex> lock(m_mutex);
        state.statusMessage = "Deleted " + std::to_string(count) + " / " + std::to_string(total) + " items [Remote]. Refreshing scan...";
      }

      state.activeOperation = false;
      StartAsyncCompare(state, true);
    }).detach();
  }

  void RenderContextMenu(GUIState &state,
                         const std::shared_ptr<TreeNode> &child) {
    if (ImGui::BeginPopupContextItem()) {
      if (!state.selectedPaths.count(child->relativePath)) {
        state.selectedPaths.clear();
        state.selectedPaths.insert(child->relativePath);
      }

      std::string itemLabel =
          "Selected Items (" + std::to_string(state.selectedPaths.size()) + ")";
      ImGui::TextDisabled("%s", itemLabel.c_str());
      ImGui::Separator();

      if (ImGui::MenuItem("Copy Left ---> Right")) {
        ExecuteBatchCopyLeftToRight(state);
      }
      if (ImGui::MenuItem("Copy Right <--- Left")) {
        ExecuteBatchCopyRightToLeft(state);
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Delete from Local (Left)")) {
        ExecuteBatchDeleteLeft(state);
      }
      if (ImGui::MenuItem("Delete from Remote (Right)")) {
        ExecuteBatchDeleteRight(state);
      }
      ImGui::EndPopup();
    }
  }

  void RenderTreeNodesRecursive(GUIState &state,
                                const std::shared_ptr<TreeNode> &node) {
    for (const auto &pair : node->children) {
      const auto &child = pair.second;

      // Apply Filters
      if (!state.showMatches && child->aggregatedStatus == DiffStatus::Equal)
        continue;
      if (!state.showDiffs && child->aggregatedStatus == DiffStatus::Modified)
        continue;
      if (!state.showOrphans &&
          (child->aggregatedStatus == DiffStatus::LeftOnly ||
           child->aggregatedStatus == DiffStatus::RightOnly))
        continue;

      ImGui::TableNextRow();

      std::string nameStr = WStringFormatToUTF8(child->name);

      bool isSelected = state.selectedPaths.count(child->relativePath) > 0;

      // Column 0: Local Folder Tree (Local Tree - display item only if present on Left)
      ImGui::TableSetColumnIndex(0);

      bool clickedToggle = false;
      if (child->isDirectory && !child->children.empty()) {
        ImGui::PushID((void*)child.get());
        if (ImGui::SmallButton(child->isExpanded ? " - " : " + ")) {
          child->isExpanded = !child->isExpanded;
          clickedToggle = true;
        }
        ImGui::PopID();
        ImGui::SameLine();
      } else {
        ImGui::TextUnformatted("   ");
        ImGui::SameLine();
      }

      std::string tagStr = child->isDirectory ? "[DIR] " : "[FILE] ";
      std::string label = tagStr + nameStr;

      ImGuiTreeNodeFlags flags =
          ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_OpenOnArrow;
      if (isSelected)
        flags |= ImGuiTreeNodeFlags_Selected;
      if (!child->isDirectory || child->children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
      }

      // Always synchronize ImGui internal open state with child->isExpanded
      if (child->isDirectory && !child->children.empty()) {
        ImGui::SetNextItemOpen(child->isExpanded);
      }

      bool isOpen = false;
      if (child->diffData.hasLeft || child->aggregatedStatus == DiffStatus::Equal || child->aggregatedStatus == DiffStatus::Modified || child->aggregatedStatus == DiffStatus::LeftOnly) {
        isOpen = ImGui::TreeNodeEx((void *)child.get(), flags, "%s", label.c_str());
      } else {
        // RightOnly file: draw subtle placeholder hyphen on Left side
        ImGui::TreeNodeEx((void *)child.get(), flags | ImGuiTreeNodeFlags_NoTreePushOnOpen, "%s", "-");
      }

      if (child->isDirectory && !child->children.empty()) {
        child->isExpanded = isOpen;
      }

      // Handle Selection Logic (Windows Explorer Style: Single-click,
      // Ctrl+click, Shift+click)
      if (ImGui::IsItemClicked()) {
        std::wstring anchor = state.selectionAnchorPath.empty()
                                  ? state.lastSelectedPath
                                  : state.selectionAnchorPath;
        if (ImGui::GetIO().KeyShift && !anchor.empty()) {
          std::vector<std::shared_ptr<TreeNode>> visibleList;
          TreeNode::GetVisibleFlatList(state.rootNode, visibleList);

          int anchorIdx = -1, targetIdx = -1;
          for (int i = 0; i < (int)visibleList.size(); ++i) {
            if (visibleList[i]->relativePath == anchor)
              anchorIdx = i;
            if (visibleList[i]->relativePath == child->relativePath)
              targetIdx = i;
          }

          if (anchorIdx != -1 && targetIdx != -1) {
            state.selectedPaths.clear();
            int start = min(anchorIdx, targetIdx);
            int end = max(anchorIdx, targetIdx);
            for (int i = start; i <= end; ++i) {
              state.selectedPaths.insert(visibleList[i]->relativePath);
            }
          }
        } else if (ImGui::GetIO().KeyCtrl) {
          if (isSelected)
            state.selectedPaths.erase(child->relativePath);
          else
            state.selectedPaths.insert(child->relativePath);
          state.lastSelectedPath = child->relativePath;
          state.selectionAnchorPath = child->relativePath;
        } else {
          state.selectedPaths.clear();
          state.selectedPaths.insert(child->relativePath);
          state.lastSelectedPath = child->relativePath;
          state.selectionAnchorPath = child->relativePath;
        }
      }

      // Right click context menu on Local Column
      RenderContextMenu(state, child);

      // Column 1: Status Badge
      ImGui::TableSetColumnIndex(1);
      switch (child->aggregatedStatus) {
      case DiffStatus::Equal:
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Equal");
        break;
      case DiffStatus::Modified:
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Modified");
        break;
      case DiffStatus::LeftOnly:
        ImGui::TextColored(ImVec4(0.3f, 0.6f, 0.9f, 1.0f), "Left Only");
        break;
      case DiffStatus::RightOnly:
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f), "Right Only");
        break;
      }

      // Column 2: Remote Folder Tree (Display item only if present on Right)
      ImGui::TableSetColumnIndex(2);
      if (child->diffData.hasRight || (child->isDirectory && (child->aggregatedStatus == DiffStatus::RightOnly || child->aggregatedStatus == DiffStatus::Equal || child->aggregatedStatus == DiffStatus::Modified))) {
        ImGui::Selectable((label + "##Right_" + nameStr).c_str(), isSelected,
                          ImGuiSelectableFlags_SpanAllColumns);
        if (ImGui::IsItemClicked()) {
          std::wstring anchor = state.selectionAnchorPath.empty()
                                    ? state.lastSelectedPath
                                    : state.selectionAnchorPath;
          if (ImGui::GetIO().KeyShift && !anchor.empty()) {
            std::vector<std::shared_ptr<TreeNode>> visibleList;
            TreeNode::GetVisibleFlatList(state.rootNode, visibleList);

            int anchorIdx = -1, targetIdx = -1;
            for (int i = 0; i < (int)visibleList.size(); ++i) {
              if (visibleList[i]->relativePath == anchor)
                anchorIdx = i;
              if (visibleList[i]->relativePath == child->relativePath)
                targetIdx = i;
            }

            if (anchorIdx != -1 && targetIdx != -1) {
              state.selectedPaths.clear();
              int start = min(anchorIdx, targetIdx);
              int end = max(anchorIdx, targetIdx);
              for (int i = start; i <= end; ++i) {
                state.selectedPaths.insert(visibleList[i]->relativePath);
              }
            }
          } else if (ImGui::GetIO().KeyCtrl) {
            if (isSelected)
              state.selectedPaths.erase(child->relativePath);
            else
              state.selectedPaths.insert(child->relativePath);
            state.lastSelectedPath = child->relativePath;
            state.selectionAnchorPath = child->relativePath;
          } else {
            state.selectedPaths.clear();
            state.selectedPaths.insert(child->relativePath);
            state.lastSelectedPath = child->relativePath;
            state.selectionAnchorPath = child->relativePath;
          }
        }
        RenderContextMenu(state, child);
      } else {
        ImGui::TextDisabled("-");
      }

      // Column 3: File Sizes
      ImGui::TableSetColumnIndex(3);
      if (child->isDirectory) {
        ImGui::TextDisabled("Folder");
      } else {
        std::string leftSize = child->diffData.hasLeft
                                   ? std::to_string(child->diffData.leftSize)
                                   : "-";
        std::string rightSize = child->diffData.hasRight
                                    ? std::to_string(child->diffData.rightSize)
                                    : "-";
        ImGui::Text("%s / %s B", leftSize.c_str(), rightSize.c_str());
      }

      // Recurse into subfolders only if explicitly expanded by the user
      if (isOpen) {
        if (child->isDirectory && !child->children.empty() && child->isExpanded) {
          RenderTreeNodesRecursive(state, child);
        }
        ImGui::TreePop();
      }
    }
  }

  void RenderUI(GUIState &state) {
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("BestCompare Controls", NULL, flags);

    ImGui::TextColored(
        ImVec4(0.40f, 0.75f, 1.00f, 1.00f),
        "BestCompare v%.2f - High Performance Dual-PC Folder Compare", AppVersion);
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Columns(2, "ConfigColumns", false);
    ImGui::SetColumnWidth(0, viewport->WorkSize.x * 0.48f);

    ImGui::TextUnformatted("Local Folder (PC #1):");
    ImGui::PushItemWidth(-80.0f);
    ImGui::InputText("##LocalPath", state.localPath, sizeof(state.localPath));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Browse...##Local", ImVec2(70, 0))) {
      std::string folder = OpenFolderPickerWin32(m_hwnd);
      if (!folder.empty()) {
        strncpy_s(state.localPath, folder.c_str(), sizeof(state.localPath) - 1);
      }
    }

    ImGui::NextColumn();

    ImGui::TextUnformatted("Remote IP & Folder (PC #2):");
    ImGui::PushItemWidth(110.0f);
    ImGui::InputText("##RemoteIP", state.remoteIp, sizeof(state.remoteIp));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::PushItemWidth(-80.0f);
    ImGui::InputText("##RemotePath", state.remotePath,
                     sizeof(state.remotePath));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Browse...##Remote", ImVec2(70, 0))) {
      std::string folder = OpenFolderPickerWin32(m_hwnd);
      if (!folder.empty()) {
        strncpy_s(state.remotePath, folder.c_str(),
                  sizeof(state.remotePath) - 1);
      }
    }

    ImGui::Columns(2, "FilterColumns", false);
    ImGui::TextUnformatted("Include File Types (default * or e.g. *.cpp, *.hpp):");
    ImGui::PushItemWidth(-1.0f);
    ImGui::InputText("##IncludePatterns", state.includePatterns, sizeof(state.includePatterns));
    ImGui::PopItemWidth();

    ImGui::NextColumn();

    ImGui::TextUnformatted("Ignore Patterns (e.g. *.db, *.tmp, .git):");
    ImGui::PushItemWidth(-1.0f);
    ImGui::InputText("##IgnorePatterns", state.ignorePatterns, sizeof(state.ignorePatterns));
    ImGui::PopItemWidth();

    ImGui::Columns(1);
    ImGui::Spacing();

    // Top Toolbar: Compare & Action Buttons (Applies to Selected Items)
    if (state.isScanning) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.15f, 0.15f, 1.0f));
      if (ImGui::Button(" STOP ", ImVec2(120, 30))) {
        state.cancelScan = true;
        state.statusMessage = "[CANCELING...] Stopping scan on local and remote sides...";
      }
      ImGui::PopStyleColor(3);
    } else {
      if (ImGui::Button(" Run Compare ", ImVec2(120, 30))) {
        StartAsyncCompare(state, false);
      }
      ImGui::SameLine();
      if (ImGui::Button(" Fast Refresh ", ImVec2(120, 30))) {
        StartAsyncCompare(state, true);
      }
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Action Toolbar applying to selection
    bool hasSelection = !state.selectedPaths.empty();
    ImGui::BeginDisabled(!hasSelection || state.isScanning);
    if (ImGui::Button(" Copy Left ---> Right ", ImVec2(160, 30))) {
      ExecuteBatchCopyLeftToRight(state);
    }
    ImGui::SameLine();
    if (ImGui::Button(" Copy Right <--- Left ", ImVec2(160, 30))) {
      ExecuteBatchCopyRightToLeft(state);
    }
    ImGui::SameLine();
    if (ImGui::Button(" Delete Left ", ImVec2(100, 30))) {
      ExecuteBatchDeleteLeft(state);
    }
    ImGui::SameLine();
    if (ImGui::Button(" Delete Right ", ImVec2(100, 30))) {
      ExecuteBatchDeleteRight(state);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    ImGui::Checkbox("Equal Matches", &state.showMatches);
    ImGui::SameLine();
    ImGui::Checkbox("Differences", &state.showDiffs);
    ImGui::SameLine();
    ImGui::Checkbox("Orphans", &state.showOrphans);

    ImGui::Spacing();
    ImGui::Separator();

    // Collapsible Tree Results Table
    static ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingFixedFit;
    float tableHeight = viewport->WorkSize.y - 260.0f;

    if (ImGui::BeginTable("DiffTreeTable", 4, tableFlags,
                          ImVec2(0.0f, tableHeight))) {
      ImGui::TableSetupColumn("Local Folder Tree (PC #1)",
                              ImGuiTableColumnFlags_WidthStretch, 0.40f);
      ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed,
                              100.0f);
      ImGui::TableSetupColumn("Remote Folder Tree (PC #2)",
                              ImGuiTableColumnFlags_WidthStretch, 0.40f);
      ImGui::TableSetupColumn("Size (Local / Remote)",
                              ImGuiTableColumnFlags_WidthFixed, 150.0f);
      ImGui::TableHeadersRow();

      std::lock_guard<std::mutex> lock(m_mutex);
      if (state.rootNode) {
        RenderTreeNodesRecursive(state, state.rootNode);
      }

      // Keyboard Navigation Handling (Arrow Up / Arrow Down / Shift + Arrows)
      if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
          state.rootNode) {
        std::vector<std::shared_ptr<TreeNode>> visibleList;
        TreeNode::GetVisibleFlatList(state.rootNode, visibleList);

        if (!visibleList.empty()) {
          int currentIdx = -1;
          if (!state.lastSelectedPath.empty()) {
            for (int i = 0; i < (int)visibleList.size(); ++i) {
              if (visibleList[i]->relativePath == state.lastSelectedPath) {
                currentIdx = i;
                break;
              }
            }
          }

          int newIdx = -1;
          if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            if (currentIdx == -1)
              newIdx = 0;
            else if (currentIdx < (int)visibleList.size() - 1)
              newIdx = currentIdx + 1;
          } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            if (currentIdx == -1)
              newIdx = (int)visibleList.size() - 1;
            else if (currentIdx > 0)
              newIdx = currentIdx - 1;
          }

          if (newIdx != -1) {
            const auto &targetItem = visibleList[newIdx];
            if (ImGui::GetIO().KeyShift && currentIdx != -1) {
              if (state.selectionAnchorPath.empty()) {
                state.selectionAnchorPath =
                    visibleList[currentIdx]->relativePath;
              }

              int anchorIdx = -1;
              for (int i = 0; i < (int)visibleList.size(); ++i) {
                if (visibleList[i]->relativePath == state.selectionAnchorPath) {
                  anchorIdx = i;
                  break;
                }
              }

              if (anchorIdx != -1) {
                state.selectedPaths.clear();
                int start = min(anchorIdx, newIdx);
                int end = max(anchorIdx, newIdx);
                for (int i = start; i <= end; ++i) {
                  state.selectedPaths.insert(visibleList[i]->relativePath);
                }
              }
            } else if (!ImGui::GetIO().KeyCtrl) {
              state.selectedPaths.clear();
              state.selectedPaths.insert(targetItem->relativePath);
              state.selectionAnchorPath = targetItem->relativePath;
            } else {
              state.selectedPaths.insert(targetItem->relativePath);
              state.selectionAnchorPath = targetItem->relativePath;
            }
            state.lastSelectedPath = targetItem->relativePath;
          }
        }
      }

      ImGui::EndTable();
    }

    ImGui::Separator();
    {
      std::lock_guard<std::mutex> lock(state.stateMutex);
      if (state.isScanning) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s",
                           state.statusMessage.c_str());
      } else {
        ImGui::Text("Status: %s | Selected: %zu items | Matches: %zu | Modified: "
                    "%zu | Left Orphans: %zu | Right Orphans: %zu",
                    state.statusMessage.c_str(), state.selectedPaths.size(),
                    state.equalCount, state.modifiedCount, state.leftOnlyCount,
                    state.rightOnlyCount);
      }
    }

    ImGui::End();
  }

  bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_pSwapChain,
        &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
      res = D3D11CreateDeviceAndSwapChain(
          NULL, D3D_DRIVER_TYPE_WARP, NULL, createDeviceFlags,
          featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_pSwapChain,
          &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext);
    if (res != S_OK)
      return false;

    CreateRenderTarget();
    return true;
  }

  void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (m_pSwapChain) {
      m_pSwapChain->Release();
      m_pSwapChain = NULL;
    }
    if (m_pd3dDeviceContext) {
      m_pd3dDeviceContext->Release();
      m_pd3dDeviceContext = NULL;
    }
    if (m_pd3dDevice) {
      m_pd3dDevice->Release();
      m_pd3dDevice = NULL;
    }
  }

  void CreateRenderTarget() {
    ID3D11Texture2D *pBackBuffer;
    m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    m_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL,
                                         &m_mainRenderTargetView);
    pBackBuffer->Release();
  }

  void CleanupRenderTarget() {
    if (m_mainRenderTargetView) {
      m_mainRenderTargetView->Release();
      m_mainRenderTargetView = NULL;
    }
  }

  static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam,
                                LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
      return true;

    switch (msg) {
    case WM_SIZE:
      if (wParam == SIZE_MINIMIZED)
        return 0;
      Instance().m_ResizeWidth = (UINT)LOWORD(lParam);
      Instance().m_ResizeHeight = (UINT)HIWORD(lParam);
      return 0;
    case WM_SYSCOMMAND:
      if ((wParam & 0xfff0) == SC_KEYMENU)
        return 0;
      break;
    case WM_DESTROY:
      ::PostQuitMessage(0);
      return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
  }

  HWND m_hwnd = NULL;
  ID3D11Device *m_pd3dDevice = NULL;
  ID3D11DeviceContext *m_pd3dDeviceContext = NULL;
  IDXGISwapChain *m_pSwapChain = NULL;
  UINT m_ResizeWidth = 0;
  UINT m_ResizeHeight = 0;
  ID3D11RenderTargetView *m_mainRenderTargetView = NULL;
  std::mutex m_mutex;
};

} // namespace BestCompare

#endif // BESTCOMPARE_APP_WINDOW_HPP
