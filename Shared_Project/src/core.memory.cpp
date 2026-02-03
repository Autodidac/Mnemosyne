module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

module core.memory;

import core.assert;
import core.error;
import core.log;
import core.memory.index;
import core.memory.stage;
import core.time;

namespace core::memory
{
    namespace
    {
        struct memory_state
        {
            std::vector<memory_record> stored;
        };

        memory_state& state()
        {
            static memory_state s;
            return s;
        }

        core::error::err not_found(std::string_view msg)
        {
            return core::error::make({core::error::core_domain::id, core::error::core_domain::not_found}, msg);
        }

        namespace store
        {
            core::error::result<std::vector<memory_result>> query(const memory_query& query)
            {
                auto& s = state();
                std::vector<memory_result> results;
                results.reserve(std::min(query.limit, s.stored.size()));

                if (query.limit == 0)
                {
                    return results;
                }

                for (const auto& rec : s.stored)
                {
                    const bool match = query.text.empty() ||
                        (rec.text.find(query.text) != std::string::npos);
                    if (!match)
                    {
                        continue;
                    }

                    memory_result result;
                    result.record = rec;
                    result.score = query.text.empty() ? 0.0f : 1.0f;
                    results.push_back(result);
                    if (results.size() >= query.limit)
                    {
                        break;
                    }
                }

                core::log::info("memory", "store query completed");
                return results;
            }

            core::error::result<void> reinforce(memory_id id, float delta)
            {
                if (!id.valid())
                {
                    core::asserts::that(false, "memory id must be valid");
                    return std::unexpected(core::error::invalid_argument("memory id invalid"));
                }

                auto& s = state();
                auto it = std::find_if(s.stored.begin(), s.stored.end(),
                    [&](const memory_record& rec) { return rec.id == id; });
                if (it == s.stored.end())
                {
                    return std::unexpected(not_found("stored memory not found"));
                }

                it->strength = std::max(0.0f, it->strength + delta);
                it->updated_ns = core::time::now_ns();
                core::log::info("memory", "reinforced memory record");
                return {};
            }

            core::error::result<void> decay_sweep()
            {
                auto& s = state();
                if (s.stored.empty())
                {
                    core::log::trace("memory", "no stored records to decay");
                    return {};
                }

                const auto now = core::time::now_ns();
                for (auto& rec : s.stored)
                {
                    rec.strength = std::max(0.0f, rec.strength * 0.98f);
                    rec.updated_ns = now;
                }
                core::log::info("memory", "decayed stored memory records");
                return {};
            }
        }

    }

    core::error::result<memory_id> stage_add(std::string_view text)
    {
        return core::memory::stage::add(text);
    }

    core::error::result<void> stage_edit(memory_id id, std::string_view text)
    {
        return core::memory::stage::edit(id, text);
    }

    core::error::result<std::vector<memory_record>> stage_list()
    {
        return core::memory::stage::list();
    }

    core::error::result<void> stage_commit()
    {
        auto committed = core::memory::stage::commit();
        if (!committed)
        {
            return std::unexpected(committed.error());
        }
        if (committed->empty())
        {
            core::log::trace("memory", "no staged records to commit");
            return {};
        }
        auto& s = state();
        s.stored.insert(s.stored.end(), committed->begin(), committed->end());
        auto index_result = core::memory::index::update_on_commit(*committed);
        if (!index_result)
        {
            return index_result;
        }
        return {};
    }

    core::error::result<void> stage_discard()
    {
        return core::memory::stage::discard();
    }

    core::error::result<std::vector<memory_result>> store_query(const memory_query& query)
    {
        return core::memory::index::query(query);
    }

    core::error::result<void> reinforce(memory_id id, float delta)
    {
        return store::reinforce(id, delta);
    }

    core::error::result<void> decay_sweep()
    {
        return store::decay_sweep();
    }
}
