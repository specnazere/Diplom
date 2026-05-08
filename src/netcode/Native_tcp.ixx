module;

#include <memory>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>
#include <type_traits>
#include <cstring>
#include <iostream>
#include <ostream>
#pragma comment(lib, "Ws2_32.lib")

export module native_tcp;

#define TCP_TYPE_INDEX(T) static_cast<uint32_t>(T::tcp_type_id)

namespace net {
namespace detail {
    SOCKET g_sock = INVALID_SOCKET;
    SOCKET g_client_sock = INVALID_SOCKET; // Для сервера: сокет конкретного клиента
    bool g_is_server = false;
    bool g_initialized = false;

    void ensure_init() {
        if (!g_initialized) {
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                throw std::runtime_error("WSAStartup failed");
            }
            g_initialized = true;
        }
    }

    std::string get_error() {
        return "Error code: " + std::to_string(WSAGetLastError());
    }

    // Читает ровно size байт. Блокирует, пока не прочитает всё или не отвалится соединение.
    void recv_exact(SOCKET sock, void* buffer, size_t size) {
        size_t total_read = 0;
        char* buf_ptr = static_cast<char*>(buffer);

        while (total_read < size) {
            int result = recv(sock, buf_ptr + total_read, static_cast<int>(size - total_read), 0);
            if (result <= 0) {
                if (result == 0) {
                    throw std::runtime_error("Connection closed by peer");
                }
                throw std::runtime_error("Recv failed: " + get_error());
            }
            total_read += result;
        }
        return;
    }
}

struct separate_data
{
    uint32_t type;
    uint32_t data_size;
};

// --- SERVER SIDE ---

export
void listen_start(unsigned short port, int backlog = 1) {
    detail::ensure_init();
    detail::g_is_server = true;

    SOCKET server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock == INVALID_SOCKET) {
        throw std::runtime_error("Socket creation failed: " + detail::get_error());
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(server_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        closesocket(server_sock);
        throw std::runtime_error("Bind failed: " + detail::get_error());
    }

    if (::listen(server_sock, backlog) < 0) {
        closesocket(server_sock);
        throw std::runtime_error("Listen failed: " + detail::get_error());
    }

    detail::g_sock = server_sock;
}

export
bool accept_connection(int timeout_ms = 5000) {
    if (detail::g_sock == INVALID_SOCKET || !detail::g_is_server) {
        throw std::runtime_error("Server not listening");
    }

    // Устанавливаем таймаут на accept через select или SO_RCVTIMEO (зависит от реализации, проще через select)
    // Для простоты оставим блокирующий accept, но можно добавить таймаут через setsockopt SO_RCVTIMEO на listening socket (не везде работает для accept)
    // Или использовать non-blocking mode. Оставим просто blocking для начала.

    sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);

    SOCKET client_sock = ::accept(detail::g_sock, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
    if (client_sock == INVALID_SOCKET) {
        return false;
    }

    detail::g_client_sock = client_sock;
    return true;
}

// --- CLIENT SIDE ---

export
void connect_to_host(const std::string& ip, unsigned short port) {
    detail::ensure_init();
    detail::g_is_server = false;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        throw std::runtime_error("Socket creation failed: " + detail::get_error());
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        closesocket(sock);
        throw std::runtime_error("Invalid IP address");
    }

    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        closesocket(sock);
        throw std::runtime_error("Connect failed: " + detail::get_error());
    }

    detail::g_sock = sock;
}

// --- COMMON SEND/RECEIVE ---

// Helper to get active socket
SOCKET get_active_socket() {
    if (detail::g_is_server) {
        if (detail::g_client_sock == INVALID_SOCKET) throw std::runtime_error("No client connected");
        return detail::g_client_sock;
    } else {
        if (detail::g_sock == INVALID_SOCKET) throw std::runtime_error("Not connected");
        return detail::g_sock;
    }
}

export
void send_raw(const void* data, size_t size) {
    SOCKET sock = get_active_socket();
    const char* buf_ptr = static_cast<const char*>(data);
    size_t total_sent = 0;

    while (total_sent < size) {
        int result = send(sock, buf_ptr + total_sent, static_cast<int>(size - total_sent), 0);
        if (result <= 0) {
            throw std::runtime_error("Send failed: " + detail::get_error());
        }
        total_sent += result;
    }
}

export
template <typename T>
requires std::is_trivially_copyable_v<T>
void send_typed(const T& data) {
    separate_data misc;
    misc.type = TCP_TYPE_INDEX(T);
    misc.data_size = sizeof(T);

    // Отправляем заголовок
    send_raw(&misc, sizeof(misc));

    // Отправляем данные
    send_raw(&data, sizeof(T));
}

export
template <typename T>
requires std::is_trivially_copyable_v<T>
T receive_typed(int timeout_ms = 1000) {
    SOCKET sock = get_active_socket();

    // Set timeout
    DWORD timeout = static_cast<DWORD>(timeout_ms);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    separate_data misc;

    // Читаем заголовок строго по размеру
    detail::recv_exact(sock, &misc, sizeof(misc));

    if (misc.type != TCP_TYPE_INDEX(T)) {
        throw std::runtime_error("Type mismatch in received data");
    }

    T data{};
    // std::cout << sizeof(uint32_t) << " " << &data << " " << &data + sizeof(uint32_t) << std::endl;
    detail::recv_exact(sock, reinterpret_cast<void*>(&data), sizeof(T));

    return data;
}

export
void cleanup() {
    if (detail::g_client_sock != INVALID_SOCKET) {
        closesocket(detail::g_client_sock);
        detail::g_client_sock = INVALID_SOCKET;
    }
    if (detail::g_sock != INVALID_SOCKET) {
        closesocket(detail::g_sock);
        detail::g_sock = INVALID_SOCKET;
    }
    if (detail::g_initialized) {
        WSACleanup();
        detail::g_initialized = false;
    }
}

} // namespace net