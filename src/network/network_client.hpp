#ifndef BESTCOMPARE_NETWORK_CLIENT_HPP
#define BESTCOMPARE_NETWORK_CLIENT_HPP

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <functional>
#include <atomic>
#include "../core/scanner.hpp"
#include "protocol.hpp"

namespace BestCompare {

class NetworkClient {
public:
    using ProgressCallback = std::function<void(size_t filesReceived, size_t dirsReceived, uint64_t bytesReceived, const std::wstring& currentItem)>;

    static bool CheckRemoteReady(const std::string& remoteIp, std::atomic<bool>* cancelFlag = nullptr, unsigned short port = 9090) {
        SOCKET clientSocket = ConnectToRemote(remoteIp, port, cancelFlag);
        if (clientSocket == INVALID_SOCKET) return false;

        PacketHeader pingHeader{};
        pingHeader.type = MessageType::Ping;
        send(clientSocket, reinterpret_cast<char*>(&pingHeader), sizeof(PacketHeader), 0);

        PacketHeader response{};
        int bytesRead = recv(clientSocket, reinterpret_cast<char*>(&response), sizeof(PacketHeader), 0);
        closesocket(clientSocket);

        return (bytesRead == sizeof(PacketHeader) && response.type == MessageType::ReadyResponse);
    }

    static void SendCancelRequest(const std::string& remoteIp, unsigned short port = 9090) {
        SOCKET clientSocket = ConnectToRemote(remoteIp, port, nullptr, 500);
        if (clientSocket == INVALID_SOCKET) return;

        PacketHeader cancelHeader{};
        cancelHeader.type = MessageType::CancelScanRequest;
        send(clientSocket, reinterpret_cast<char*>(&cancelHeader), sizeof(PacketHeader), 0);
        closesocket(clientSocket);
    }

    static bool RequestRemoteCopyFile(const std::string& remoteIp, const std::wstring& remoteRoot, const std::wstring& relPath, const fs::path& localSrcPath, bool isCopyLeftToRight, unsigned short port = 9090) {
        SOCKET clientSocket = ConnectToRemote(remoteIp, port, nullptr, 3000);
        if (clientSocket == INVALID_SOCKET) return false;

        std::wstring fullRemoteTarget = (fs::path(remoteRoot) / relPath).wstring();

        PacketHeader header{};
        header.type = isCopyLeftToRight ? MessageType::CopyToRemoteRequest : MessageType::CopyFromRemoteRequest;
        
        bool isDir = fs::is_directory(localSrcPath);
        uint64_t fSize = isDir ? 0 : (fs::exists(localSrcPath) ? fs::file_size(localSrcPath) : 0);

        FileOpHeader opHeader{};
        opHeader.isDirectory = isDir ? 1 : 0;
        opHeader.fileSize = fSize;
        opHeader.relPathLength = static_cast<uint16_t>(fullRemoteTarget.length() * sizeof(wchar_t));
        opHeader.contentPayloadLength = isCopyLeftToRight && !isDir ? static_cast<uint32_t>(fSize) : 0;

        header.payloadLength = sizeof(FileOpHeader) + opHeader.relPathLength + opHeader.contentPayloadLength;

        send(clientSocket, reinterpret_cast<char*>(&header), sizeof(PacketHeader), 0);
        send(clientSocket, reinterpret_cast<char*>(&opHeader), sizeof(FileOpHeader), 0);
        send(clientSocket, reinterpret_cast<const char*>(fullRemoteTarget.data()), opHeader.relPathLength, 0);

        if (isCopyLeftToRight && !isDir && fSize > 0) {
            std::ifstream inFile(localSrcPath, std::ios::binary);
            if (inFile.is_open()) {
                char buf[65536];
                while (inFile.read(buf, sizeof(buf)) || inFile.gcount() > 0) {
                    send(clientSocket, buf, static_cast<int>(inFile.gcount()), 0);
                }
            }
        }

        PacketHeader resp{};
        int bytes = recv(clientSocket, reinterpret_cast<char*>(&resp), sizeof(PacketHeader), 0);

        if (bytes == sizeof(PacketHeader) && resp.type == MessageType::FileOpResponse) {
            if (!isCopyLeftToRight) {
                // Incoming payload from remote PC to write to localDstPath
                if (localSrcPath.has_parent_path()) {
                    std::error_code ec;
                    fs::create_directories(localSrcPath.parent_path(), ec);
                }
                uint32_t remaining = resp.payloadLength;
                std::ofstream outFile(localSrcPath, std::ios::binary);
                if (outFile.is_open()) {
                    char buf[65536];
                    while (remaining > 0) {
                        int toRead = static_cast<int>((std::min)(static_cast<uint32_t>(sizeof(buf)), remaining));
                        int r = recv(clientSocket, buf, toRead, 0);
                        if (r <= 0) break;
                        outFile.write(buf, r);
                        remaining -= r;
                    }
                }
            }
            closesocket(clientSocket);
            return true;
        }

        closesocket(clientSocket);
        return false;
    }

