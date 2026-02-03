module;

#include <string_view>
#include <vector>

export module core.memory;

import core.error;
export import core.memory.types;

export namespace core::memory
{
    core::error::result<memory_id> stage_add(std::string_view text);
    core::error::result<void> stage_edit(memory_id id, std::string_view text);
    core::error::result<std::vector<memory_record>> stage_list();
    core::error::result<void> stage_commit();
    core::error::result<void> stage_discard();

    core::error::result<std::vector<memory_result>> store_query(const memory_query& query);
    core::error::result<void> reinforce(memory_id id, float delta);
    core::error::result<void> decay_sweep();
}
