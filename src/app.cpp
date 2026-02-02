#include "../include/app_api.h"

#include <print>

// App-side logic (can dispatch to other files/modules)
static int on_init_impl()
{
    std::println("[app] init");
    return 0;
}

static void on_tick_impl(double dt_seconds)
{
    // Fully type-safe, compile-time checked
    std::println("[app] tick dt={:.6f}", dt_seconds);
}

static void on_shutdown_impl()
{
    std::println("[app] shutdown");
}

extern "C" const app_callbacks_v1* app_get_callbacks()
{
    static const app_callbacks_v1 cb{
        .version = APP_API_VERSION,
        .on_init = &on_init_impl,
        .on_tick = &on_tick_impl,
        .on_shutdown = &on_shutdown_impl,
    };
    return &cb;
}