    static bool RequestRemoteDelete(const std::string& remoteIp, const std::wstring& remoteRoot, const std::wstring& relPath, unsigned short port = 9090) {
        SOCKET clientSocket = ConnectToRemote(remoteIp, port, nullptr, 3000);
        if (clientSocket == INVALID_SOCKET) return false;

        std::wstring fullRemoteTarget = (fs::path(remoteRoot) / relPath).wstring();

        PacketHeader header{};
        header.type = MessageType::DeleteRemoteRequest;

        FileOpHeader opHeader{};
        opHeader.isDirectory = 0;
        opHeader.fileSize = 0;
        opHeader.relPathLength = static_cast<uint16_t>(fullRemoteTarget.length() * sizeof(wchar_t));
        opHeader.contentPayloadLength = 0;

        header.payloadLength = sizeof(FileOpHeader) + opHeader.relPathLength;

        send(clientSocket, reinterpret_cast<char*>(&header), sizeof(PacketHeader), 0);
        send(clientSocket, reinterpret_cast<char*>(&opHeader), sizeof(FileOpHeader), 0);
        send(clientSocket, reinterpret_cast<const char*>(fullRemoteTarget.data()), opHeader.relPathLength, 0);

        PacketHeader resp{};
        int bytes = recv(clientSocket, reinterpret_cast<char*>(&resp), sizeof(PacketHeader), 0);
        closesocket(clientSocket);

        return (bytes == sizeof(PacketHeader) && resp.type == MessageType::FileOpResponse);
    }

