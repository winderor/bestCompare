#ifndef BESTCOMPARE_NETWORK_SERVER_HPP
#define BESTCOMPARE_NETWORK_SERVER_HPP

#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <atomic>
#include <iostream>
#include <vector>
#include <fstream>
#include "../core/scanner.hpp"
#include "../core/file_ops.hpp"
#include "protocol.hpp"

#pragma comment(lib, "Ws2_32.lib")

namespace BestCompare {

struct IGUIStateReporter {
    virtual void ReportStatus(const std::string& msg, bool isScanning) = 0;
    virtual ~IGUIStateReporter() = default;
};

class NetworkServer {
public:
    NetworkServer(unsigned short port = 9090) : m_port(port), m_running(false), m_reporter(nullptr), m_cancelCurrentScan(false) {}

    void SetStatusReporter(IGUIStateReporter* reporter) {
        m_reporter = reporter;
    }

    void CancelCurrentScan() {
        m_cancelCurrentScan = true;
    }

    ~NetworkServer() {
        Stop();
    }

    bool Start() {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "[BestCompare Server] WSAStartup failed.\n";
            return false;
        }

        m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listenSocket == INVALID_SOCKET) {
            WSACleanup();
            return false;
        }

        sockaddr_in service{};
        service.sin_family = AF_INET;
        service.sin_addr.s_addr = INADDR_ANY;
        service.sin_port = htons(m_port);

        if (bind(m_listenSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
            closesocket(m_listenSocket);
            WSACleanup();
            return false;
        }

        if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(m_listenSocket);
            WSACleanup();
            return false;
        }

        m_running = true;
        m_serverThread = std::jthread([this]() { ListenLoop(); });

        std::cout << "[BestCompare Server] Listening on port " << m_port << " for LAN connections...\n";
        return true;
    }

    void Stop() {
        if (m_running) {
            m_running = false;
            if (m_listenSocket != INVALID_SOCKET) {
                closesocket(m_listenSocket);
                m_listenSocket = INVALID_SOCKET;
            }
            WSACleanup();
        }
    }

