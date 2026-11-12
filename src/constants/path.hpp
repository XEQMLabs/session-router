#pragma once

#include "util/time.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace srouter::path
{
    /// pad messages to the nearest this many bytes
    inline constexpr std::size_t PAD_SIZE{128};

    /// Number of encryption "frames" inside path builds.  This implicitly defines the maximum
    /// length of a path: shorter path builds still put data in all frames, but frame data beyond
    /// the last hop are random unused data (so that the length of the path build message does not
    /// reveal anything about the total number of hops for the path, and so that the final target
    /// cannot tell how long the path was).
    inline constexpr int BUILD_LENGTH = 8;

    /// A path build is BUILD_LENGTH frames of equal size F, where F may be anywhere from
    /// BUILD_FRAME_SIZE_MIN to BUILD_FRAME_SIZE_MAX.  The format of each frame is determined by its
    /// first byte (after de-onioning) rather than by F, so that one build can mix frame versions:
    ///
    /// - 'd' -- v0: a bt-encoded frame (see path_build_onion) which requires that F be exactly
    ///   BUILD_FRAME_SIZE_V0.
    /// - 0x01 -- v1: a packed frame which requires F >= BUILD_FRAME_SIZE_V1; bytes beyond that are
    ///   ignored.
    inline constexpr size_t BUILD_FRAME_SIZE_V0 = 169;
    inline constexpr size_t BUILD_FRAME_SIZE_V1 = 115;
    inline constexpr size_t BUILD_FRAME_SIZE_MIN = BUILD_FRAME_SIZE_V1;
    inline constexpr size_t BUILD_FRAME_SIZE_MAX = 256;

    /// The frame size we use when building paths.  This has to stay at BUILD_FRAME_SIZE_V0 until
    /// v0-only relays are no longer on the network, as they accept nothing else.
    inline constexpr size_t BUILD_FRAME_SIZE = BUILD_FRAME_SIZE_V0;

    /// Relays with a RC version at least this value understand v1 path build frames.
    inline constexpr std::array<uint8_t, 3> BUILD_FRAME_V1_MIN_VERSION{1, 1, 0};

    /// Max base lifetime of paths.  This is the lifetime of outbound paths, and is the maximum
    /// target lifetime of inbound paths.  Inbound paths also have up some random fuzz added to
    /// this, and so the actual maximum allowed by a relay is be slightly higher than this; see
    /// next two variables.
    inline constexpr std::chrono::seconds MAX_LIFETIME = 20min;

    /// Maximum path expiry randomness: when building paths we add a random value up to this amount
    /// to the path lifetime, and so relays will accept paths of up to MAX_LIFETIME plus this value.
    inline constexpr std::chrono::seconds MAX_LIFETIME_FUZZ = 3min;

    /// The maximum path life accepted by a relay: this is the maximum life plus the maximum amount
    /// of random fuzz.
    inline constexpr std::chrono::seconds MAX_LIFETIME_ACCEPTED = MAX_LIFETIME + MAX_LIFETIME_FUZZ;

    static_assert(
        MAX_LIFETIME_ACCEPTED.count() <= std::numeric_limits<uint16_t>::max(),
        "v1 path build frames encode the path lifetime as a 16-bit value");

    /// The minimum expiry time slots for inbound paths.  See detailed comments in
    /// SessionEndpoint::update_paths().
    inline constexpr auto MAX_LIFETIME_SLOTS = 4;

    static_assert(
        std::chrono::seconds{MAX_LIFETIME} % MAX_LIFETIME_SLOTS == 0s,
        "MAX_LIFETIME_SLOTS must evenly divide MAX_LIFETIME seconds");

    /// How many locations a client contact gets published to.  The contact gets published to the
    /// "closest" [this number] relays, using a metric based on the CC and relay IDs, for short term
    /// redundancy for relays become unreachable or inactive via the Oxen chain.
    ///
    /// (Note that this value cannot be changed without upgrading relays and clients).
    inline constexpr int CC_PUBLISH_LOCATIONS = 4;

    /// after this many ms a path build times out
    inline constexpr auto BUILD_TIMEOUT{10s};

    inline constexpr auto MIN_PATH_BUILD_INTERVAL{500ms};

    inline constexpr auto PATH_BUILD_RATE{100ms};

    /// measure latency every this interval ms
    inline constexpr std::chrono::milliseconds LATENCY_INTERVAL{20s};

    /// if a path is inactive for this amount of time it's dead
    inline constexpr std::chrono::milliseconds ALIVE_TIMEOUT{LATENCY_INTERVAL * 3 / 2};

    /// how big transit hop traffic queues are
    inline constexpr std::size_t TRANSIT_HOP_QUEUE_SIZE{256};

    // F11: relay resource caps.  Each is a hard ceiling on remote-driven growth, not a steady-state
    // target: normal operation stays far below these, and they exist only to bound memory/CPU abuse
    // from a hostile peer.  The three store caps below are the *defaults* for the operator-tunable
    // [router] overrides (max-stored-client-contacts, max-transit-paths,
    // max-inbound-client-connections); a relay reads the configured value, falling back to these.
    // They are deliberately generous and should be re-tuned against real fleet footprint.

    /// Maximum number of distinct blinded client-contact records a relay will store.  At the cap a
    /// relay refuses a new (previously unseen) blinded key rather than evicting a valid entry:
    /// evicting by key proximity is exactly the grind this guards against.
    inline constexpr std::size_t MAX_STORED_CCS{65'536};

    /// Maximum size, in bytes, of a single stored encrypted client-contact record.  A genuine
    /// record is well under this; the cap bounds a single oversized-record abuse.
    inline constexpr std::size_t MAX_CC_RECORD_SIZE{8192};

    /// Maximum number of concurrent transit paths a relay will carry.  _transit_hops holds two
    /// entries (rx + tx) per hop, so the underlying map is bounded at twice this value.
    inline constexpr std::size_t MAX_TRANSIT_HOPS{16'384};

    /// Maximum number of simultaneous inbound client connections a relay will hold open.
    inline constexpr std::size_t MAX_INBOUND_CLIENTS{8192};

    /// F11.3 per-source token bucket in front of path builds: sustained builds/second and the
    /// instantaneous burst ceiling a single source may accumulate.
    inline constexpr double PATH_BUILD_BUCKET_RATE{20.0};
    inline constexpr double PATH_BUILD_BUCKET_BURST{60.0};

    /// Prune the per-source path-build bucket map (drop full/idle buckets) once it grows past this.
    inline constexpr std::size_t PATH_BUILD_BUCKET_PRUNE_AT{1024};

}  // namespace srouter::path
