module;

#include <chrono>
#include <cstdint>
#include <thread>

module core.time;

namespace core::time
{
    std::uint64_t now_ns() noexcept
    {
        const auto t = steady::now().time_since_epoch();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t).count()
        );
    }

    void sleep_ms(std::uint32_t ms)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    void frame_clock::start() noexcept
    {
        frame_index = 0;
        last_ns = now_ns();
        dt_ns = 0;
    }

    void frame_clock::tick() noexcept
    {
        const std::uint64_t n = now_ns();
        dt_ns = (n - last_ns);
        last_ns = n;
        ++frame_index;
    }
}
