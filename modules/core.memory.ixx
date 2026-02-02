module;

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module core.memory;

import core.error;
import core.id;

export namespace core::memory
{
    struct memory_tag;
    using memory_id = core::id::strong_id<memory_tag, std::uint64_t>;

    struct memory_record
    {
        memory_id id{};
        std::string text;
        std::uint64_t created_ns = 0;
        std::uint64_t updated_ns = 0;
        float strength = 1.0f;
    };

    struct memory_query
    {
        std::string text;
        std::size_t limit = 16;
    };

    struct memory_result
    {
        memory_record record;
        float score = 0.0f;
    };

    core::error::result<memory_id> stage_add(std::string_view text);
    core::error::result<void> stage_edit(memory_id id, std::string_view text);
    core::error::result<std::vector<memory_record>> stage_list();
    core::error::result<void> stage_commit();
    core::error::result<void> stage_discard();

    core::error::result<std::vector<memory_result>> store_query(const memory_query& query);
    core::error::result<void> reinforce(memory_id id, float delta);
    core::error::result<void> decay_sweep();
}
