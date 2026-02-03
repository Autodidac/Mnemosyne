#include <algorithm>
#include <cstdlib>
#include <filesystem>

import core.assert;
import core.env;
import core.error;
import core.format;
import core.id;
import core.math;
import core.memory;
import core.memory.store;
import core.path;
import core.string;
import core.time;

int main()
{
    using core::asserts::that;

    // core.math
    that(core::math::clamp(5, 0, 3) == 3);
    that(core::math::lerp(0.0, 10.0, 0.5) == 5.0);

    // core.string
    that(core::string::trim("  hi  ") == "hi");

    // core.id (basic compile-time sanity)
    struct TagA {};
    core::id::strong_id<TagA> a{42};
    that(a.value == 42);

    // core.env
    (void)core::env::set("DEMO_TEST_ENV", "123");
    auto v = core::env::get("DEMO_TEST_ENV");
    that(v.has_value() && *v == "123");
    (void)core::env::unset("DEMO_TEST_ENV");

    // core.path (best-effort existence)
    auto exe = core::path::executable_path();
    that(!exe.empty());

    // core.error (source_location present)
    auto e = core::error::failed("x");
    that((bool)e);

    // core.memory (stage add/edit/commit)
    const auto exe_dir = core::path::executable_dir();
    core::path::path memory_root = exe_dir.empty()
        ? core::path::normalize(core::path::path{"data/memory"})
        : core::path::join(core::path::join(exe_dir, "data"), "memory");
    std::error_code cleanup_ec;
    std::filesystem::remove_all(memory_root, cleanup_ec);

    auto stage_add_result = core::memory::stage_add("alpha one");
    that(stage_add_result.has_value());
    auto stage_id = *stage_add_result;

    auto stage_edit_result = core::memory::stage_edit(stage_id, "alpha one edited");
    that(stage_edit_result.has_value());

    auto stage_list_result = core::memory::stage_list();
    that(stage_list_result.has_value());
    that(stage_list_result->size() == 1);
    that(stage_list_result->front().id == stage_id);
    that(stage_list_result->front().text == "alpha one edited");
    auto staged_records = *stage_list_result;

    auto stage_commit_result = core::memory::stage_commit();
    that(stage_commit_result.has_value());
    that(staged_records.size() == 1);
    that(staged_records.front().id == stage_id);

    auto stage_list_after_commit = core::memory::stage_list();
    that(stage_list_after_commit.has_value());
    that(stage_list_after_commit->empty());

    // core.memory.store (reload from disk yields identical state)
    const auto store_root = std::filesystem::temp_directory_path() / "mnemosyne_memory_store_test";
    std::filesystem::remove_all(store_root, cleanup_ec);
    std::filesystem::create_directories(store_root, cleanup_ec);

    core::memory::store::memory_snapshot snapshot;
    snapshot.records = staged_records;
    snapshot.next_id = 1;
    for (const auto& record : snapshot.records)
    {
        snapshot.next_id = std::max(snapshot.next_id, record.id.value + 1);
    }

    auto save_snapshot_result = core::memory::store::save_snapshot(store_root, snapshot);
    that(save_snapshot_result.has_value());

    auto reload_result = core::memory::store::rebuild_state(store_root);
    that(reload_result.has_value());
    that(reload_result->records.size() == snapshot.records.size());
    that(reload_result->next_id == snapshot.next_id);
    for (std::size_t i = 0; i < snapshot.records.size(); ++i)
    {
        const auto& expected = snapshot.records[i];
        const auto& actual = reload_result->records[i];
        that(actual.id == expected.id);
        that(actual.text == expected.text);
        that(actual.created_ns == expected.created_ns);
        that(actual.updated_ns == expected.updated_ns);
        that(actual.strength == expected.strength);
    }

    // core.memory.query (deterministic ordering)
    auto stage_add_more = core::memory::stage_add("alpha bravo");
    that(stage_add_more.has_value());
    auto stage_add_more_two = core::memory::stage_add("alpha charlie");
    that(stage_add_more_two.has_value());
    auto stage_commit_more = core::memory::stage_commit();
    that(stage_commit_more.has_value());

    core::memory::memory_query query;
    query.text = "alpha";
    query.limit = 10;
    auto query_first = core::memory::store_query(query);
    that(query_first.has_value());
    auto query_second = core::memory::store_query(query);
    that(query_second.has_value());
    that(query_first->size() == query_second->size());
    for (std::size_t i = 0; i < query_first->size(); ++i)
    {
        that((*query_first)[i].record.id == (*query_second)[i].record.id);
    }

    return 0;
}
