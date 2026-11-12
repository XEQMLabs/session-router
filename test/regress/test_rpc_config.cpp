// Regression: F8 Part 1 -- the control RPC must default to a local IPC socket, not a
// network-reachable TCP port, and must not silently enable insecure options. Before the fix the
// default bind list included tcp://127.0.0.1:1190.

#include "test_harness.hpp"

#include <config/config.hpp>

#include <string>

using namespace srouter;

namespace
{
    bool has_prefix(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
}  // namespace

REGRESS_CASE(rpc_default_bind_is_ipc_only_on_posix)
{
    Config cfg{config::Type::FullClient, std::string{}};

    bool any_tcp = false, any_ipc = false;
    for (const auto& a : cfg.api.rpc_bind_addrs)
    {
        any_tcp = any_tcp or has_prefix(a, "tcp://");
        any_ipc = any_ipc or has_prefix(a, "ipc://");
    }

#ifndef _WIN32
    REQUIRE(not any_tcp);  // no network-reachable default on a platform that has unix sockets
    REQUIRE(any_ipc);
#else
    REQUIRE(any_tcp);  // Windows has no IPC, so loopback TCP is the only default
#endif
}

REGRESS_CASE(rpc_insecure_options_default_off)
{
    Config cfg{config::Type::FullClient, std::string{}};
    REQUIRE(not cfg.api.allow_insecure_rpc);
    REQUIRE(cfg.api.rpc_max_request_size > 0);  // the control surface is bounded by default
}
