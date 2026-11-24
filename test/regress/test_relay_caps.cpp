// Regression: F11 relay resource caps.
//
// The handler-level enforcement (ALPN gating in register_commands, the ContactDB / transit / inbound
// caps, the is_transit_allowed() gate, the bootstrap-blob cache) needs a live Router + crypto fixture
// and is exercised on the testnet.  Here we pin the pure pieces that carry the arithmetic risk:
//   - the per-source token bucket (F11.3): it must start full, refuse once drained, and refill at the
//     configured rate without exceeding the burst ceiling; and
//   - the cap constants (F11.2) are present and self-consistent (e.g. the transit-hop map, which
//     stores two entries per path, stays bounded by twice MAX_TRANSIT_HOPS).

#include "test_harness.hpp"

#include <constants/path.hpp>
#include <util/time.hpp>
#include <util/token_bucket.hpp>

using namespace srouter;
using namespace std::chrono_literals;

REGRESS_CASE(token_bucket_starts_full_and_drains)
{
    const sys_ms t0{};
    util::TokenBucket b{10.0, 5.0, t0};  // 10/s refill, burst 5

    REQUIRE(b.full(t0));

    // Five tokens available at rest; the sixth immediate spend fails.
    for (int i = 0; i < 5; i++)
        REQUIRE(b.try_spend(t0));
    REQUIRE(not b.try_spend(t0));
    REQUIRE(not b.full(t0));
}

REGRESS_CASE(token_bucket_refills_at_rate)
{
    const sys_ms t0{};
    util::TokenBucket b{10.0, 5.0, t0};  // 10/s refill, burst 5

    for (int i = 0; i < 5; i++)
        REQUIRE(b.try_spend(t0));
    REQUIRE(not b.try_spend(t0));

    // 100ms at 10/s == exactly one token back.
    REQUIRE(b.try_spend(t0 + 100ms));
    REQUIRE(not b.try_spend(t0 + 100ms));

    // One full second refills the whole burst but never beyond it.
    for (int i = 0; i < 5; i++)
        REQUIRE(b.try_spend(t0 + 1100ms));
    REQUIRE(not b.try_spend(t0 + 1100ms));
    REQUIRE(b.full(t0 + 5s));  // long idle: back to capacity, not above
}

REGRESS_CASE(relay_caps_constants_consistent)
{
    REQUIRE(path::MAX_STORED_CCS > 0);
    REQUIRE(path::MAX_CC_RECORD_SIZE > 0);
    REQUIRE(path::MAX_TRANSIT_HOPS > 0);
    REQUIRE(path::MAX_INBOUND_CLIENTS > 0);

    // _transit_hops stores an rx and a tx entry per path, so the underlying map is bounded by twice
    // the path cap; the multiplication must not overflow size_t.
    REQUIRE(2 * path::MAX_TRANSIT_HOPS > path::MAX_TRANSIT_HOPS);

    // Burst must be positive and the refill rate non-negative, or the bucket can never admit a
    // request.
    REQUIRE(path::PATH_BUILD_BUCKET_BURST >= 1.0);
    REQUIRE(path::PATH_BUILD_BUCKET_RATE > 0.0);
}
