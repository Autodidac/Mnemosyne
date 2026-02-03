module;

#include <chrono>
#include <cstdint>

export module core.time;

export namespace core::time
{
    using steady = std::chrono::steady_clock;

    // Monotonic timestamp in nanoseconds since an arbitrary epoch.
    std::uint64_t now_ns() noexcept;

    // Convenience sleep.
    void sleep_ms(std::uint32_t ms);

    struct frame_clock
    {
        std::uint64_t frame_index = 0;
        std::uint64_t last_ns = 0;
        std::uint64_t dt_ns = 0;

        void start() noexcept;
        void tick() noexcept;

        double dt_seconds() const noexcept
        {
            return static_cast<double>(dt_ns) * 1e-9;
        }
    };
}
