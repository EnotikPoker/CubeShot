#include "network/Address.h"

namespace cubeshot::network
{
bool validAddress(const std::string& address)
{
    // LAN v1: только IPv4 и необязательный порт. Никакого DNS/IPv6 в форме.
    const auto colon = address.find(':');
    if (colon != std::string::npos && address.find(':', colon + 1) != std::string::npos)
        return false;
    const std::string ip = address.substr(0, colon);
    unsigned octets = 0, value = 0, digits = 0;
    for (char c : ip + '.')
    {
        if (c == '.')
        {
            if (!digits || value > 255)
                return false;
            ++octets;
            value = digits = 0;
        }
        else if (c >= '0' && c <= '9' && digits < 3)
        {
            value = value * 10 + c - '0';
            ++digits;
        }
        else
            return false;
    }
    if (octets != 4)
        return false;
    if (colon != std::string::npos)
    {
        auto port = address.substr(colon + 1);
        if (port.empty() || port.size() > 5)
            return false;
        unsigned n = 0;
        for (char c : port)
        {
            if (c < '0' || c > '9')
                return false;
            n = n * 10 + c - '0';
        }
        if (!n || n > 65535)
            return false;
    }
    return true;
}

} // namespace cubeshot::network
