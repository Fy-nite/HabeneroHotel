// Platform socket headers MUST come first in this TU and nowhere else,
// so that winsock2.h / windows.h never contaminates raylib-using TUs.
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <mswsock.h>   // SIO_UDP_CONNRESET
  using SocketHandle = SOCKET;
  static constexpr SocketHandle INVALID_SOCK_VAL = INVALID_SOCKET;
  using SockLen = int;
  #if defined(_MSC_VER)
    #pragma comment(lib, "ws2_32.lib")
  #endif
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>       // getaddrinfo, freeaddrinfo, gai_strerror
  #include <unistd.h>
  using SocketHandle = int;
  static constexpr SocketHandle INVALID_SOCK_VAL = -1;
  using SockLen = socklen_t;
#endif

// Now include our own header (it no longer pulls windows.h)
#include <server/NetworkManager.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

namespace Hotones::Net {

// ─── Internal types (invisible to all other TUs) ─────────────────────────────

struct ClientSlot {
    sockaddr_in addr     = {};
    uint8_t     id       = 0;
    char        name[16] = {};
    bool        active   = false;
};

struct RawPacket {
    uint8_t     data[512] = {};
    int         len       = 0;
    sockaddr_in from      = {};
};

// ─── Impl ─────────────────────────────────────────────────────────────────────

struct NetworkManager::Impl {
    // Socket
    SocketHandle      socket  = INVALID_SOCK_VAL;

    // Mode / run state
    NetworkManager::Mode  mode    = NetworkManager::Mode::None;
    std::atomic<bool>     running { false };
    std::thread           recvThread;

    // Thread-safe receive queue
    std::mutex           queueMutex;
    std::queue<RawPacket> recvQueue;

    // Server state
    ClientSlot clients[MAX_PLAYERS];
    uint8_t    nextId = 1;

    // Client state
    sockaddr_in serverAddr  = {};
    uint8_t     localId     = 0;
    bool        connected   = false;
    char        localName[16] = "Player";

    // Remote player snapshots
    std::unordered_map<uint8_t, RemotePlayer> remotePlayers;

    // Connection retry (client mode)
    std::chrono::steady_clock::time_point lastConnectAttempt {};
    int connectAttempts = 0;
    static constexpr int    MAX_CONNECT_ATTEMPTS  = 15;
    static constexpr int    CONNECT_RETRY_MS      = 500;

    // ── Socket helpers ────────────────────────────────────────────────────────
    bool InitSocket(uint16_t bindPort) {
        socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket == INVALID_SOCK_VAL) {
            std::cerr << "[Net] socket() failed\n";
            return false;
        }
        int opt = 1;
        setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&opt), sizeof(opt));

#ifdef _WIN32
        // Disable ICMP Port Unreachable from causing recvfrom to return WSAECONNRESET.
        // Without this, any UDP packet that hits a closed port on the remote machine
        // causes the NEXT recvfrom on THIS socket to return -1, silently consuming
        // a real incoming packet (e.g. ConnectAckPacket). Hits cross-machine only;
        // loopback suppresses ICMP errors, which is why localhost appeared to work.
        {
            DWORD dwBytes  = 0;
            BOOL  bDisable = FALSE;
            WSAIoctl(socket, SIO_UDP_CONNRESET,
                     &bDisable, sizeof(bDisable),
                     nullptr, 0, &dwBytes, nullptr, nullptr);
        }
#endif

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(bindPort);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            std::cerr << "[Net] bind() failed on port " << bindPort << "\n";
            CloseSocket();
            return false;
        }
        // 200 ms recv timeout so RecvLoop can check `running` periodically
