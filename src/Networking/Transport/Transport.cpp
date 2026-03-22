#include "Transport.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

namespace WT {

// ============================================================
// WinsockTransport Implementation
// ============================================================

bool WinsockTransport::Init() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    // Non-blocking mode
    u_long nonBlocking = 1;
    ioctlsocket(sock, FIONBIO, &nonBlocking);

    // Increase recv buffer
    int bufSize = 256 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));

    m_socket = static_cast<uintptr_t>(sock);
    return true;
}

void WinsockTransport::Shutdown() {
    DisconnectAll();
    if (m_socket != ~0ULL) {
        closesocket(static_cast<SOCKET>(m_socket));
        m_socket = ~0ULL;
    }
    WSACleanup();
    m_listening = false;
}

bool WinsockTransport::Listen(uint16_t port) {
    if (m_socket == ~0ULL) return false;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(static_cast<SOCKET>(m_socket), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        return false;
    }

    m_listening = true;
    m_listenPort = port;
    return true;
}

bool WinsockTransport::Connect(const std::string& address, uint16_t port) {
    if (m_socket == ~0ULL) return false;

    // Bind to any available port if not already listening
    if (!m_listening) {
        sockaddr_in localAddr = {};
        localAddr.sin_family = AF_INET;
        localAddr.sin_port = 0; // OS assigns port
        localAddr.sin_addr.s_addr = INADDR_ANY;
        bind(static_cast<SOCKET>(m_socket), reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr));
    }

    ConnectionId cid = AllocateConnectionId();
    Peer peer;
    peer.id = cid;
    peer.state = ConnectionState::Connecting;
    peer.address = address;
    peer.port = port;
    m_peers[cid] = std::move(peer);

    // Send connection request packet (type byte 0x01)
    uint8_t connectPacket[5] = { 0x01 };
    memcpy(connectPacket + 1, &cid, 4);

    sockaddr_in destAddr = {};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(port);
    inet_pton(AF_INET, address.c_str(), &destAddr.sin_addr);

    sendto(static_cast<SOCKET>(m_socket), reinterpret_cast<const char*>(connectPacket),
           sizeof(connectPacket), 0,
           reinterpret_cast<sockaddr*>(&destAddr), sizeof(destAddr));

    return true;
}