private:
    void ListenLoop() {
        while (m_running) {
            SOCKET clientSocket = accept(m_listenSocket, NULL, NULL);
            if (clientSocket != INVALID_SOCKET) {
                std::jthread([this, clientSocket]() { HandleClient(clientSocket); }).detach();
            }
        }
    }

    void HandleClient(SOCKET clientSocket) {
        PacketHeader header{};
        int bytesRead = recv(clientSocket, reinterpret_cast<char*>(&header), sizeof(PacketHeader), 0);

        if (bytesRead == sizeof(PacketHeader) && header.magic == 0x42435032) {
            if (header.type == MessageType::Ping) {
                PacketHeader response{};
                response.type = MessageType::ReadyResponse;
                send(clientSocket, reinterpret_cast<char*>(&response), sizeof(PacketHeader), 0);
            } else if (header.type == MessageType::CancelScanRequest) {
                m_cancelCurrentScan = true;
                if (m_reporter) {
                    m_reporter->ReportStatus("[REMOTE CANCELLED] Incoming cancellation request received from client.", false);
                }
            } else if (header.type == MessageType::StartScanRequest || header.type == MessageType::StartFastScanRequest) {
                bool isFastScan = (header.type == MessageType::StartFastScanRequest);
                m_cancelCurrentScan = false;
                // Read requested path
                std::vector<wchar_t> pathBuffer(header.payloadLength / sizeof(wchar_t));
                recv(clientSocket, reinterpret_cast<char*>(pathBuffer.data()), header.payloadLength, 0);
                std::wstring targetPath(pathBuffer.data(), pathBuffer.size());

                std::string narrowTarget;
                if (!targetPath.empty()) {
                    std::wstring bidiFixed = targetPath;
                    size_t bi = 0, blen = bidiFixed.length();
                    while (bi < blen) {
                        if (bidiFixed[bi] >= 0x0590 && bidiFixed[bi] <= 0x05FF) {
                            size_t bstart = bi;
                            while (bi < blen && ((bidiFixed[bi] >= 0x0590 && bidiFixed[bi] <= 0x05FF) || bidiFixed[bi] == L' ' || (bidiFixed[bi] >= L'0' && bidiFixed[bi] <= L'9'))) {
                                if (bidiFixed[bi] == L' ' && (bi + 1 >= blen || !(bidiFixed[bi + 1] >= 0x0590 && bidiFixed[bi + 1] <= 0x05FF))) break;
                                bi++;
                            }
                            std::reverse(bidiFixed.begin() + bstart, bidiFixed.begin() + bi);
                        } else { bi++; }
                    }
                    int sz = WideCharToMultiByte(CP_UTF8, 0, &bidiFixed[0], (int)bidiFixed.size(), NULL, 0, NULL, NULL);
                    narrowTarget.resize(sz);
                    WideCharToMultiByte(CP_UTF8, 0, &bidiFixed[0], (int)bidiFixed.size(), &narrowTarget[0], sz, NULL, NULL);
                }

                if (m_reporter) {
                    m_reporter->ReportStatus(std::string(isFastScan ? "[REMOTE FAST SCAN] " : "[REMOTE FULL SCAN] ") + "Processing directory scan for: " + narrowTarget, true);
                }

                // Perform local scan using local CPU & disk with live progress callbacks and cancellation support
                DirectoryScanResult scanResult = DirectoryScanner::ScanDirectory(targetPath, [this, clientSocket](size_t filesScanned, size_t dirsScanned, uint64_t bytesScanned, const std::wstring& currentItem) {
                    if (m_reporter) {
                        std::string narrowItem;
                        if (!currentItem.empty()) {
                            std::wstring bidiFixed = currentItem;
                            size_t bi = 0, blen = bidiFixed.length();
                            while (bi < blen) {
                                if (bidiFixed[bi] >= 0x0590 && bidiFixed[bi] <= 0x05FF) {
                                    size_t bstart = bi;
                                    while (bi < blen && ((bidiFixed[bi] >= 0x0590 && bidiFixed[bi] <= 0x05FF) || bidiFixed[bi] == L' ' || (bidiFixed[bi] >= L'0' && bidiFixed[bi] <= L'9'))) {
                                        if (bidiFixed[bi] == L' ' && (bi + 1 >= blen || !(bidiFixed[bi + 1] >= 0x0590 && bidiFixed[bi + 1] <= 0x05FF))) break;
                                        bi++;
                                    }
                                    std::reverse(bidiFixed.begin() + bstart, bidiFixed.begin() + bi);
                                } else { bi++; }
                            }
                            int sz = WideCharToMultiByte(CP_UTF8, 0, &bidiFixed[0], (int)bidiFixed.size(), NULL, 0, NULL, NULL);
                            narrowItem.resize(sz);
                            WideCharToMultiByte(CP_UTF8, 0, &bidiFixed[0], (int)bidiFixed.size(), &narrowItem[0], sz, NULL, NULL);
                        }
                        m_reporter->ReportStatus("[REMOTE SCANNING] Files: " + std::to_string(filesScanned) + ", Dirs: " + std::to_string(dirsScanned) + " | Current: " + narrowItem, true);
                    }

                    // Send live progress update packet over network back to PC #1 client
                    PacketHeader progHeader{};
                    progHeader.type = MessageType::ScanProgressUpdate;
                    ProgressSerialized progData{};
                    progData.filesScanned = static_cast<uint32_t>(filesScanned);
                    progData.dirsScanned = static_cast<uint32_t>(dirsScanned);
                    progData.bytesScanned = bytesScanned;
                    progData.itemLength = static_cast<uint16_t>(currentItem.length() * sizeof(wchar_t));
                    progHeader.payloadLength = sizeof(ProgressSerialized) + progData.itemLength;

                    send(clientSocket, reinterpret_cast<char*>(&progHeader), sizeof(PacketHeader), 0);
                    send(clientSocket, reinterpret_cast<char*>(&progData), sizeof(ProgressSerialized), 0);
                    if (progData.itemLength > 0) {
                        send(clientSocket, reinterpret_cast<const char*>(currentItem.data()), progData.itemLength, 0);
                    }
                }, &m_cancelCurrentScan, isFastScan);

                if (m_cancelCurrentScan) {
                    if (m_reporter) {
                        m_reporter->ReportStatus("[REMOTE SCAN CANCELLED] Remote scan cancelled by request.", false);
                    }
                } else {
                    if (m_reporter) {
                        m_reporter->ReportStatus("[REMOTE INCOMING REQUEST] Streaming " + std::to_string(scanResult.nodes.size()) + " scanned nodes back to client...", true);
                    }

                    // Stream back scan result
                    PacketHeader headerResp{};
                    headerResp.type = MessageType::NodeDataChunk;
                    headerResp.payloadLength = static_cast<uint32_t>(scanResult.nodes.size());
                    send(clientSocket, reinterpret_cast<char*>(&headerResp), sizeof(PacketHeader), 0);

                    for (const auto& node : scanResult.nodes) {
                        if (m_cancelCurrentScan) break;

                        NodeSerialized serialized{};
                        serialized.fileSize = node.fileSize;
                        serialized.mtime = node.mtime;
                        serialized.partialHash = node.partialHash;
                        serialized.merkleHash = node.merkleHash;
                        serialized.isDirectory = node.isDirectory ? 1 : 0;
                        serialized.pathLength = static_cast<uint16_t>(node.relativePath.length() * sizeof(wchar_t));

                        send(clientSocket, reinterpret_cast<char*>(&serialized), sizeof(NodeSerialized), 0);
                        send(clientSocket, reinterpret_cast<const char*>(node.relativePath.data()), serialized.pathLength, 0);
                    }

                    if (m_reporter) {
                        m_reporter->ReportStatus("[REMOTE SERVER IDLE] Remote scan served successfully. Ready for next request.", false);
                    }
                }
            } else if (header.type == MessageType::CopyToRemoteRequest) {
                FileOpHeader opHeader{};
                recv(clientSocket, reinterpret_cast<char*>(&opHeader), sizeof(FileOpHeader), 0);

                std::vector<wchar_t> pathBuf(opHeader.relPathLength / sizeof(wchar_t));
                recv(clientSocket, reinterpret_cast<char*>(pathBuf.data()), opHeader.relPathLength, 0);
                std::wstring dstPathStr(pathBuf.data(), pathBuf.size());
                fs::path dstPath(dstPathStr);

                bool ok = false;
                if (opHeader.isDirectory) {
                    std::error_code ec;
                    fs::create_directories(dstPath, ec);
                    ok = !ec;
                } else {
                    if (dstPath.has_parent_path()) {
                        std::error_code ec;
                        fs::create_directories(dstPath.parent_path(), ec);
                    }
                    std::ofstream outFile(dstPath, std::ios::binary);
                    if (outFile.is_open()) {
                        uint32_t remaining = opHeader.contentPayloadLength;
                        char buf[65536];
                        while (remaining > 0) {
                            int toRead = static_cast<int>((std::min)(static_cast<uint32_t>(sizeof(buf)), remaining));
                            int r = recv(clientSocket, buf, toRead, 0);
                            if (r <= 0) break;
                            outFile.write(buf, r);
                            remaining -= r;
                        }
                        ok = (remaining == 0);
                    }
                }

                PacketHeader resp{};
                resp.type = ok ? MessageType::FileOpResponse : MessageType::Error;
                send(clientSocket, reinterpret_cast<char*>(&resp), sizeof(PacketHeader), 0);

            } else if (header.type == MessageType::CopyFromRemoteRequest) {
                FileOpHeader opHeader{};
                recv(clientSocket, reinterpret_cast<char*>(&opHeader), sizeof(FileOpHeader), 0);

                std::vector<wchar_t> pathBuf(opHeader.relPathLength / sizeof(wchar_t));
                recv(clientSocket, reinterpret_cast<char*>(pathBuf.data()), opHeader.relPathLength, 0);
                std::wstring srcPathStr(pathBuf.data(), pathBuf.size());
                fs::path srcPath(srcPathStr);

                bool isDir = fs::is_directory(srcPath);
                uint64_t fSize = isDir ? 0 : (fs::exists(srcPath) ? fs::file_size(srcPath) : 0);

                PacketHeader respHeader{};
                respHeader.type = MessageType::FileOpResponse;
                respHeader.payloadLength = static_cast<uint32_t>(fSize);
                send(clientSocket, reinterpret_cast<char*>(&respHeader), sizeof(PacketHeader), 0);

                if (!isDir && fSize > 0) {
                    std::ifstream inFile(srcPath, std::ios::binary);
                    if (inFile.is_open()) {
                        char buf[65536];
                        while (inFile.read(buf, sizeof(buf)) || inFile.gcount() > 0) {
                            send(clientSocket, buf, static_cast<int>(inFile.gcount()), 0);
                        }
                    }
                }

            } else if (header.type == MessageType::DeleteRemoteRequest) {
                FileOpHeader opHeader{};
                recv(clientSocket, reinterpret_cast<char*>(&opHeader), sizeof(FileOpHeader), 0);

                std::vector<wchar_t> pathBuf(opHeader.relPathLength / sizeof(wchar_t));
                recv(clientSocket, reinterpret_cast<char*>(pathBuf.data()), opHeader.relPathLength, 0);
                std::wstring targetPathStr(pathBuf.data(), pathBuf.size());

                bool ok = FileOps::DeletePath(fs::path(targetPathStr));

                PacketHeader resp{};
                resp.type = ok ? MessageType::FileOpResponse : MessageType::Error;
                send(clientSocket, reinterpret_cast<char*>(&resp), sizeof(PacketHeader), 0);
            }
        }

        closesocket(clientSocket);
    }

    unsigned short m_port;
    SOCKET m_listenSocket = INVALID_SOCKET;
    std::atomic<bool> m_running;
    IGUIStateReporter* m_reporter = nullptr;
    std::atomic<bool> m_cancelCurrentScan{false};
    std::jthread m_serverThread;
};

} // namespace BestCompare

#endif // BESTCOMPARE_NETWORK_SERVER_HPP
