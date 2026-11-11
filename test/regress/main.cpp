// Runner for the security regression suite.  Builds as the `srouter-regress` executable and is
// invoked by the `check` target.  Prints one line per case and a final summary; exit status is the
// number of failed cases (0 = all passed) so CI and the shell can gate on it.

#include "test_harness.hpp"

#include <cstdio>

int main()
{
    int failed = 0;
    auto& cases = regress::registry();
    std::printf("running %zu regression case(s)\n", cases.size());
    for (const auto& c : cases)
    {
        try
        {
            c.fn();
            std::printf("  ok    %s\n", c.name.c_str());
        }
        catch (const regress::Failure& f)
        {
            failed++;
            std::printf("  FAIL  %s\n        %s\n", c.name.c_str(), f.what());
        }
        catch (const std::exception& e)
        {
            failed++;
            std::printf("  ERROR %s\n        unexpected exception: %s\n", c.name.c_str(), e.what());
        }
    }
    std::printf("%d/%zu passed\n", static_cast<int>(cases.size()) - failed, cases.size());
    return failed;
}
