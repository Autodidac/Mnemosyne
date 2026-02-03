module;

#include <cstdint>
#include <filesystem>
#include <vector>

export module core.memory.store;

import core.error;
import core.memory.types;

export namespace core::memory::store
{
    struct memory_snapshot
    {
        std::vector<memory_record> records;
        std::uint64_t next_id = 1;
    };

    core::error::result<memory_snapshot> load_snapshot(const std::filesystem::path& root);
    core::error::result<void> save_snapshot(const std::filesystem::path& root, const memory_snapshot& snapshot);
    core::error::result<void> append_journal(const std::filesystem::path& root,
                                             const std::vector<memory_record>& records);
    core::error::result<memory_snapshot> rebuild_state(const std::filesystem::path& root);
}