void WinsockTransport::Disconnect(ConnectionId id) {
    auto it = m_peers.find(id);
    if (it == m_peers.end()) return;

    // Send disconnect packet (type byte 0x02)
    uint8_t disc[5] = { 0x02 };
    memcpy(disc + 1, &id, 4);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(it->second.port);
    inet_pton(AF_INET, it->second.address.c_str(), &addr.sin_addr);
    sendto(static_cast<SOCKET>(m_socket), reinterpret_cast<const char*>(disc),
           sizeof(disc), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    if (m_callbacks.onDisconnected) m_callbacks.onDisconnected(id);
    m_peers.erase(it);
}

void WinsockTransport::DisconnectAll() {
    auto ids = GetConnections();
    for (auto id : ids) Disconnect(id);
    m_peers.clear();
}

void WinsockTransport::Poll() {
    if (m_socket == ~0ULL) return;

    sockaddr_in fromAddr = {};
    int fromLen = sizeof(fromAddr);

    while (true) {
        int bytes = recvfrom(static_cast<SOCKET>(m_socket), reinterpret_cast<char*>(m_recvBuffer),
                             kRecvBufferSize, 0,
                             reinterpret_cast<sockaddr*>(&fromAddr), &fromLen);

        if (bytes <= 0) break;

        // Parse packet header
        uint8_t type = m_recvBuffer[0];

        if (type == 0x01 && bytes >= 5 && m_listening) {
            // Connection request from a client
            char addrStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &fromAddr.sin_addr, addrStr, sizeof(addrStr));
            uint16_t fromPort = ntohs(fromAddr.sin_port);

            ConnectionId cid = AllocateConnectionId();
            Peer peer;
            peer.id = cid;
            peer.state = ConnectionState::Connected;
            peer.address = addrStr;
            peer.port = fromPort;
            m_peers[cid] = std::move(peer);

            // Send accept packet (type 0x03)
            uint8_t accept[5] = { 0x03 };
            memcpy(accept + 1, &cid, 4);
            sendto(static_cast<SOCKET>(m_socket), reinterpret_cast<const char*>(accept),
                   sizeof(accept), 0,
                   reinterpret_cast<sockaddr*>(&fromAddr), fromLen);

            if (m_callbacks.onConnected) m_callbacks.onConnected(cid);
        }
        else if (type == 0x02 && bytes >= 5) {
            // Disconnect notification
            ConnectionId cid;
            memcpy(&cid, m_recvBuffer + 1, 4);
            // Find by address match
            for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
                char addrStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &fromAddr.sin_addr, addrStr, sizeof(addrStr));
                if (it->second.address == addrStr && it->second.port == ntohs(fromAddr.sin_port)) {
                    ConnectionId peerCid = it->first;
                    if (m_callbacks.onDisconnected) m_callbacks.onDisconnected(peerCid);
                    m_peers.erase(it);
                    break;
                }
            }
        }
        else if (type == 0x03 && bytes >= 5) {
            // Connection accepted — mark as connected
            ConnectionId assignedCid;
            memcpy(&assignedCid, m_recvBuffer + 1, 4);
            // Find the connecting peer by address
            char addrStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &fromAddr.sin_addr, addrStr, sizeof(addrStr));
            for (auto& [cid, peer] : m_peers) {
                if (peer.state == ConnectionState::Connecting && peer.address == addrStr) {
                    peer.state = ConnectionState::Connected;
                    if (m_callbacks.onConnected) m_callbacks.onConnected(cid);
                    break;
                }
            }
        }
        else if (type == 0x10 && bytes > 5) {
            // Unreliable data: [type=0x10][connId:4][payload...]
            ConnectionId cid;
            memcpy(&cid, m_recvBuffer + 1, 4);

            // Find by address
            char addrStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &fromAddr.sin_addr, addrStr, sizeof(addrStr));
            uint16_t fromPort = ntohs(fromAddr.sin_port);

            for (auto& [pid, peer] : m_peers) {
                if (peer.address == addrStr && peer.port == fromPort) {
                    peer.bytesRecv += bytes;
                    if (m_callbacks.onDataReceived) {
                        m_callbacks.onDataReceived(pid, m_recvBuffer + 5, bytes - 5);
                    }
                    break;
                }
            }
        }
        else if (type == 0x11 && bytes > 7) {
            // Reliable data: [type=0x11][connId:4][seq:2][payload...]
            ConnectionId cid;
            memcpy(&cid, m_recvBuffer + 1, 4);
            uint16_t seq;
            memcpy(&seq, m_recvBuffer + 5, 2);

            char addrStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &fromAddr.sin_addr, addrStr, sizeof(addrStr));
            uint16_t fromPort = ntohs(fromAddr.sin_port);

            for (auto& [pid, peer] : m_peers) {
                if (peer.address == addrStr && peer.port == fromPort) {
                    peer.bytesRecv += bytes;

                    // Send ack: [type=0x12][seq:2]
                    uint8_t ack[3] = { 0x12 };
                    memcpy(ack + 1, &seq, 2);
                    sendto(static_cast<SOCKET>(m_socket), reinterpret_cast<const char*>(ack),
                           sizeof(ack), 0,
                           reinterpret_cast<sockaddr*>(&fromAddr), fromLen);

                    // Update remote sequence tracking
                    if (seq > peer.remoteSeq) {
                        uint16_t diff = seq - peer.remoteSeq;
                        peer.ackBits <<= diff;
                        peer.ackBits |= 1;
                        peer.remoteSeq = seq;
                    } else {
                        uint16_t diff = peer.remoteSeq - seq;
                        if (diff < 32) peer.ackBits |= (1u << diff);
                    }

                    if (m_callbacks.onDataReceived) {
                        m_callbacks.onDataReceived(pid, m_recvBuffer + 7, bytes - 7);
                    }
                    break;
                }
            }
        }
        else if (type == 0x12 && bytes >= 3) {
            // Ack received: [type=0x12][seq:2]
            uint16_t ackedSeq;
            memcpy(&ackedSeq, m_recvBuffer + 1, 2);

            char addrStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &fromAddr.sin_addr, addrStr, sizeof(addrStr));
            uint16_t fromPort = ntohs(fromAddr.sin_port);

            for (auto& [pid, peer] : m_peers) {
                if (peer.address == addrStr && peer.port == fromPort) {
                    // Remove from pending reliable queue
                    peer.pendingReliable.erase(
                        std::remove_if(peer.pendingReliable.begin(), peer.pendingReliable.end(),
                            [ackedSeq](const Peer::PendingReliable& pr) { return pr.seq == ackedSeq; }),
                        peer.pendingReliable.end()
                    );
                    break;
                }
            }
        }
    }

    // Update connected time for all peers
    // (Time tracking done externally via frame dt — here we just maintain state)
}

