// Regression: F13 -- srouter::zstd::decompressor::decompress must return nullopt on a truncated
// frame instead of looping forever.  Before the fix a frame that still wanted input while the input
// was exhausted and no output was produced spun the decode loop indefinitely, which a malicious
// bootstrap blob could exploit as a hang.  The mere fact that this case returns proves the loop
// terminates.

#include "test_harness.hpp"

#include <util/zstd.hpp>

#include <cstddef>
#include <string>
#include <vector>

using namespace srouter;

namespace
{
    std::string to_string(const std::vector<std::byte>& v)
    {
        return std::string{reinterpret_cast<const char*>(v.data()), v.size()};
    }
}  // namespace

REGRESS_CASE(zstd_roundtrips)
{
    const std::string original = "the quick brown fox jumps over the lazy dog, repeatedly. "
                                 "the quick brown fox jumps over the lazy dog, repeatedly.";
    zstd::compressor c;
    auto compressed = c.compress(original);
    REQUIRE(!compressed.empty());

    zstd::decompressor d;
    auto back = d.decompress(compressed);
    REQUIRE(back.has_value());
    REQUIRE(to_string(*back) == original);
}

REGRESS_CASE(zstd_truncated_frame_returns_not_hangs)
{
    const std::string original = "compressible compressible compressible compressible compressible "
                                 "compressible compressible compressible compressible compressible";
    zstd::compressor c;
    auto compressed = c.compress(original);
    REQUIRE(compressed.size() > 4);

    // Chop the frame in half so the decoder is left wanting more bytes that never arrive.
    std::vector<std::byte> truncated{compressed.begin(), compressed.begin() + compressed.size() / 2};

    zstd::decompressor d;
    auto back = d.decompress(truncated);
    REQUIRE(!back.has_value());
}

REGRESS_CASE(zstd_respects_max_size)
{
    const std::string original(4096, 'a');
    zstd::compressor c;
    auto compressed = c.compress(original);

    zstd::decompressor d;
    // Cap well below the real decompressed size: must refuse rather than allocate past the bound.
    auto back = d.decompress(compressed, 16);
    REQUIRE(!back.has_value());
}