#ifdef _WIN32
        DWORD tv = 200;
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
        timeval tv{ 0, 200000 };
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
        return true;
    }

    void CloseSocket() {
        if (socket == INVALID_SOCK_VAL) return;
#ifdef _WIN32
        closesocket(socket);
#else
        close(socket);
#endif
        socket = INVALID_SOCK_VAL;
    }

    void SendRaw(const sockaddr_in& addr, const void* data, int len) {
#ifdef _WIN32
        sendto(socket, reinterpret_cast<const char*>(data), len, 0,
               reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
#else
        sendto(socket, data, static_cast<size_t>(len), 0,
               reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
#endif
    }

    // ── Background receive thread ─────────────────────────────────────────────
    void RecvLoop() {
        uint8_t buf[512];
        while (running.load()) {
            // Client: resend ConnectPacket every CONNECT_RETRY_MS until acknowledged.
            if (mode == NetworkManager::Mode::Client && !connected
                    && connectAttempts < MAX_CONNECT_ATTEMPTS) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    now - lastConnectAttempt).count();
                if (elapsed >= CONNECT_RETRY_MS) {
                    ConnectPacket pkt{};
                    pkt.header.type     = PacketType::CONNECT;
                    pkt.header.playerId = 0;
                    std::strncpy(pkt.name, localName, 15);
                    pkt.name[15] = '\0';
                    SendRaw(serverAddr, &pkt, sizeof(pkt));
                    lastConnectAttempt = now;
                    ++connectAttempts;
                    std::cout << "[Net] ConnectPacket attempt "
                              << connectAttempts << "/" << MAX_CONNECT_ATTEMPTS << "\n";
                }
            }

            sockaddr_in from{};
            SockLen fromLen = sizeof(from);
#ifdef _WIN32
            int n = recvfrom(socket,
                             reinterpret_cast<char*>(buf),
                             static_cast<int>(sizeof(buf)),
                             0,
                             reinterpret_cast<sockaddr*>(&from), &fromLen);
#else
            ssize_t n = recvfrom(socket, buf, sizeof(buf), 0,
                                 reinterpret_cast<sockaddr*>(&from), &fromLen);
#endif
            if (n <= 0) continue; // timeout / EAGAIN — loop and check running
            if (n < static_cast<int>(sizeof(PacketHeader))) continue;

            RawPacket rp;
            rp.len  = (n < static_cast<int>(sizeof(rp.data)))
                      ? n : static_cast<int>(sizeof(rp.data));
            std::memcpy(rp.data, buf, rp.len);
            rp.from = from;

            std::lock_guard<std::mutex> lk(queueMutex);
            recvQueue.push(rp);
        }
    }

    // ── Server broadcast ──────────────────────────────────────────────────────
    void Server_Broadcast(const uint8_t* data, int len, uint8_t excludeId = 0xFF) {
        for (auto& slot : clients)
            if (slot.active && slot.id != excludeId)
                SendRaw(slot.addr, data, len);
    }

    // ── Server packet handlers ────────────────────────────────────────────────
    void Server_HandleConnect(const ConnectPacket& pkt, const sockaddr_in& from,
                               NetworkManager& nm) {
        // Re-send ACK if already registered (idempotent connect)
        for (auto& slot : clients) {
            if (slot.active &&
                slot.addr.sin_addr.s_addr == from.sin_addr.s_addr &&
                slot.addr.sin_port        == from.sin_port) {
                ConnectAckPacket ack{};
                ack.header.type     = PacketType::CONNECT_ACK;
                ack.header.playerId = slot.id;
                ack.assignedId      = slot.id;
                SendRaw(from, &ack, sizeof(ack));
                return;
            }
        }
        // Find a free slot
        ClientSlot* slot = nullptr;
        for (auto& s : clients) { if (!s.active) { slot = &s; break; } }
        if (!slot) { std::cerr << "[Net] Server full\n"; return; }

        slot->active = true;
        slot->addr   = from;
        slot->id     = nextId++;
        std::strncpy(slot->name, pkt.name, 15);
        slot->name[15] = '\0';

        ConnectAckPacket ack{};
        ack.header.type     = PacketType::CONNECT_ACK;
        ack.header.playerId = slot->id;
        ack.assignedId      = slot->id;
        SendRaw(from, &ack, sizeof(ack));

        // Notify other clients (zeroed position intro)
        PlayerUpdatePacket intro{};
        intro.header.type     = PacketType::PLAYER_UPDATE;
        intro.header.playerId = slot->id;
        Server_Broadcast(reinterpret_cast<uint8_t*>(&intro), sizeof(intro), slot->id);

        std::cout << "[Net] Player " << static_cast<int>(slot->id)
                  << " (\"" << slot->name << "\") joined\n";
        if (nm.OnPlayerJoined) nm.OnPlayerJoined(slot->id, slot->name);
    }

    void Server_HandleDisconnect(const DisconnectPacket& /*pkt*/,
                                  const sockaddr_in& from, NetworkManager& nm) {
        for (auto& slot : clients) {
            if (slot.active &&
                slot.addr.sin_addr.s_addr == from.sin_addr.s_addr &&
                slot.addr.sin_port        == from.sin_port) {
                std::cout << "[Net] Player " << static_cast<int>(slot.id)
                          << " (\"" << slot.name << "\") left\n";
                DisconnectPacket dc{};
                dc.header.type     = PacketType::DISCONNECT;
                dc.header.playerId = slot.id;
                Server_Broadcast(reinterpret_cast<uint8_t*>(&dc), sizeof(dc), slot.id);
                if (nm.OnPlayerLeft) nm.OnPlayerLeft(slot.id);
                slot.active = false;
                return;
            }
        }
    }

    void Server_HandlePlayerUpdate(const PlayerUpdatePacket& pkt,
                                    const sockaddr_in& from) {
        for (auto& slot : clients) {
            if (slot.active && slot.id == pkt.header.playerId &&
                slot.addr.sin_addr.s_addr == from.sin_addr.s_addr) {
                // Keep a server-side snapshot so the hosting player can
                // render remote clients via GetRemotePlayers().
                auto& rp  = remotePlayers[pkt.header.playerId];
                rp.id     = pkt.header.playerId;
                rp.posX   = pkt.posX; rp.posY = pkt.posY; rp.posZ = pkt.posZ;
                rp.rotX   = pkt.rotX; rp.rotY = pkt.rotY;
                rp.active = true;
                Server_Broadcast(reinterpret_cast<const uint8_t*>(&pkt),
                                 sizeof(pkt), pkt.header.playerId);
                return;
            }
        }
    }

    // ── Client packet handlers ────────────────────────────────────────────────
    void Client_HandleConnectAck(const ConnectAckPacket& pkt, NetworkManager& nm) {
        localId   = pkt.assignedId;
        connected = true;
        std::cout << "[Net] Connected! Assigned player ID "
                  << static_cast<int>(localId) << "\n";
        if (nm.OnPlayerJoined) nm.OnPlayerJoined(localId, localName);
    }

    void Client_HandleDisconnect(const DisconnectPacket& pkt, NetworkManager& nm) {
        uint8_t id = pkt.header.playerId;
        if (id == localId) {
            connected = false;
            remotePlayers.clear();
            std::cout << "[Net] Kicked by server\n";
            if (nm.OnPlayerLeft) nm.OnPlayerLeft(localId);
        } else {
            remotePlayers.erase(id);
            std::cout << "[Net] Player " << static_cast<int>(id) << " left\n";
            if (nm.OnPlayerLeft) nm.OnPlayerLeft(id);
        }
    }

    void Client_HandlePlayerUpdate(const PlayerUpdatePacket& pkt) {
        uint8_t id = pkt.header.playerId;
        if (id == localId) return;
        auto& rp  = remotePlayers[id];
        rp.id     = id;
        rp.posX   = pkt.posX; rp.posY = pkt.posY; rp.posZ = pkt.posZ;
        rp.rotX   = pkt.rotX; rp.rotY = pkt.rotY;
        rp.active = true;
    }

    // ── Main-thread packet dispatch ───────────────────────────────────────────
    void DispatchPacket(const RawPacket& rp, NetworkManager& nm) {
        const auto& hdr = *reinterpret_cast<const PacketHeader*>(rp.data);
        if (mode == NetworkManager::Mode::Server) {
            switch (hdr.type) {
            case PacketType::CONNECT:
                if (rp.len >= static_cast<int>(sizeof(ConnectPacket)))
                    Server_HandleConnect(*reinterpret_cast<const ConnectPacket*>(rp.data), rp.from, nm);
                break;
            case PacketType::DISCONNECT:
                if (rp.len >= static_cast<int>(sizeof(DisconnectPacket)))
                    Server_HandleDisconnect(*reinterpret_cast<const DisconnectPacket*>(rp.data), rp.from, nm);
                break;
            case PacketType::PLAYER_UPDATE:
                if (rp.len >= static_cast<int>(sizeof(PlayerUpdatePacket)))
                    Server_HandlePlayerUpdate(*reinterpret_cast<const PlayerUpdatePacket*>(rp.data), rp.from);
                break;
            default: break;
            }
        } else if (mode == NetworkManager::Mode::Client) {
            switch (hdr.type) {
            case PacketType::CONNECT_ACK:
                if (rp.len >= static_cast<int>(sizeof(ConnectAckPacket)))
                    Client_HandleConnectAck(*reinterpret_cast<const ConnectAckPacket*>(rp.data), nm);
                break;
            case PacketType::DISCONNECT:
                if (rp.len >= static_cast<int>(sizeof(DisconnectPacket)))
                    Client_HandleDisconnect(*reinterpret_cast<const DisconnectPacket*>(rp.data), nm);
                break;
            case PacketType::PLAYER_UPDATE:
                if (rp.len >= static_cast<int>(sizeof(PlayerUpdatePacket)))
                    Client_HandlePlayerUpdate(*reinterpret_cast<const PlayerUpdatePacket*>(rp.data));
                break;
            default: break;
            }
        }
    }
};

