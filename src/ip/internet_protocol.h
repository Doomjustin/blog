#ifndef BLOG_IP_INTERNET_PROTOCOL_H
#define BLOG_IP_INTERNET_PROTOCOL_H

#include "endpoint.h"
#include "protocol.h"

namespace ip {

template<int Domain, int Type, int Protocol, bool IsConnectionOriented>
struct InternetProtocol : BasicProtocol<Domain, Type, Protocol, IsConnectionOriented> {
    using protocol_type = InternetProtocol<Domain, Type, Protocol, IsConnectionOriented>;

    using endpoint = BasicEndpoint<protocol_type>;
    using port = typename endpoint::port_type;
    using address = typename endpoint::address_type;

};

template<int Domain, int Type, int Protocol>
struct InternetProtocol<Domain, Type, Protocol, true> : BasicProtocol<Domain, Type, Protocol, true> {
    using protocol_type = InternetProtocol<Domain, Type, Protocol, true>;

    using endpoint = BasicEndpoint<protocol_type>;
    using port = typename endpoint::port_type;
    using address = typename endpoint::address_type;
};

template<int Domain, int Type, int Protocol>
struct InternetProtocol<Domain, Type, Protocol, false>: BasicProtocol<Domain, Type, Protocol, false> {
    using protocol_type = InternetProtocol<Domain, Type, Protocol, false>;

    using endpoint = BasicEndpoint<protocol_type>;
    using port = typename endpoint::port_type;
    using address = typename endpoint::address_type;
};

} // namespace ip

#endif // BLOG_IP_INTERNET_PROTOCOL_H