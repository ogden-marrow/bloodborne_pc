// bbport: GPU-side assertion failures stop the port with exit code 23.
#include <cstdio>
#include <execinfo.h>
#include <cstdlib>
#include <stdexcept>
#include "common/assert.h"
#include "common/logging/log.h"
#include <unistd.h>

// bb-probe (probe.c): set while the port restarts itself through run.sh. The device fd is closed
// before exec; Vulkan calls failing then are not errors: this thread waits for the exec instead.
extern "C" __attribute__((weak)) volatile int runtime_restarting; // absent in the tests

void assert_fail_impl() {
    if (&runtime_restarting && runtime_restarting) {
        for (;;) {
            pause();
        }
    }
    std::fflush(stdout);
    // bbport: say where it failed (the call stack of the asserting thread) and flush every open log
    // (BB_MEM_TRACE, BB_FRAME_LOG): a crash names its own site, no debugger needed.
    {
        void* frames[40];
        const int count = backtrace(frames, 40);
        std::fputs("STOP: assertion call stack:\n", stderr);
        backtrace_symbols_fd(frames, count, STDERR_FILENO);
    }
    std::fflush(nullptr);
    std::fputs("STOP: GPU library assertion failed (see GPU log above)\n", stderr);
    std::_Exit(23);
}

[[noreturn]] void unreachable_impl() {
    assert_fail_impl();
    throw std::runtime_error("Unreachable code");
}

void assert_fail_debug_msg(const char* msg) {
    LOG_CRITICAL(Debug, "Assertion Failed!\n{}", msg);
    assert_fail_impl();
}
