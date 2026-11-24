// Regression: F14 -- srouter::util::buffer_to_private_file must create the file with 0600
// permissions and must not write through a pre-existing symlink.  Key material is written with this
// helper, so a world-readable key file or a write redirected through an attacker-planted symlink
// would be a disclosure.

#include "test_harness.hpp"

#include <util/file.hpp>

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace srouter;
namespace fs = std::filesystem;

namespace
{
    // Returns the permission bits (mode & 0777) of a path, following no symlinks.
    unsigned mode_of(const fs::path& p)
    {
        struct stat st
        {
        };
        if (::lstat(p.c_str(), &st) != 0)
            throw regress::Failure{"lstat failed on " + p.string()};
        return st.st_mode & 0777;
    }

    fs::path unique_path(std::string_view tag)
    {
        return fs::temp_directory_path()
            / ("srouter-regress-" + std::string{tag} + "-" + std::to_string(::getpid()) + "-"
               + std::to_string(std::rand()));
    }
}  // namespace

REGRESS_CASE(private_file_is_0600)
{
    auto path = unique_path("perms");
    util::buffer_to_private_file(path, "secret-key-material");

    REQUIRE(fs::exists(path));
    REQUIRE(fs::is_regular_file(fs::symlink_status(path)));  // a real file, not a symlink
    REQUIRE(mode_of(path) == 0600);
    REQUIRE(util::file_to_string(path) == "secret-key-material");

    fs::remove(path);
}

REGRESS_CASE(private_file_overwrites_atomically)
{
    auto path = unique_path("overwrite");
    util::buffer_to_private_file(path, "first");
    util::buffer_to_private_file(path, "second");

    REQUIRE(util::file_to_string(path) == "second");
    REQUIRE(mode_of(path) == 0600);

    fs::remove(path);
}

REGRESS_CASE(private_file_does_not_follow_symlink)
{
    // Plant a symlink at the target pointing at a decoy; the helper must replace the symlink with a
    // real private file and must NOT write the secret through to the decoy.
    auto decoy = unique_path("decoy");
    auto link = unique_path("link");
    util::buffer_to_file(decoy, "innocent");
    fs::create_symlink(decoy, link);

    util::buffer_to_private_file(link, "secret-through-symlink");

    REQUIRE(fs::is_regular_file(fs::symlink_status(link)));  // link is now a real file
    REQUIRE(util::file_to_string(link) == "secret-through-symlink");
    REQUIRE(util::file_to_string(decoy) == "innocent");  // decoy untouched
    REQUIRE(mode_of(link) == 0600);

    fs::remove(link);
    fs::remove(decoy);
}