// ─── NetworkManager public methods ───────────────────────────────────────────

NetworkManager::NetworkManager() : m_impl(std::make_unique<Impl>()) {
#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        std::cerr << "[Net] WSAStartup failed\n";
#endif
}

NetworkManager::~NetworkManager() {
    if (m_impl->mode == Mode::Server) StopServer();
    else if (m_impl->mode == Mode::Client) Disconnect();
#ifdef _WIN32
    WSACleanup();
#endif
}

// ── Server ────────────────────────────────────────────────────────────────────

bool NetworkManager::StartServer(uint16_t port) {
    if (m_impl->running.load()) return false;
    if (!m_impl->InitSocket(port)) return false;
    m_impl->mode    = Mode::Server;
    m_impl->nextId  = 1;
    std::memset(m_impl->clients, 0, sizeof(m_impl->clients));
    m_impl->running = true;
    m_impl->recvThread = std::thread([this]{ m_impl->RecvLoop(); });
    std::cout << "[Net] Server started on port " << port << "\n";
    return true;
}

void NetworkManager::StopServer() {
    if (!m_impl->running.load()) return;
    m_impl->running = false;
    if (m_impl->recvThread.joinable()) m_impl->recvThread.join();
    m_impl->CloseSocket();
    m_impl->mode = Mode::None;
    std::cout << "[Net] Server stopped\n";
}

