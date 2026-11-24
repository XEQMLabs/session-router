#pragma once

#include "util/time.hpp"

#include <algorithm>
#include <chrono>

namespace srouter::util
{
    // A simple token-bucket rate limiter.
    //
    // The bucket starts full and refills at `rate` tokens per second up to a ceiling of `burst`
    // tokens.  Each allowed request spends one token; when the bucket is empty the request is
    // refused.  This bounds both the sustained rate (to `rate`) and the instantaneous burst (to
    // `burst`) that a single source can drive, which is what we want in front of the expensive,
    // remotely-triggerable relay handlers (F11.3).
    //
    // NOT thread-safe: an instance must only be used from a single thread (in practice the router
    // event loop, where the rate-limited handlers run).
    class TokenBucket
    {
        double _tokens;
        double _rate;   // tokens added per second
        double _burst;  // maximum accumulated tokens
        sys_ms _last;

        double _available(sys_ms now) const
        {
            if (now <= _last)
                return _tokens;
            double elapsed = std::chrono::duration<double>{now - _last}.count();
            return std::min(_burst, _tokens + elapsed * _rate);
        }

      public:
        TokenBucket(double rate, double burst, sys_ms now) : _tokens{burst}, _rate{rate}, _burst{burst}, _last{now} {}

        // Refills for the time elapsed since the last call and tries to spend `cost` tokens.
        // Returns true if there were enough tokens (request allowed) and spends them; returns false
        // and spends nothing if the bucket is short (request should be dropped).
        bool try_spend(sys_ms now, double cost = 1.0)
        {
            _tokens = _available(now);
            _last = now;
            if (_tokens >= cost)
            {
                _tokens -= cost;
                return true;
            }
            return false;
        }

        // True if the bucket would be at full capacity at `now`.  A full bucket carries no state
        // worth keeping, so callers prune full entries to bound the size of a per-source bucket map.
        bool full(sys_ms now) const { return _available(now) >= _burst; }
    };
}  // namespace srouter::util
