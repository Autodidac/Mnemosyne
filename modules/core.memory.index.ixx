module;

#include <cstddef>
#include <vector>

export module core.memory.index;

import core.error;
import core.memory;
import core.memory.store;

export namespace core::memory::index
{
    constexpr std::size_t semantic_dimensions = 256;
    constexpr float w_keyword = 1.0f;
    constexpr float w_semantic = 0.35f;
    constexpr float w_confidence = 0.25f;
    constexpr float w_age = 0.05f;

    core::error::result<void> build_from_snapshot(const core::memory::store::memory_snapshot& snapshot);
    core::error::result<void> update_on_commit(const std::vector<memory_record>& committed);
    core::error::result<std::vector<memory_result>> query(const memory_query& query);
}