    static DirectoryScanResult RequestRemoteScan(const std::string& remoteIp, const std::wstring& remotePath, ProgressCallback progressCb = nullptr, std::atomic<bool>* cancelFlag = nullptr, bool fastScan = false, unsigned short port = 9090) {
        DirectoryScanResult result;
        result.rootPath = remotePath;

        SOCKET clientSocket = ConnectToRemote(remoteIp, port, cancelFlag);
        if (clientSocket == INVALID_SOCKET) return result;

        // Send start scan request (full or fast)
        PacketHeader scanHeader{};
        scanHeader.type = fastScan ? MessageType::StartFastScanRequest : MessageType::StartScanRequest;
        scanHeader.payloadLength = static_cast<uint32_t>(remotePath.length() * sizeof(wchar_t));

        send(clientSocket, reinterpret_cast<char*>(&scanHeader), sizeof(PacketHeader), 0);
        send(clientSocket, reinterpret_cast<const char*>(remotePath.data()), scanHeader.payloadLength, 0);

        // Read incoming packet headers (ScanProgressUpdate packets or NodeDataChunk header)
        while (true) {
            if (cancelFlag && cancelFlag->load()) {
                SendCancelRequest(remoteIp, port);
                break;
            }

            PacketHeader respHeader{};
            int bytes = recv(clientSocket, reinterpret_cast<char*>(&respHeader), sizeof(PacketHeader), 0);
            if (bytes != sizeof(PacketHeader) || respHeader.magic != 0x42435032) {
                break;
            }

            if (respHeader.type == MessageType::ScanProgressUpdate) {
                ProgressSerialized progData{};
                recv(clientSocket, reinterpret_cast<char*>(&progData), sizeof(ProgressSerialized), 0);

                std::wstring itemStr = L"";
                if (progData.itemLength > 0) {
                    std::vector<wchar_t> itemBuf(progData.itemLength / sizeof(wchar_t));
                    recv(clientSocket, reinterpret_cast<char*>(itemBuf.data()), progData.itemLength, 0);
                    itemStr = std::wstring(itemBuf.data(), itemBuf.size());
                }

                if (progressCb) {
                    progressCb(progData.filesScanned, progData.dirsScanned, progData.bytesScanned, itemStr);
                }
            } else if (respHeader.type == MessageType::NodeDataChunk) {
                uint32_t nodeCount = respHeader.payloadLength;

                for (uint32_t i = 0; i < nodeCount; ++i) {
                    if (cancelFlag && cancelFlag->load()) {
                        SendCancelRequest(remoteIp, port);
                        break;
                    }

                    NodeSerialized serialized{};
                    recv(clientSocket, reinterpret_cast<char*>(&serialized), sizeof(NodeSerialized), 0);

                    std::vector<wchar_t> pathBuf(serialized.pathLength / sizeof(wchar_t));
                    recv(clientSocket, reinterpret_cast<char*>(pathBuf.data()), serialized.pathLength, 0);

                    FileNode node;
                    node.fileSize = serialized.fileSize;
                    node.mtime = serialized.mtime;
                    node.partialHash = serialized.partialHash;
                    node.merkleHash = serialized.merkleHash;
                    node.isDirectory = (serialized.isDirectory != 0);
                    node.relativePath = std::wstring(pathBuf.data(), pathBuf.size());

                    if (node.isDirectory) result.totalDirectories++;
                    else {
                        result.totalFiles++;
                        result.totalBytes += node.fileSize;
                    }

                    result.nodes.push_back(std::move(node));
                }
                break;
            }
        }

        closesocket(clientSocket);
        return result;
    }

private:
    static SOCKET ConnectToRemote(const std::string& remoteIp, unsigned short port, std::atomic<bool>* cancelFlag = nullptr, DWORD timeoutMs = 2000) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return INVALID_SOCKET;

        // Set non-blocking mode
        u_long mode = 1;
        ioctlsocket(s, FIONBIO, &mode);

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        inet_pton(AF_INET, remoteIp.c_str(), &serverAddr.sin_addr);

        int res = connect(s, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
        if (res == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                closesocket(s);
                return INVALID_SOCKET;
            }

            // Poll using select in small chunks (50ms) to allow immediate user cancellation
            fd_set writeSet, errSet;
            TIMEVAL tv;
            DWORD elapsedMs = 0;
            bool connected = false;

            while (elapsedMs < timeoutMs) {
                if (cancelFlag && cancelFlag->load()) {
                    closesocket(s);
                    return INVALID_SOCKET;
                }

                FD_ZERO(&writeSet);
                FD_ZERO(&errSet);
                FD_SET(s, &writeSet);
                FD_SET(s, &errSet);

                tv.tv_sec = 0;
                tv.tv_usec = 50000; // 50ms polling step

                int selectRes = select(0, NULL, &writeSet, &errSet, &tv);
                if (selectRes > 0) {
                    if (FD_ISSET(s, &writeSet) && !FD_ISSET(s, &errSet)) {
                        int sockErr = 0;
                        int optLen = sizeof(sockErr);
                        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&sockErr), &optLen);
                        if (sockErr == 0) {
                            connected = true;
                        }
                    }
                    break;
                } else if (selectRes < 0) {
                    break;
                }

                elapsedMs += 50;
            }

            if (!connected) {
                closesocket(s);
                return INVALID_SOCKET;
            }
        }

        // Restore blocking mode for normal TCP operations
        mode = 0;
        ioctlsocket(s, FIONBIO, &mode);
        return s;
    }
};

} // namespace BestCompare

#endif // BESTCOMPARE_NETWORK_CLIENT_HPP