bool WinsockTransport::Send(ConnectionId id, const uint8_t* data, int size, SendFlags flags) {
    auto* peer = FindPeer(id);
    if (!peer || peer->state != ConnectionState::Connected) return false;
    if (size <= 0 || size > kRecvBufferSize - 8) return false;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(peer->port);
    inet_pton(AF_INET, peer->address.c_str(), &addr.sin_addr);

    if (flags & SendFlags::Reliable) {
        // Reliable: [0x11][connId:4][seq:2][payload]
        uint8_t header[7] = { 0x11 };
        memcpy(header + 1, &id, 4);
        uint16_t seq = peer->localSeq++;
        memcpy(header + 5, &seq, 2);

        // Build combined message
        std::vector<uint8_t> msg(7 + size);
        memcpy(msg.data(), header, 7);
        memcpy(msg.data() + 7, data, size);

        sendto(static_cast<SOCKET>(m_socket), reinterpret_cast<const char*>(msg.data()),
               static_cast<int>(msg.size()), 0,
               reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

        // Store for retransmission
        Peer::PendingReliable pr;
        pr.seq = seq;
        pr.data = std::move(msg);
        pr.sendTime = 0.0f;
        pr.retransmitTimer = 0.1f; // 100ms initial retransmit
        peer->pendingReliable.push_back(std::move(pr));

        peer->bytesSent += 7 + size;
    } else {
        // Unreliable: [0x10][connId:4][payload]
        uint8_t header[5] = { 0x10 };
        memcpy(header + 1, &id, 4);

        std::vector<uint8_t> msg(5 + size);
        memcpy(msg.data(), header, 5);
        memcpy(msg.data() + 5, data, size);

        sendto(static_cast<SOCKET>(m_socket), reinterpret_cast<const char*>(msg.data()),
               static_cast<int>(msg.size()), 0,
               reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

        peer->bytesSent += 5 + size;
    }

    return true;
}

void WinsockTransport::Broadcast(const uint8_t* data, int size, SendFlags flags) {
    for (auto& [cid, peer] : m_peers) {
        if (peer.state == ConnectionState::Connected) {
            Send(cid, data, size, flags);
        }
    }
}

ConnectionInfo WinsockTransport::GetConnectionInfo(ConnectionId id) const {
    const auto* peer = FindPeer(id);
    if (!peer) return {};

    ConnectionInfo info;
    info.id = peer->id;
    info.state = peer->state;
    info.address = peer->address + ":" + std::to_string(peer->port);
    info.rttMs = peer->rttMs;
    info.bytesSent = peer->bytesSent;
    info.bytesRecv = peer->bytesRecv;
    info.connectedTime = peer->connectedTime;
    return info;
}

std::vector<ConnectionId> WinsockTransport::GetConnections() const {
    std::vector<ConnectionId> ids;
    ids.reserve(m_peers.size());
    for (const auto& [cid, peer] : m_peers) {
        ids.push_back(cid);
    }
    return ids;
}

int WinsockTransport::GetConnectionCount() const {
    return static_cast<int>(m_peers.size());
}

ConnectionId WinsockTransport::AllocateConnectionId() {
    return m_nextConnectionId++;
}

WinsockTransport::Peer* WinsockTransport::FindPeer(ConnectionId id) {
    auto it = m_peers.find(id);
    return (it != m_peers.end()) ? &it->second : nullptr;
}

const WinsockTransport::Peer* WinsockTransport::FindPeer(ConnectionId id) const {
    auto it = m_peers.find(id);
    return (it != m_peers.end()) ? &it->second : nullptr;
}

} // namespace WT
