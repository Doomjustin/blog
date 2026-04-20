#ifndef BLOG_PROTOCOL_H
#define BLOG_PROTOCOL_H

#include <concepts>

#include <netinet/in.h>
#include <sys/socket.h>

constexpr bool connection_oriented{ true };
constexpr bool connectionless{ false };

template<int Domain, int Type, int Protocol, bool IsConnectionOriented>
struct BasicProtocol {
    static constexpr int domain = Domain;
    static constexpr int type = Type;
    static constexpr int protocol = Protocol;
    static constexpr bool is_connection_oriented = IsConnectionOriented;
};

template<typename Protocol, typename Endpoint>
concept is_same_protocol = std::same_as<Protocol, typename Endpoint::protocol_type>; 

template<typename T>
concept connection_oriented_protocol = T::is_connection_oriented;


struct domain {
    domain() = delete;
    
    static constexpr int unspecified = AF_UNSPEC;
    static constexpr int ipv4 = AF_INET;
    static constexpr int ipv6 = AF_INET6;
    static constexpr int unix = AF_UNIX;
    static constexpr int packet = AF_PACKET;
};

struct type {
    type() = delete;

    static constexpr int unspecified = 0;
    static constexpr int stream = SOCK_STREAM;
    static constexpr int datagram = SOCK_DGRAM;
    static constexpr int sequence_packet = SOCK_SEQPACKET;
};

struct protocol {
    protocol() = delete;

    static constexpr int unspecified = 0;
    static constexpr int tcp = IPPROTO_TCP;
    static constexpr int udp = IPPROTO_UDP;

#ifdef IPPROTO_SCTP
    static constexpr int sctp = IPPROTO_SCTP;
#endif
};

#endif // BLOG_PROTOCOL_H