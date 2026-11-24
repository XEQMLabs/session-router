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
target_include_directories(srouter-regress PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/regress)

# util/zstd.cpp and util/file.cpp live in session-router-utils (always built); its PUBLIC include
# path is src/, so the cases can include "util/..." and "dns/..." headers directly.
target_link_libraries(srouter-regress PRIVATE session-router-utils)

# dns/encode.cpp (extract_name) lives in the full-only session-router-dns library, so the DNS name
# case only builds when that library exists.
if(SROUTER_FULL)
  target_sources(srouter-regress PRIVATE regress/test_dns_name.cpp)
  target_link_libraries(srouter-regress PRIVATE session-router-dns)
endif()

add_custom_target(check COMMAND srouter-regress)
