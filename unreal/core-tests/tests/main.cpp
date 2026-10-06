// Runs every TEST (or those whose name contains argv[1]) and prints a summary.
#include "test.h"

#include <cstring>

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int64_t ran = 0, failed = 0;
    for (const auto& c : test::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        test::counts().failuresInTest = 0;
        std::printf("%s\n", c.name);
        c.run();
        ran += 1;
        if (test::counts().failuresInTest > 0) failed += 1;
    }
    std::printf("\n%lld tests, %lld expectations, %lld failures in %lld tests: %s\n", static_cast<long long>(ran),
                static_cast<long long>(test::counts().expectations), static_cast<long long>(test::counts().failures),
                static_cast<long long>(failed), failed == 0 ? "PASS" : "FAIL");
    return failed == 0 ? 0 : 1;
}
