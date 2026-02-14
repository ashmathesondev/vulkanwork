#include "app.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

int main() {
    App app;
    try {
        app.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
