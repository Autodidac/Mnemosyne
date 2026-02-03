module;

#include <string_view>
#include <vector>

export module core.memory.stage;

import core.error;
import core.memory.types;

export namespace core::memory::stage
{
    core::error::result<memory_id> add(std::string_view text);
    core::error::result<void> edit(memory_id id, std::string_view text);
    core::error::result<std::vector<memory_record>> list();
    core::error::result<std::vector<memory_record>> commit();
    core::error::result<void> discard();
}
