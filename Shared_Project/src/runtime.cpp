module;

#include <string>

module runtime;

import mylib;
import core.env;
import core.format;
import core.log;
import core.memory;
import core.memory.index;
import core.memory.store;
import core.path;
import core.time;

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

        core::path::path data_root()
        {
            const auto exe_dir = core::path::executable_dir();
            if (exe_dir.empty())
            {
                core::log::warn("runtime", "executable dir unavailable; using relative ./data");
                return core::path::normalize(core::path::path{"data"});
            }
            return core::path::join(exe_dir, "data");
        }

        void load_memory_store_and_stage()
        {
            const auto root = core::path::join(data_root(), "memory");
            auto snapshot_result = core::memory::store::rebuild_state(root);
            if (!snapshot_result)
            {
                core::log::error("runtime", core::format::str("memory store load failed: {}",
                                                             snapshot_result.error().message));
                return;
            }
            auto index_result = core::memory::index::build_from_snapshot(*snapshot_result);
            if (!index_result)
            {
                core::log::error("runtime", core::format::str("memory index build failed: {}",
                                                             index_result.error().message));
                return;
            }
            core::log::info("runtime",
                            core::format::str("memory store loaded (records={})",
                                              snapshot_result->records.size()));

            auto staged_result = core::memory::stage_list();
            if (!staged_result)
            {
                core::log::error("runtime", core::format::str("memory staging load failed: {}",
                                                             staged_result.error().message));
                return;
            }
            core::log::info("runtime",
                            core::format::str("memory staging loaded (records={})",
                                              staged_result->size()));
        }

        void run_smoke_sequence()
        {
            const std::string_view smoke_text = "smoke test record";
            auto stage_result = core::memory::stage_add(smoke_text);
            if (!stage_result)
            {
                core::log::error("runtime", core::format::str("smoke stage_add failed: {}",
                                                             stage_result.error().message));
                return;
            }
            auto commit_result = core::memory::stage_commit();
            if (!commit_result)
            {
                core::log::error("runtime", core::format::str("smoke stage_commit failed: {}",
                                                             commit_result.error().message));
                return;
            }

            core::memory::memory_query query;
            query.text = std::string{smoke_text};
            query.limit = 1;
            auto query_result = core::memory::store_query(query);
            if (!query_result)
            {
                core::log::error("runtime", core::format::str("smoke store_query failed: {}",
                                                             query_result.error().message));
                return;
            }

            if (query_result->empty())
            {
                core::log::info("runtime", "smoke query completed (results=0)");
                return;
            }

            const auto& top = query_result->front();
            core::log::info("runtime",
                            core::format::str("smoke query completed (results={}, id={}, score={:.2f})",
                                              query_result->size(),
                                              top.record.id.value,
                                              top.score));
        }
    }

    int run()
    {
        core::log::write(core::log::level::info, "runtime", "startup");
        core::log::write(core::log::level::info, "runtime", "exe running");
        load_memory_store_and_stage();

        const int rc = mylib::entry();
        core::log::write(core::log::level::info, "runtime", core::format::str("mylib::entry() -> {}", rc));

        core::time::frame_clock fc{};
        fc.start();

        const bool smoke = smoke_mode();
        const std::uint64_t max_frames = smoke ? 3u : ~0ull;
        if (smoke)
        {
            run_smoke_sequence();
        }

        while (fc.frame_index < max_frames)
        {
            fc.tick();
            core::log::info("runtime",
                            core::format::str("running (frame={}, dt_ms={:.3f})",
                                              fc.frame_index,
                                              fc.dt_seconds() * 1000.0));
            core::time::sleep_ms(1000);
        }

        if (smoke)
            core::log::write(core::log::level::info, "runtime", "smoke complete");

        return 0;
    }
}
