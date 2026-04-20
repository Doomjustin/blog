#ifndef BLOG_IP_V4_H
#define BLOG_IP_V4_H

#include <netinet/in.h>

#include "address.h"
#include "internet_protocol.h"
#include "option.h"
#include "protocol.h"

namespace ip {

struct v4 {
private:
    static constexpr auto domain_ = domain::ipv4;
    static constexpr auto stream_ = type::stream;
    static constexpr auto datagram_ = type::datagram;
    static constexpr auto tcp_ = protocol::tcp;
    static constexpr auto udp_ = protocol::udp;
    
public:
    v4() = delete;

    using broadcast = BooleanOption<SOL_SOCKET, SO_BROADCAST>;
    using type_of_service = ValueOption<IPPROTO_IP, IP_TOS>;
    using receive_destination_address = BooleanOption<IPPROTO_IP, IP_PKTINFO>;

    using address = AddressV4;
    using tcp = InternetProtocol<domain_, stream_, tcp_, true>;
    using udp = InternetProtocol<domain_, datagram_, udp_, false>;    
};

} // namespace ip

#endif // BLOG_IP_V4_H