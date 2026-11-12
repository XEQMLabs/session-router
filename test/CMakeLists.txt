# Security regression suite.
#
# The original llarp-era unit tests were written against the pre-rewrite lokinet/llarp codebase
# (<llarp/...> headers, a `lokinet-amalgum` target) and no longer built after the rewrite into the
# srouter:: namespace; they have been removed.  This target builds the current srouter:: regression
# cases instead, which link the real component libraries and are meant to run under
# AddressSanitizer/UBSan.

add_executable(srouter-regress
  regress/main.cpp
  regress/test_zstd.cpp
  regress/test_file_perms.cpp
)
# The regress sources plus src/ for the component headers ("util/...", "config/...", "rpc/...").
# src/ is a component library's PUBLIC include, but the full aggregate below does not re-export it,
# so add it directly.
target_include_directories(srouter-regress PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/regress
  ${PROJECT_SOURCE_DIR}/src)

# The component libraries' PUBLIC include path is src/, so the cases can include "util/...",
# "dns/...", "config/..." and "rpc/..." headers directly.
#
# dns/encode (F6), Config defaults (F8 part 1) and the RPC command auth tiers (F8 part 2) live in
# the full-only libraries. Under a full build we link the full aggregate (exactly as the daemon
# does) so the components' transitive dependencies and static-link ordering are resolved for us; it
# also contains util/zstd and util/file for the F13/F14 cases. Under a non-full build only
# session-router-utils is available, which is all the F13/F14 cases need.
if(SROUTER_FULL)
  target_sources(srouter-regress PRIVATE
    regress/test_dns_name.cpp
    regress/test_rpc_config.cpp
    regress/test_rpc_auth_tiers.cpp
    regress/test_session_init_v2.cpp)
  # oxenmq::oxenmq for its interface include dirs: the rpc request headers include <oxenmq/...>,
  # which the component libraries pull in only privately.
  target_link_libraries(srouter-regress PRIVATE session-router::full oxenmq::oxenmq)

  # Force the whole session-router-utils archive in. Inlined sodium_cleared destructors emitted
  # from the test objects reference srouter::set_zero (util/cleared.cpp), but on-demand extraction
  # does not pull that bitcode member from the archive at the final LTO link, leaving it undefined.
  # A raw --whole-archive flag on the archive path avoids the CMake WHOLE_ARCHIVE link-feature
  # conflict with the same library arriving transitively as a plain dependency.
  target_link_options(srouter-regress PRIVATE
    "LINKER:--whole-archive" "$<TARGET_FILE:session-router-utils>" "LINKER:--no-whole-archive")
else()
  target_link_libraries(srouter-regress PRIVATE session-router-utils)
endif()

# Match the daemon: enable LTO so the test translation units join the same LTO as the (bitcode)
# component archives. Without it, a reference emitted from a non-LTO test object (e.g. an inlined
# sodium_cleared destructor calling util::set_zero) fails to resolve against the LTO archives.
enable_lto(srouter-regress)

add_custom_target(check COMMAND srouter-regress)
