#include "check.h"

int main() {
    int ran = 0;
    for (const auto& c : check::registry()) {
        int before = check::failures();
        c.fn();
        ++ran;
        std::printf("%s %s\n", check::failures() == before ? "ok  " : "FAIL", c.name);
    }
    std::printf("\n%d tests, %d failed checks\n", ran, check::failures());
    return check::failures() == 0 ? 0 : 1;
}
