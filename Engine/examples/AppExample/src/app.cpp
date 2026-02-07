#include "../include/app_api.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <print>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <sys/select.h>
#  include <termios.h>
#  include <unistd.h>
#endif

inline constexpr std::uint32_t APP_VERSION = 1;

#ifndef _WIN32
namespace
{
    struct TermState
    {
        termios old{};
        bool active = false;
    };

    // Single TU-wide terminal state so shutdown can restore it.
    inline TermState g_term{};
}
#endif

// -----------------------------
// Cross-platform "ESC pressed?"
// -----------------------------
static bool should_quit_impl(void* /*user*/)
{
#ifdef _WIN32
    return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
#else
    // Terminal-based ESC detection (non-blocking).
    // Sets stdin to raw, non-echo mode once, then polls using select().
    if (!g_term.active)
    {
        if (isatty(STDIN_FILENO))
        {
            if (tcgetattr(STDIN_FILENO, &g_term.old) == 0)
            {
                termios raw = g_term.old;
                raw.c_lflag &= static_cast<unsigned>(~(ICANON | ECHO));
                raw.c_cc[VMIN] = 0;  // non-blocking read
                raw.c_cc[VTIME] = 0;
                if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0)
                    g_term.active = true;
            }
        }
    }

    // If not a TTY (piped input), don’t claim ESC.
    if (!g_term.active)
        return false;

    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);

    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    const int ready = select(STDIN_FILENO + 1, &set, nullptr, nullptr, &tv);
    if (ready <= 0 || !FD_ISSET(STDIN_FILENO, &set))
        return false;

    unsigned char ch = 0;
    const auto n = ::read(STDIN_FILENO, &ch, 1);
    return (n == 1 && ch == 27); // ESC
#endif
}

// -----------------------------
// App-side logic
// -----------------------------
static int on_init_impl(void* /*user*/)
{
    std::println("[app] init (press ESC to quit)");
    return 0;
}

static void on_tick_impl(void* /*user*/, std::uint64_t frame, double dt_seconds)
{
    constexpr std::size_t kWindow = 10;
    constexpr std::uint64_t APP_PRINT_EVERY = 10; // print every N frames

    static std::array<double, kWindow> buf{}; // seconds
    static std::size_t idx = 0;
    static std::size_t count = 0;
    static double sum = 0.0;

    if (count == kWindow) sum -= buf[idx];
    else ++count;

    buf[idx] = dt_seconds;
    sum += dt_seconds;
    idx = (idx + 1) % kWindow;

    double min_dt = buf[0];
    double max_dt = buf[0];
    for (std::size_t i = 1; i < count; ++i)
    {
        min_dt = std::min(min_dt, buf[i]);
        max_dt = std::max(max_dt, buf[i]);
    }

    const double avg_dt_ms = (sum / static_cast<double>(count)) * 1000.0;
    const double min_dt_ms = min_dt * 1000.0;
    const double max_dt_ms = max_dt * 1000.0;

    if ((frame % APP_PRINT_EVERY) != 0)
        return;

    std::print(
        "\r\n[ app v{:03} ] frame={:05} avg={:.2f}ms low={:.2f}ms high={:.2f}ms",
        APP_VERSION, frame, avg_dt_ms, min_dt_ms, max_dt_ms
    );
}

static void on_shutdown_impl(void* /*user*/)
{
#ifndef _WIN32
    // Best-effort restore terminal settings (only if we enabled raw mode).
    if (g_term.active)
    {
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &g_term.old);
        g_term.active = false;
    }
#endif
    std::println("\n[app] shutdown");
}

extern "C" const app_callbacks_v1* app_get_callbacks()
{
    static const app_callbacks_v1 cb{
        .version = APP_API_VERSION,
        .on_init = &on_init_impl,
        .on_tick = &on_tick_impl,
        .should_quit = &should_quit_impl,
        .on_shutdown = &on_shutdown_impl,
    };
    return &cb;
}
