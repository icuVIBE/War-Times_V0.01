#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>

namespace WT {

// ============================================================
// Connection ID — uniquely identifies a remote peer
// ============================================================

using ConnectionId = uint32_t;
static constexpr ConnectionId InvalidConnection = 0;

// ============================================================
// Connection State
// ============================================================

enum class ConnectionState : uint8_t {
    Disconnected = 0,
    Connecting,
    Connected,
    Disconnecting
};

inline const char* ConnectionStateName(ConnectionState s) {
    switch (s) {
        case ConnectionState::Disconnected:   return "Disconnected";
        case ConnectionState::Connecting:     return "Connecting";
        case ConnectionState::Connected:      return "Connected";
        case ConnectionState::Disconnecting:  return "Disconnecting";
        default: return "Unknown";
    }
}

// ============================================================
// Send Flags
// ============================================================

enum class SendFlags : uint8_t {
    Unreliable    = 0,       // Fire-and-forget (game state, positions)
    Reliable      = 1 << 0,  // Guaranteed delivery, ordered
    NoNagle       = 1 << 1,  // Flush immediately, don't buffer
    NoDelay       = 1 << 2,  // Low latency, skip internal buffering
};

inline SendFlags operator|(SendFlags a, SendFlags b) {
    return static_cast<SendFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline bool operator&(SendFlags a, SendFlags b) {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

// ============================================================
// Connection Info — stats for a single peer connection
// ============================================================

struct ConnectionInfo {
    ConnectionId    id = InvalidConnection;
    ConnectionState state = ConnectionState::Disconnected;
    std::string     address;           // IP:Port string
    float           rttMs = 0.0f;      // Round-trip time in milliseconds
    float           packetLoss = 0.0f; // 0..1 fraction
    uint64_t        bytesSent = 0;
    uint64_t        bytesRecv = 0;
    float           connectedTime = 0.0f; // Seconds since connected
};

// ============================================================
// Transport Events — callbacks for connection lifecycle
// ============================================================

struct TransportCallbacks {
    std::function<void(ConnectionId)> onConnected;
    std::function<void(ConnectionId)> onDisconnected;
    std::function<void(ConnectionId, const uint8_t* data, int size)> onDataReceived;
};

// ============================================================
// Transport — abstract network transport layer
// ============================================================
// Provides connection management, send/receive, and connection
// lifecycle. The default implementation uses Winsock UDP. When
// GameNetworkingSockets is available, a derived class can be
// substituted for reliable UDP, encryption, and NAT traversal.

class Transport {
public:
    virtual ~Transport() = default;

    // Initialise the transport subsystem.
    virtual bool Init() = 0;

    // Shut down, close all connections.
    virtual void Shutdown() = 0;

    // Start listening for incoming connections on a port.
    virtual bool Listen(uint16_t port) = 0;

    // Connect to a remote host.
    virtual bool Connect(const std::string& address, uint16_t port) = 0;

    // Disconnect a specific peer.
    virtual void Disconnect(ConnectionId id) = 0;

    // Close all connections.
    virtual void DisconnectAll() = 0;

    // Poll for incoming messages and connection events.
    // Should be called once per frame/tick.
    virtual void Poll() = 0;

    // Send data to a specific connection.
    virtual bool Send(ConnectionId id, const uint8_t* data, int size, SendFlags flags = SendFlags::Unreliable) = 0;

    // Send data to all connected peers.
    virtual void Broadcast(const uint8_t* data, int size, SendFlags flags = SendFlags::Unreliable) = 0;

    // Get connection info for a specific peer.
    virtual ConnectionInfo GetConnectionInfo(ConnectionId id) const = 0;

    // Get all active connection IDs.
    virtual std::vector<ConnectionId> GetConnections() const = 0;

    // Get count of active connections.
    virtual int GetConnectionCount() const = 0;

    // Set event callbacks.
    void SetCallbacks(const TransportCallbacks& cb) { m_callbacks = cb; }

protected:
    TransportCallbacks m_callbacks;
};

// ============================================================
// WinsockTransport — basic UDP transport using Winsock2
// ============================================================
// Provides unreliable UDP for game state and a simple sequence-
// based reliable layer for important messages. This is a
// placeholder that can be replaced with GameNetworkingSockets.

class WinsockTransport : public Transport {
public:
    bool Init() override;
    void Shutdown() override;
    bool Listen(uint16_t port) override;
    bool Connect(const std::string& address, uint16_t port) override;
    void Disconnect(ConnectionId id) override;
    void DisconnectAll() override;
    void Poll() override;
    bool Send(ConnectionId id, const uint8_t* data, int size, SendFlags flags) override;
    void Broadcast(const uint8_t* data, int size, SendFlags flags) override;
    ConnectionInfo GetConnectionInfo(ConnectionId id) const override;
    std::vector<ConnectionId> GetConnections() const override;
    int GetConnectionCount() const override;

private:
    struct Peer {
        ConnectionId id = InvalidConnection;
        ConnectionState state = ConnectionState::Disconnected;
        std::string address;
        uint16_t port = 0;
        float rttMs = 0.0f;
        uint64_t bytesSent = 0;
        uint64_t bytesRecv = 0;
        float connectedTime = 0.0f;

        // Reliable sequencing
        uint16_t localSeq = 0;      // Next outgoing sequence number
        uint16_t remoteSeq = 0;     // Latest received sequence number
        uint32_t ackBits = 0;       // Bitfield of received packets (relative to remoteSeq)

        // Pending reliable messages awaiting ack
        struct PendingReliable {
            uint16_t seq;
            std::vector<uint8_t> data;
            float sendTime;
            float retransmitTimer;
        };
        std::vector<PendingReliable> pendingReliable;
    };

    ConnectionId AllocateConnectionId();
    Peer* FindPeer(ConnectionId id);
    const Peer* FindPeer(ConnectionId id) const;

    uintptr_t m_socket = ~0ULL;        // SOCKET handle (stored as uintptr_t to avoid winsock include)
    bool m_listening = false;
    uint16_t m_listenPort = 0;

    std::unordered_map<ConnectionId, Peer> m_peers;
    ConnectionId m_nextConnectionId = 1;

    // Recv buffer
    static constexpr int kRecvBufferSize = 4096;
    uint8_t m_recvBuffer[kRecvBufferSize] = {};
};

} // namespace WT
