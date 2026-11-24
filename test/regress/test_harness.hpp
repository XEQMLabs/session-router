#pragma once

// A tiny, dependency-free test harness for the security regression cases.  The original llarp-era
// suite linked Catch2 and a `lokinet-amalgum` target that no longer exist, so these cases bring
// their own minimal runner instead.  Each case self-registers at static-init time; `main` (in
// main.cpp) walks the registry, runs every case, and returns non-zero if any REQUIRE fails.  The
// point is to run clean under AddressSanitizer/UBSan, so the harness itself allocates nothing
// exotic and throws a plain exception on failure.

#include <exception>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace regress
{
    struct Case
    {
        std::string name;
        std::function<void()> fn;
    };

    inline std::vector<Case>& registry()
    {
        static std::vector<Case> cases;
        return cases;
    }

    struct Registrar
    {
        Registrar(std::string name, std::function<void()> fn)
        {
            registry().push_back({std::move(name), std::move(fn)});
        }
    };

    struct Failure : std::exception
    {
        std::string message;
        explicit Failure(std::string m) : message{std::move(m)} {}
        const char* what() const noexcept override { return message.c_str(); }
    };

    inline void check(bool cond, std::string_view expr, const char* file, int line)
    {
        if (!cond)
            throw Failure{std::string{file} + ":" + std::to_string(line) + ": REQUIRE(" + std::string{expr} + ")"};
    }
}  // namespace regress

// Register a test case.  Usage:
//     REGRESS_CASE(dns_name_rejects_oversized_label) { ... REQUIRE(cond); ... }
#define REGRESS_CASE(name)                                                      \
    static void regress_fn_##name();                                           \
    static const ::regress::Registrar regress_reg_##name{#name, regress_fn_##name}; \
    static void regress_fn_##name()

#define REQUIRE(cond) ::regress::check((cond), #cond, __FILE__, __LINE__)