bool NetworkManager::IsServerRunning() const {
    return m_impl->mode == Mode::Server && m_impl->running.load();
}

// ── Client ────────────────────────────────────────────────────────────────────

bool NetworkManager::Connect(const std::string& host, uint16_t port,
                               const std::string& playerName) {
    if (m_impl->running.load()) return false;
    if (!m_impl->InitSocket(0)) return false;  // ephemeral local port

    // Resolve host — handles both IP strings ("192.168.1.5") and hostnames ("myserver").
    // inet_pton only handles IP strings, which is why cross-machine hostname connects failed.
    {
        addrinfo hints{};
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        addrinfo* res     = nullptr;
        std::string portStr = std::to_string(port);
        int err = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
        if (err != 0 || !res) {
            std::cerr << "[Net] Cannot resolve host '" << host << "': "
                      << gai_strerror(err) << "\n";
            m_impl->CloseSocket();
            return false;
        }
        std::memcpy(&m_impl->serverAddr, res->ai_addr, sizeof(sockaddr_in));
        freeaddrinfo(res);
    }
    std::strncpy(m_impl->localName, playerName.c_str(), 15);
    m_impl->localName[15] = '\0';

    m_impl->mode    = Mode::Client;
    m_impl->running = true;
    m_impl->recvThread = std::thread([this]{ m_impl->RecvLoop(); });

    // Send the initial ConnectPacket; RecvLoop will retry every 500ms until ACKed.
    ConnectPacket pkt{};
    pkt.header.type     = PacketType::CONNECT;
    pkt.header.playerId = 0;
    std::strncpy(pkt.name, m_impl->localName, 15);
    pkt.name[15] = '\0';
    m_impl->SendRaw(m_impl->serverAddr, &pkt, sizeof(pkt));
    m_impl->lastConnectAttempt = std::chrono::steady_clock::now();
    m_impl->connectAttempts    = 1;

    std::cout << "[Net] Connecting to " << host << ":" << port
              << " as \"" << m_impl->localName << "\"...\n";
    return true;
}

