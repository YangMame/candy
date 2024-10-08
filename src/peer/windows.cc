// SPDX-License-Identifier: MIT
#include <Poco/Platform.h>

#if POCO_OS == POCO_OS_WINDOWS_NT

#include "peer/peer.h"
#include "utility/address.h"
#include <spdlog/spdlog.h>
#include <string.h>
// clang-format off
#include <winsock2.h>
#include <mstcpip.h>
// clang-format on

#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)

namespace Candy {

UdpHolder::UdpHolder() {
    this->socket = INVALID_SOCKET;
    return;
}

UdpHolder::~UdpHolder() {
    return;
}

int UdpHolder::init() {
    this->socket = INVALID_SOCKET;
    SOCKET winsock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (winsock == INVALID_SOCKET) {
        spdlog::error("create udp socket failed: {}", WSAGetLastError());
        return -1;
    }
    // https://stackoverflow.com/a/42409198
    BOOL bNewBehavior = FALSE;
    DWORD dwBytesReturned = 0;
    WSAIoctl(winsock, SIO_UDP_CONNRESET, &bNewBehavior, sizeof bNewBehavior, NULL, 0, &dwBytesReturned, NULL, NULL);
    // set non-blocking
    u_long mode = 1;
    if (ioctlsocket(winsock, FIONBIO, &mode) != NO_ERROR) {
        closesocket(winsock);
        spdlog::error("set non-blocking failed: {}", WSAGetLastError());
        return -1;
    }
    struct sockaddr_in local_addr;
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(this->port);
    if (bind(winsock, (struct sockaddr *)&local_addr, sizeof(local_addr)) == SOCKET_ERROR) {
        closesocket(winsock);
        spdlog::error("socket binding failed: {}", WSAGetLastError());
        return -1;
    }
    this->socket = winsock;
    return 0;
}

void UdpHolder::reset() {
    SOCKET winsock = std::any_cast<SOCKET>(this->socket);
    if (winsock != INVALID_SOCKET) {
        closesocket(winsock);
        this->socket = INVALID_SOCKET;
    }
    this->port = 0;
    this->ip = 0;
}

uint16_t UdpHolder::Port() {
    if (this->port) {
        return this->port;
    }

    SOCKET winsock = std::any_cast<SOCKET>(this->socket);
    if (winsock != INVALID_SOCKET) {
        struct sockaddr_in local;
        int len = sizeof(local);
        memset(&local, 0, sizeof(local));
        if (getsockname(winsock, (struct sockaddr *)&local, &len) != SOCKET_ERROR) {
            this->port = ntohs(local.sin_port);
            return this->port;
        }
    }
    return 0;
}

size_t UdpHolder::read(UdpMessage &message) {
    SOCKET winsock = std::any_cast<SOCKET>(this->socket);
    if (winsock == INVALID_SOCKET) {
        spdlog::error("udp socket read failed: uninitialized");
        return -1;
    }
    char buffer[1500] = {0};
    struct sockaddr_in from;
    int addr_len = sizeof(from);
    memset(&from, 0, sizeof(from));

    int len = recvfrom(winsock, buffer, sizeof(buffer), 0, (struct sockaddr *)&from, &addr_len);
    if (len != SOCKET_ERROR) {
        message.buffer.assign(buffer, len);
        message.ip = Address::netToHost((uint32_t)from.sin_addr.s_addr);
        message.port = Address::netToHost((uint16_t)from.sin_port);
        return len;
    }
    int error = WSAGetLastError();
    if (error == WSAEWOULDBLOCK) {
        struct timeval timeout = {.tv_sec = 1};
        fd_set set;
        FD_ZERO(&set);
        FD_SET(winsock, &set);
        select(0, &set, NULL, NULL, &timeout);
        return 0;
    }
    spdlog::error("udp socket read failed: {}", error);
    return -1;
}

size_t UdpHolder::write(const UdpMessage &message) {
    if (message.buffer.empty()) {
        spdlog::debug("udp socket write failed: empty message");
        return -1;
    }

    SOCKET winsock = std::any_cast<SOCKET>(this->socket);
    if (winsock == INVALID_SOCKET) {
        spdlog::debug("udp socket write failed: uninitialized");
        return -1;
    }

    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = Address::hostToNet(message.ip);
    to.sin_port = Address::hostToNet(message.port);
    int len = sendto(winsock, message.buffer.c_str(), message.buffer.length(), 0, (struct sockaddr *)&to, sizeof(to));
    if (len != SOCKET_ERROR) {
        return len;
    }
    int error = WSAGetLastError();
    if (error == WSAEWOULDBLOCK) {
        return 0;
    }
    spdlog::debug("udp socket write failed: {}", error);
    return -1;
}

} // namespace Candy

#endif
