#pragma once
#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

    // Bump when the ABI layout changes.
    enum { APP_API_VERSION = 1 };

    struct app_callbacks_v1
    {
        std::uint32_t version; // must be APP_API_VERSION

        // Return 0 for success; non-zero to abort startup.
        int  (*on_init)();

        // Called once per frame. dt_seconds is monotonic delta.
        void (*on_tick)(double dt_seconds);

        // Called exactly once at shutdown.
        void (*on_shutdown)();
    };

    // Implemented by the EXE (app project). May return nullptr.
    const struct app_callbacks_v1* app_get_callbacks();

#ifdef __cplusplus
}
#endif