void NetworkManager::Disconnect() {
    if (!m_impl->running.load()) return;
    if (m_impl->connected) {
        DisconnectPacket pkt{};
        pkt.header.type     = PacketType::DISCONNECT;
        pkt.header.playerId = m_impl->localId;
        m_impl->SendRaw(m_impl->serverAddr, &pkt, sizeof(pkt));
    }
    m_impl->running          = false;
    m_impl->connected        = false;
    m_impl->localId          = 0;
    m_impl->connectAttempts  = 0;
    if (m_impl->recvThread.joinable()) m_impl->recvThread.join();
    m_impl->CloseSocket();
    m_impl->remotePlayers.clear();
    m_impl->mode = Mode::None;
    std::cout << "[Net] Disconnected\n";
}

bool NetworkManager::IsConnected() const { return m_impl->connected; }

void NetworkManager::SendPlayerUpdate(float px, float py, float pz,
                                       float rotX, float rotY) {
    PlayerUpdatePacket pkt{};
    pkt.header.type = PacketType::PLAYER_UPDATE;
    pkt.posX = px; pkt.posY = py; pkt.posZ = pz;
    pkt.rotX = rotX; pkt.rotY = rotY;

    if (m_impl->mode == Mode::Client && m_impl->connected) {
        pkt.header.playerId = m_impl->localId;
        m_impl->SendRaw(m_impl->serverAddr, &pkt, sizeof(pkt));
    } else if (m_impl->mode == Mode::Server) {
        // Broadcast the host's position to all connected clients.
        // Player ID 0 is reserved for the server/host; clients treat it
        // as any other remote player and render it normally.
        pkt.header.playerId = 0;
        m_impl->Server_Broadcast(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    }
}

// ── Shared ────────────────────────────────────────────────────────────────────

void NetworkManager::Update() {
    std::queue<RawPacket> local;
    {
        std::lock_guard<std::mutex> lk(m_impl->queueMutex);
        std::swap(local, m_impl->recvQueue);
    }
    while (!local.empty()) {
        m_impl->DispatchPacket(local.front(), *this);
        local.pop();
    }
}

NetworkManager::Mode NetworkManager::GetMode() const { return m_impl->mode; }
uint8_t NetworkManager::GetLocalId()             const { return m_impl->localId; }

const std::unordered_map<uint8_t, RemotePlayer>&
NetworkManager::GetRemotePlayers() const { return m_impl->remotePlayers; }

} // namespace Hotones::Net