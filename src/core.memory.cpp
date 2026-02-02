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
import core.path;
import core.time;

namespace core::memory
{
    namespace
    {
        struct memory_state
        {
            std::vector<memory_record> staged;
            std::vector<memory_record> stored;
            std::uint64_t next_id = 1;
        };

        memory_state& state()
        {
            static memory_state s;
            return s;
        }

        core::path::path memory_root()
        {
            static core::path::path root;
            static bool init = false;
            if (!init)
            {
                const auto base = core::path::executable_dir();
                if (base.empty())
                {
                    root = core::path::normalize(core::path::path{"data/memory"});
                    core::log::warn("memory", "executable dir unavailable; using relative data/memory");
                }
                else
                {
                    root = core::path::join(core::path::join(base, "data"), "memory");
                }
                core::asserts::that(!root.empty(), "memory root path unavailable");
                core::log::info("memory", std::string{"memory root: "} + root.string());
                init = true;
            }
            return root;
        }

        core::error::err not_found(std::string_view msg)
        {
            return core::error::make({core::error::core_domain::id, core::error::core_domain::not_found}, msg);
        }

        namespace stage
        {
            core::error::result<memory_id> add(std::string_view text)
            {
                if (text.empty())
                {
                    return std::unexpected(core::error::invalid_argument("memory text empty"));
                }

                auto& s = state();
                memory_record rec;
                rec.id = memory_id{s.next_id++};
                rec.text = std::string{text};
                rec.created_ns = core::time::now_ns();
                rec.updated_ns = rec.created_ns;
                rec.strength = 1.0f;

                core::asserts::that(rec.id.valid(), "generated memory id invalid");
                s.staged.push_back(rec);
                core::log::info("memory", "staged new memory record");
                return rec.id;
            }

            core::error::result<void> edit(memory_id id, std::string_view text)
            {
                if (!id.valid())
                {
                    core::asserts::that(false, "memory id must be valid");
                    return std::unexpected(core::error::invalid_argument("memory id invalid"));
                }
                if (text.empty())
                {
                    return std::unexpected(core::error::invalid_argument("memory text empty"));
                }

                auto& s = state();
                auto it = std::find_if(s.staged.begin(), s.staged.end(),
                    [&](const memory_record& rec) { return rec.id == id; });
                if (it == s.staged.end())
                {
                    return std::unexpected(not_found("staged memory not found"));
                }

                it->text = std::string{text};
                it->updated_ns = core::time::now_ns();
                core::log::info("memory", "updated staged memory record");
                return {};
            }

            core::error::result<std::vector<memory_record>> list()
            {
                auto& s = state();
                return s.staged;
            }

            core::error::result<void> commit()
            {
                auto& s = state();
                if (s.staged.empty())
                {
                    core::log::trace("memory", "no staged records to commit");
                    return {};
                }

                const auto root = memory_root();
                core::log::info("memory", std::string{"committing staged records to "} + root.string());
                s.stored.insert(s.stored.end(), s.staged.begin(), s.staged.end());
                auto index_result = core::memory::index::update_on_commit(s.staged);
                if (!index_result)
                {
                    return index_result;
                }
                s.staged.clear();
                return {};
            }

            core::error::result<void> discard()
            {
                auto& s = state();
                if (s.staged.empty())
                {
                    core::log::trace("memory", "no staged records to discard");
                    return {};
                }
                s.staged.clear();
                core::log::info("memory", "discarded staged memory records");
                return {};
            }
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
        return stage::add(text);
    }

    core::error::result<void> stage_edit(memory_id id, std::string_view text)
    {
        return stage::edit(id, text);
    }

    core::error::result<std::vector<memory_record>> stage_list()
    {
        return stage::list();
    }

    core::error::result<void> stage_commit()
    {
        return stage::commit();
    }

    core::error::result<void> stage_discard()
    {
        return stage::discard();
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
