// Regression: F4b/F12 session-init v2 primitives.
//
// The full wire round-trip (sign + seal_box + parse) needs a Router/crypto fixture and is exercised
// on the testnet; here we pin the pure pieces that carry the risk:
//   - the INIT_V2 capability bit that gates the whole v2 handshake (and must not collide with the
//     other advertised protocol flags);
//   - the replay-freshness window arithmetic, i.e. the clock-skew tolerance the design flagged as
//     the main operational risk; and
//   - the replay-id construction that distinguishes a byte-identical replay from a fresh init.

#include "test_harness.hpp"

#include <net/policy.hpp>
#include <session/session.hpp>

#include <string>

using namespace srouter;
using namespace std::chrono_literals;

REGRESS_CASE(init_v2_flag_is_bit6_and_distinct)
{
    REQUIRE(static_cast<uint8_t>(protocol_flag::INIT_V2) == (1u << 6));

    // INIT_V2 is its own bit, not aliased onto any other advertised capability.
    REQUIRE(not has_flag(protocol_flag::PFS_PQ, protocol_flag::INIT_V2));
    REQUIRE(not has_flag(protocol_flag::TCP_TUNNEL, protocol_flag::INIT_V2));
    REQUIRE(not has_flag(protocol_flag::IPV4 | protocol_flag::IPV6, protocol_flag::INIT_V2));

    auto combined = protocol_flag::PFS_PQ | protocol_flag::INIT_V2;
    REQUIRE(has_flag(combined, protocol_flag::INIT_V2));
    REQUIRE(has_flag(combined, protocol_flag::PFS_PQ));  // advertised alongside, not instead of
}

REGRESS_CASE(init_ts_freshness_window)
{
    sys_ms now = time_now_ms();

    REQUIRE(session::init_ts_fresh(now, now));
    REQUIRE(session::init_ts_fresh(now - 59s, now));
    REQUIRE(session::init_ts_fresh(now + 59s, now));                      // symmetric: future skew ok
    REQUIRE(session::init_ts_fresh(now - session::INIT_TS_WINDOW, now));  // boundary is inclusive
    REQUIRE(session::init_ts_fresh(now + session::INIT_TS_WINDOW, now));

    REQUIRE(not session::init_ts_fresh(now - 61s, now));  // just past the window -> stale
    REQUIRE(not session::init_ts_fresh(now + 61s, now));
}

REGRESS_CASE(init_replay_id_distinguishes)
{
    const std::string pkA(RouterID::SIZE, '\x11');
    const std::string pkB(RouterID::SIZE, '\x22');
    const std::string n1(session::INIT_NONCE_SIZE, '\x01');
    const std::string n2(session::INIT_NONCE_SIZE, '\x02');

    auto id1 = session::make_init_replay_id(pkA, n1);

    REQUIRE(id1 == session::make_init_replay_id(pkA, n1));  // deterministic (a true replay matches)
    REQUIRE(id1 != session::make_init_replay_id(pkA, n2));  // a fresh nonce is a different id
    REQUIRE(id1 != session::make_init_replay_id(pkB, n1));  // a different initiator is a different id
    REQUIRE(id1.size() == pkA.size() + n1.size());
}
