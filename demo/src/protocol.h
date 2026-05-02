#ifndef BLOG_PROTOCOL_H
#define BLOG_PROTOCOL_H

#include <concepts>

#include <netinet/in.h>
#include <sys/socket.h>

/** @brief Tag constant passed to `BasicProtocol` for connection-oriented protocols (e.g. TCP). */
constexpr bool connection_oriented{ true };
/** @brief Tag constant passed to `BasicProtocol` for connectionless protocols (e.g. UDP). */
constexpr bool connectionless{ false };

/**
 * @brief Compile-time protocol descriptor carrying OS socket parameters.
 *
 * Used as a policy type for socket and acceptor templates. Keeps address
 * family, socket type, and protocol number in one place so changing a
 * protocol only requires updating this struct.
 *
 * @tparam Domain              Address family (e.g. `AF_INET6`).
 * @tparam Type                Socket type (e.g. `SOCK_STREAM`).
 * @tparam Protocol            IP protocol (e.g. `IPPROTO_TCP`).
 * @tparam IsConnectionOriented Whether the protocol is connection-oriented.
 */
template<int Domain, int Type, int Protocol, bool IsConnectionOriented>
struct BasicProtocol {
    static constexpr int domain = Domain;
    static constexpr int type = Type;
    static constexpr int protocol = Protocol;
    static constexpr bool is_connection_oriented = IsConnectionOriented;
};

/** @brief Constrain (Protocol, Endpoint) pairs where the endpoint's `protocol_type` is exactly `Protocol`. */
template<typename Protocol, typename Endpoint>
concept is_same_protocol = std::same_as<Protocol, typename Endpoint::protocol_type>; 

/** @brief Constrain protocols that require a connected socket before I/O. */
template<typename T>
concept connection_oriented_protocol = T::is_connection_oriented;


/**
 * @brief Named constants for common socket address families.
 *
 * Used as the `Domain` argument when constructing protocol descriptors or
 * filtering endpoints by address family at compile time.
 */
struct domain {
    domain() = delete;
    
    static constexpr int unspecified = AF_UNSPEC;
    static constexpr int ipv4 = AF_INET;
    static constexpr int ipv6 = AF_INET6;
    static constexpr int unix = AF_UNIX;
    static constexpr int packet = AF_PACKET;
};

/**
 * @brief Named constants for common socket types.
 *
 * Used as the `Type` argument when constructing protocol descriptors.
 */
struct type {
    type() = delete;

    static constexpr int unspecified = 0;
    static constexpr int stream = SOCK_STREAM;
    static constexpr int datagram = SOCK_DGRAM;
    static constexpr int sequence_packet = SOCK_SEQPACKET;
};

/**
 * @brief Named constants for common IP protocols.
 *
 * Used as the `Protocol` argument when constructing protocol descriptors.
 * `sctp` is conditionally available depending on kernel support.
 */
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