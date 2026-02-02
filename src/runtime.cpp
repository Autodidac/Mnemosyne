module;

#include <print>

module runtime;

import mylib;
import core.env;
import core.log;
import core.time;
import core.format;

namespace runtime
{
    namespace
    {
        bool smoke_mode()
        {
            if (auto v = core::env::get("DEMO_SMOKE"))
                return *v == "1";
            return false;
        }
    }

    int run()
    {
        core::log::write(core::log::level::info, "runtime", "startup");
        core::log::write(core::log::level::info, "runtime", "exe running");

        const int rc = mylib::entry();
        core::log::write(core::log::level::info, "runtime", core::format::str("mylib::entry() -> {}", rc));

        core::time::frame_clock fc{};
        fc.start();

        const bool smoke = smoke_mode();
        const std::uint64_t max_frames = smoke ? 3u : ~0ull;

        while (fc.frame_index < max_frames)
        {
            fc.tick();
            std::println("running (frame={}, dt_ms={:.3f})",
                         fc.frame_index,
                         fc.dt_seconds() * 1000.0);
            core::time::sleep_ms(1000);
        }

        if (smoke)
            core::log::write(core::log::level::info, "runtime", "smoke complete");

        return 0;
    }
}
