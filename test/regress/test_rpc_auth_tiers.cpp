// Regression: F8 Part 2 -- every RPC command carries the auth tier that decides its OMQ category.
// Only the three read-only commands are basic (public); everything that mutates state or exposes
// sensitive data is admin. A command that silently drops to basic would be invokable over an
// unprivileged (plain-TCP) connection, so this pins the classification.

#include "test_harness.hpp"

#include <rpc/rpc_request_definitions.hpp>

#include <oxenmq/auth.h>

using namespace srouter::rpc;
using oxenmq::AuthLevel;

REGRESS_CASE(rpc_read_commands_are_basic)
{
    REQUIRE(Version::auth_level == AuthLevel::basic);
    REQUIRE(Status::auth_level == AuthLevel::basic);
    REQUIRE(GetStatus::auth_level == AuthLevel::basic);
}

REGRESS_CASE(rpc_mutating_commands_are_admin)
{
    REQUIRE(Halt::auth_level == AuthLevel::admin);
    REQUIRE(SessionInit::auth_level == AuthLevel::admin);
    REQUIRE(SessionClose::auth_level == AuthLevel::admin);
    REQUIRE(FindCC::auth_level == AuthLevel::admin);
    REQUIRE(MapExit::auth_level == AuthLevel::admin);
    REQUIRE(UnmapExit::auth_level == AuthLevel::admin);
    REQUIRE(SwapExits::auth_level == AuthLevel::admin);
    REQUIRE(QuicConnect::auth_level == AuthLevel::admin);
    REQUIRE(QuicListener::auth_level == AuthLevel::admin);
    REQUIRE(LookupSnode::auth_level == AuthLevel::admin);
    REQUIRE(ListExits::auth_level == AuthLevel::admin);
}
