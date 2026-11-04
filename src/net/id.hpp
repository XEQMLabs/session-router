#pragma once
#include <fmt/format.h>

#include <string>

namespace srouter
{

    enum class NetID
    {
        MAINNET = 0,
        TESTNET = 1,
        XEQM = 2
    };

    inline std::string to_string(NetID n)
    {
        switch (n)
        {
            case NetID::MAINNET:
                return "session-router";
            case NetID::TESTNET:
                return "testnet";
            case NetID::XEQM:
                return "xeqm";
            default:
                return fmt::format("unknown network {}", static_cast<int>(n));
        }
    }

    inline NetID netid_from_string(std::string_view s)
    {
        if (s == "session-router")
            return NetID::MAINNET;
        if (s == "testnet")
            return NetID::TESTNET;
        if (s == "xeqm")
            return NetID::XEQM;
        throw std::invalid_argument{"Invalid network id"};
    }

}  // namespace srouter

namespace fmt
{
    template <>
    struct formatter<srouter::NetID, char> : formatter<std::string>
    {
        template <typename FormatContext>
        auto format(srouter::NetID n, FormatContext& ctx) const
        {
            return formatter<std::string>::format(to_string(n), ctx);
        }
    };

}  // namespace fmt
