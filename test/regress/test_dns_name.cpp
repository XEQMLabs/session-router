// Regression: F6 -- srouter::dns::extract_name must reject malformed encoded names instead of
// reading past the buffer.  Before the fix a length byte > 63 was treated as an ordinary label and
// a name was allowed to grow without bound, which both over-read the input and could later overflow
// a fixed response buffer when the name was re-encoded.

#include "test_harness.hpp"

#include <dns/encode.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

using namespace srouter;

namespace
{
    // Wraps a byte vector as the mutable span extract_name consumes.
    std::optional<std::string> decode(const std::vector<std::byte>& wire)
    {
        std::span<const std::byte> buf{wire};
        return dns::extract_name(buf);
    }

    void push(std::vector<std::byte>& v, unsigned char b) { v.push_back(static_cast<std::byte>(b)); }

    void push_label(std::vector<std::byte>& v, unsigned char len, char fill)
    {
        push(v, len);
        for (unsigned char i = 0; i < len; i++)
            push(v, static_cast<unsigned char>(fill));
    }
}  // namespace

REGRESS_CASE(dns_name_decodes_valid)
{
    // "ab." encoded as a single 2-octet label followed by the zero terminator.
    std::vector<std::byte> wire;
    push_label(wire, 2, 'a');  // but we want distinct bytes; fill 'a' is fine for the length test
    push(wire, 0);
    auto name = decode(wire);
    REQUIRE(name.has_value());
    REQUIRE(*name == "aa.");
}

REGRESS_CASE(dns_name_rejects_oversized_label)
{
    // A length byte of 64 is not a valid label (max 63) and is not a compression pointer in a
    // question name.  It must be rejected, not consumed as data.
    std::vector<std::byte> wire;
    push(wire, 64);
    for (int i = 0; i < 70; i++)  // plenty of trailing bytes so only the length check can fire
        push(wire, 'x');
    push(wire, 0);
    auto name = decode(wire);
    REQUIRE(!name.has_value());
}

REGRESS_CASE(dns_name_rejects_overlong_name)
{
    // Four maximal 63-octet labels decode to 256 characters (with dots), exceeding the RFC 1035
    // 255-octet name limit; extract_name must bail rather than keep appending.
    std::vector<std::byte> wire;
    for (int i = 0; i < 4; i++)
        push_label(wire, 63, 'a');
    push(wire, 0);
    auto name = decode(wire);
    REQUIRE(!name.has_value());
}

REGRESS_CASE(dns_name_rejects_truncated_label)
{
    // Label claims 10 octets but only 3 remain: must not read past the end.
    std::vector<std::byte> wire;
    push(wire, 10);
    push(wire, 'a');
    push(wire, 'b');
    push(wire, 'c');
    auto name = decode(wire);
    REQUIRE(!name.has_value());
}
