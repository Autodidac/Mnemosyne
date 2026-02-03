module;

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

module core.memory.stage;

import core.assert;
import core.error;
import core.log;
import core.memory.types;
import core.path;
import core.time;

namespace core::memory::stage
{
    namespace
    {
        constexpr std::string_view snapshot_filename = "memory.staging.snapshot.json";
        constexpr std::string_view snapshot_tmp_filename = "memory.staging.snapshot.json.tmp";
        constexpr std::string_view journal_filename = "memory.staging.jsonl";

        struct cursor
        {
            std::string_view input;
            std::size_t pos = 0;
        };

        enum class patch_kind
        {
            add,
            edit,
            discard,
            commit
        };

        struct stage_patch
        {
            patch_kind kind{};
            std::optional<memory_record> record;
            std::vector<memory_id> ids;
        };

        struct stage_snapshot
        {
            std::vector<memory_record> staged;
            std::vector<memory_id> committed;
            std::uint64_t next_id = 1;
        };

        struct stage_state
        {
            std::map<memory_id, memory_record> staged;
            std::set<memory_id> committed;
            std::uint64_t next_id = 1;
            bool loaded = false;
        };

        stage_state& state()
        {
            static stage_state s;
            return s;
        }

        void log_warn(std::string_view msg)
        {
            core::log::warn("memory.stage", msg);
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
                    core::log::warn("memory.stage", "executable dir unavailable; using relative data/memory");
                }
                else
                {
                    root = core::path::join(core::path::join(base, "data"), "memory");
                }
                core::asserts::that(!root.empty(), "memory root path unavailable");
                core::log::info("memory.stage", std::string{"memory root: "} + root.string());
                init = true;
            }
            return root;
        }

        core::error::err not_found(std::string_view msg)
        {
            return core::error::make({core::error::core_domain::id, core::error::core_domain::not_found}, msg);
        }

        core::error::err already_committed(std::string_view msg)
        {
            return core::error::make({core::error::core_domain::id, core::error::core_domain::failed}, msg);
        }

        void skip_ws(cursor& cur)
        {
            while (cur.pos < cur.input.size())
            {
                const char ch = cur.input[cur.pos];
                if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t')
                {
                    ++cur.pos;
                    continue;
                }
                break;
            }
        }

        bool consume(cursor& cur, char expected)
        {
            skip_ws(cur);
            if (cur.pos >= cur.input.size() || cur.input[cur.pos] != expected)
            {
                return false;
            }
            ++cur.pos;
            return true;
        }

        std::optional<std::string> parse_string(cursor& cur)
        {
            skip_ws(cur);
            if (cur.pos >= cur.input.size() || cur.input[cur.pos] != '"')
            {
                return std::nullopt;
            }
            ++cur.pos;
            std::string result;
            while (cur.pos < cur.input.size())
            {
                char ch = cur.input[cur.pos++];
                if (ch == '"')
                {
                    return result;
                }
                if (ch == '\\')
                {
                    if (cur.pos >= cur.input.size())
                    {
                        return std::nullopt;
                    }
                    char esc = cur.input[cur.pos++];
                    switch (esc)
                    {
                        case '"': result.push_back('"'); break;
                        case '\\': result.push_back('\\'); break;
                        case 'n': result.push_back('\n'); break;
                        case 'r': result.push_back('\r'); break;
                        case 't': result.push_back('\t'); break;
                        default: return std::nullopt;
                    }
                }
                else
                {
                    result.push_back(ch);
                }
            }
            return std::nullopt;
        }

        bool parse_uint64(cursor& cur, std::uint64_t& out)
        {
            skip_ws(cur);
            if (cur.pos >= cur.input.size())
            {
                return false;
            }
            const std::size_t start = cur.pos;
            if (cur.input[cur.pos] == '-')
            {
                return false;
            }
            while (cur.pos < cur.input.size())
            {
                char ch = cur.input[cur.pos];
                if (ch < '0' || ch > '9')
                {
                    break;
                }
                ++cur.pos;
            }
            if (cur.pos == start)
            {
                return false;
            }
            std::uint64_t value = 0;
            auto result = std::from_chars(cur.input.data() + start, cur.input.data() + cur.pos, value);
            if (result.ec != std::errc{})
            {
                return false;
            }
            out = value;
            return true;
        }

        bool parse_float(cursor& cur, float& out)
        {
            skip_ws(cur);
            if (cur.pos >= cur.input.size())
            {
                return false;
            }
            const std::size_t start = cur.pos;
            while (cur.pos < cur.input.size())
            {
                char ch = cur.input[cur.pos];
                if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E')
                {
                    ++cur.pos;
                    continue;
                }
                break;
            }
            if (cur.pos == start)
            {
                return false;
            }
            std::string token{cur.input.substr(start, cur.pos - start)};
            char* end = nullptr;
            errno = 0;
            float value = std::strtof(token.c_str(), &end);
            if (end == token.c_str() || errno == ERANGE)
            {
                return false;
            }
            out = value;
            return true;
        }

        bool skip_literal(cursor& cur, std::string_view literal)
        {
            skip_ws(cur);
            if (cur.input.substr(cur.pos, literal.size()) != literal)
            {
                return false;
            }
            cur.pos += literal.size();
            return true;
        }

        bool skip_value(cursor& cur);

        bool skip_array(cursor& cur)
        {
            if (!consume(cur, '['))
            {
                return false;
            }
            skip_ws(cur);
            if (consume(cur, ']'))
            {
                return true;
            }
            while (true)
            {
                if (!skip_value(cur))
                {
                    return false;
                }
                skip_ws(cur);
                if (consume(cur, ']'))
                {
                    return true;
                }
                if (!consume(cur, ','))
                {
                    return false;
                }
            }
        }

        bool skip_object(cursor& cur)
        {
            if (!consume(cur, '{'))
            {
                return false;
            }
            skip_ws(cur);
            if (consume(cur, '}'))
            {
                return true;
            }
            while (true)
            {
                auto key = parse_string(cur);
                if (!key)
                {
                    return false;
                }
                if (!consume(cur, ':'))
                {
                    return false;
                }
                if (!skip_value(cur))
                {
                    return false;
                }
                skip_ws(cur);
                if (consume(cur, '}'))
                {
                    return true;
                }
                if (!consume(cur, ','))
                {
                    return false;
                }
            }
        }

        bool skip_value(cursor& cur)
        {
            skip_ws(cur);
            if (cur.pos >= cur.input.size())
            {
                return false;
            }
            char ch = cur.input[cur.pos];
            if (ch == '"')
            {
                return static_cast<bool>(parse_string(cur));
            }
            if (ch == '{')
            {
                return skip_object(cur);
            }
            if (ch == '[')
            {
                return skip_array(cur);
            }
            if (ch == 't')
            {
                return skip_literal(cur, "true");
            }
            if (ch == 'f')
            {
                return skip_literal(cur, "false");
            }
            if (ch == 'n')
            {
                return skip_literal(cur, "null");
            }
            float value = 0.0f;
            return parse_float(cur, value);
        }

        void write_string(std::ostream& out, std::string_view value)
        {
            out.put('"');
            for (char ch : value)
            {
                switch (ch)
                {
                    case '"': out << "\\\""; break;
                    case '\\': out << "\\\\"; break;
                    case '\n': out << "\\n"; break;
                    case '\r': out << "\\r"; break;
                    case '\t': out << "\\t"; break;
                    default: out.put(ch); break;
                }
            }
            out.put('"');
        }

        void write_record(std::ostream& out, const memory_record& record)
        {
            out << '{';
            out << "\"id\":" << record.id.value << ',';
            out << "\"text\":";
            write_string(out, record.text);
            out << ',';
            out << "\"created_ns\":" << record.created_ns << ',';
            out << "\"updated_ns\":" << record.updated_ns << ',';
            out << "\"strength\":" << std::to_string(record.strength);
            out << '}';
        }

        std::optional<memory_record> parse_record(cursor& cur)
        {
            if (!consume(cur, '{'))
            {
                return std::nullopt;
            }
            std::optional<std::uint64_t> id;
            std::optional<std::string> text;
            std::optional<std::uint64_t> created_ns;
            std::optional<std::uint64_t> updated_ns;
            std::optional<float> strength;

            skip_ws(cur);
            if (consume(cur, '}'))
            {
                return std::nullopt;
            }
            while (true)
            {
                auto key = parse_string(cur);
                if (!key)
                {
                    return std::nullopt;
                }
                if (!consume(cur, ':'))
                {
                    return std::nullopt;
                }
                if (*key == "id")
                {
                    std::uint64_t value = 0;
                    if (!parse_uint64(cur, value))
                    {
                        return std::nullopt;
                    }
                    id = value;
                }
                else if (*key == "text")
                {
                    auto value = parse_string(cur);
                    if (!value)
                    {
                        return std::nullopt;
                    }
                    text = std::move(*value);
                }
                else if (*key == "created_ns")
                {
                    std::uint64_t value = 0;
                    if (!parse_uint64(cur, value))
                    {
                        return std::nullopt;
                    }
                    created_ns = value;
                }
                else if (*key == "updated_ns")
                {
                    std::uint64_t value = 0;
                    if (!parse_uint64(cur, value))
                    {
                        return std::nullopt;
                    }
                    updated_ns = value;
                }
                else if (*key == "strength")
                {
                    float value = 0.0f;
                    if (!parse_float(cur, value))
                    {
                        return std::nullopt;
                    }
                    strength = value;
                }
                else
                {
                    if (!skip_value(cur))
                    {
                        return std::nullopt;
                    }
                }

                skip_ws(cur);
                if (consume(cur, '}'))
                {
                    break;
                }
                if (!consume(cur, ','))
                {
                    return std::nullopt;
                }
            }

            if (!id || !text || !created_ns || !updated_ns || !strength)
            {
                return std::nullopt;
            }

            memory_record record;
            record.id = memory_id{*id};
            record.text = std::move(*text);
            record.created_ns = *created_ns;
            record.updated_ns = *updated_ns;
            record.strength = *strength;
            return record;
        }

        std::vector<memory_id> parse_id_array(cursor& cur, bool& ok)
        {
            std::vector<memory_id> ids;
            ok = false;
            if (!consume(cur, '['))
            {
                return ids;
            }
            skip_ws(cur);
            if (consume(cur, ']'))
            {
                ok = true;
                return ids;
            }
            while (true)
            {
                std::uint64_t value = 0;
                if (!parse_uint64(cur, value))
                {
                    return ids;
                }
                ids.push_back(memory_id{value});
                skip_ws(cur);
                if (consume(cur, ']'))
                {
                    ok = true;
                    break;
                }
                if (!consume(cur, ','))
                {
                    return ids;
                }
            }
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            return ids;
        }

        std::optional<stage_snapshot> parse_snapshot(cursor& cur)
        {
            if (!consume(cur, '{'))
            {
                return std::nullopt;
            }
            std::optional<std::uint64_t> next_id;
            std::vector<memory_record> staged;
            std::vector<memory_id> committed;

            skip_ws(cur);
            if (consume(cur, '}'))
            {
                return std::nullopt;
            }
            while (true)
            {
                auto key = parse_string(cur);
                if (!key)
                {
                    return std::nullopt;
                }
                if (!consume(cur, ':'))
                {
                    return std::nullopt;
                }
                if (*key == "next_id")
                {
                    std::uint64_t value = 0;
                    if (!parse_uint64(cur, value))
                    {
                        return std::nullopt;
                    }
                    next_id = value;
                }
                else if (*key == "staged")
                {
                    if (!consume(cur, '['))
                    {
                        return std::nullopt;
                    }
                    skip_ws(cur);
                    if (!consume(cur, ']'))
                    {
                        while (true)
                        {
                            cursor attempt = cur;
                            auto record = parse_record(attempt);
                            if (record)
                            {
                                staged.push_back(std::move(*record));
                                cur = attempt;
                            }
                            else
                            {
                                log_warn("memory staging snapshot record malformed; skipped");
                                if (!skip_value(cur))
                                {
                                    return std::nullopt;
                                }
                            }
                            skip_ws(cur);
                            if (consume(cur, ']'))
                            {
                                break;
                            }
                            if (!consume(cur, ','))
                            {
                                return std::nullopt;
                            }
                        }
                    }
                }
                else if (*key == "committed")
                {
                    bool ok = false;
                    committed = parse_id_array(cur, ok);
                    if (!ok)
                    {
                        return std::nullopt;
                    }
                }
                else
                {
                    if (!skip_value(cur))
                    {
                        return std::nullopt;
                    }
                }

                skip_ws(cur);
                if (consume(cur, '}'))
                {
                    break;
                }
                if (!consume(cur, ','))
                {
                    return std::nullopt;
                }
            }

            stage_snapshot snapshot;
            if (next_id)
            {
                snapshot.next_id = *next_id;
            }
            else
            {
                log_warn("memory staging snapshot missing next_id; defaulting to 1");
                snapshot.next_id = 1;
            }
            snapshot.staged = std::move(staged);
            snapshot.committed = std::move(committed);
            return snapshot;
        }

        std::optional<stage_patch> parse_patch(cursor& cur)
        {
            if (!consume(cur, '{'))
            {
                return std::nullopt;
            }
            std::optional<std::string> op;
            std::optional<memory_record> record;
            std::vector<memory_id> ids;

            skip_ws(cur);
            if (consume(cur, '}'))
            {
                return std::nullopt;
            }
            while (true)
            {
                auto key = parse_string(cur);
                if (!key)
                {
                    return std::nullopt;
                }
                if (!consume(cur, ':'))
                {
                    return std::nullopt;
                }
                if (*key == "op")
                {
                    auto value = parse_string(cur);
                    if (!value)
                    {
                        return std::nullopt;
                    }
                    op = std::move(*value);
                }
                else if (*key == "record")
                {
                    auto value = parse_record(cur);
                    if (!value)
                    {
                        return std::nullopt;
                    }
                    record = std::move(*value);
                }
                else if (*key == "ids")
                {
                    bool ok = false;
                    ids = parse_id_array(cur, ok);
                    if (!ok)
                    {
                        return std::nullopt;
                    }
                }
                else
                {
                    if (!skip_value(cur))
                    {
                        return std::nullopt;
                    }
                }

                skip_ws(cur);
                if (consume(cur, '}'))
                {
                    break;
                }
                if (!consume(cur, ','))
                {
                    return std::nullopt;
                }
            }

            if (!op)
            {
                return std::nullopt;
            }

            stage_patch patch;
            if (*op == "add")
            {
                if (!record)
                {
                    return std::nullopt;
                }
                patch.kind = patch_kind::add;
                patch.record = std::move(record);
            }
            else if (*op == "edit")
            {
                if (!record)
                {
                    return std::nullopt;
                }
                patch.kind = patch_kind::edit;
                patch.record = std::move(record);
            }
            else if (*op == "discard")
            {
                patch.kind = patch_kind::discard;
                patch.ids = std::move(ids);
            }
            else if (*op == "commit")
            {
                patch.kind = patch_kind::commit;
                patch.ids = std::move(ids);
            }
            else
            {
                return std::nullopt;
            }
            return patch;
        }

        std::optional<std::string> read_file(const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::in | std::ios::binary);
            if (!input)
            {
                return std::nullopt;
            }
            std::ostringstream buffer;
            buffer << input.rdbuf();
            return buffer.str();
        }

        core::error::result<void> ensure_directory(const std::filesystem::path& root)
        {
            std::error_code ec;
            if (std::filesystem::exists(root, ec))
            {
                return {};
            }
            if (!std::filesystem::create_directories(root, ec))
            {
                return std::unexpected(core::error::make({core::error::core_domain::id, core::error::core_domain::io_error},
                    "memory staging directory create failed"));
            }
            return {};
        }

        core::error::result<void> append_patch(const std::filesystem::path& root, const stage_patch& patch)
        {
            auto result = ensure_directory(root);
            if (!result)
            {
                return result;
            }

            const auto path = root / journal_filename;
            std::ofstream output(path, std::ios::out | std::ios::app | std::ios::binary);
            if (!output)
            {
                return std::unexpected(core::error::make({core::error::core_domain::id, core::error::core_domain::io_error},
                    "memory staging journal append failed"));
            }

            output << '{';
            if (patch.kind == patch_kind::add)
            {
                output << "\"op\":\"add\",\"record\":";
                write_record(output, *patch.record);
            }
            else if (patch.kind == patch_kind::edit)
            {
                output << "\"op\":\"edit\",\"record\":";
                write_record(output, *patch.record);
            }
            else if (patch.kind == patch_kind::discard)
            {
                output << "\"op\":\"discard\",\"ids\":[";
                for (std::size_t i = 0; i < patch.ids.size(); ++i)
                {
                    if (i > 0)
                    {
                        output << ',';
                    }
                    output << patch.ids[i].value;
                }
                output << ']';
            }
            else if (patch.kind == patch_kind::commit)
            {
                output << "\"op\":\"commit\",\"ids\":[";
                for (std::size_t i = 0; i < patch.ids.size(); ++i)
                {
                    if (i > 0)
                    {
                        output << ',';
                    }
                    output << patch.ids[i].value;
                }
                output << ']';
            }
            output << "}\n";
            output.flush();
            return {};
        }

        core::error::result<void> truncate_journal(const std::filesystem::path& root)
        {
            const auto path = root / journal_filename;
            std::ofstream output(path, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!output)
            {
                return std::unexpected(core::error::make({core::error::core_domain::id, core::error::core_domain::io_error},
                    "memory staging journal truncate failed"));
            }
            return {};
        }

        core::error::result<void> save_snapshot(const std::filesystem::path& root, const stage_state& s)
        {
            auto result = ensure_directory(root);
            if (!result)
            {
                return result;
            }

            const auto temp_path = root / snapshot_tmp_filename;
            const auto final_path = root / snapshot_filename;
            std::ofstream output(temp_path, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output)
            {
                return std::unexpected(core::error::make({core::error::core_domain::id, core::error::core_domain::io_error},
                    "memory staging snapshot write failed"));
            }

            output << '{';
            output << "\"next_id\":" << s.next_id << ',';
            output << "\"staged\":[";
            bool first = true;
            for (const auto& [id, record] : s.staged)
            {
                if (!first)
                {
                    output << ',';
                }
                write_record(output, record);
                first = false;
            }
            output << "],\"committed\":[";
            std::size_t idx = 0;
            for (const auto& id : s.committed)
            {
                if (idx++ > 0)
                {
                    output << ',';
                }
                output << id.value;
            }
            output << "]}";
            output.flush();
            output.close();

            std::error_code ec;
            std::filesystem::rename(temp_path, final_path, ec);
            if (ec)
            {
                return std::unexpected(core::error::make({core::error::core_domain::id, core::error::core_domain::io_error},
                    "memory staging snapshot rename failed"));
            }
            return {};
        }

        core::error::result<stage_snapshot> load_snapshot(const std::filesystem::path& root)
        {
            const auto path = root / snapshot_filename;
            std::error_code ec;
            if (!std::filesystem::exists(path, ec))
            {
                stage_snapshot snapshot;
                snapshot.next_id = 1;
                return snapshot;
            }

            auto data = read_file(path);
            if (!data)
            {
                return std::unexpected(core::error::make({core::error::core_domain::id, core::error::core_domain::io_error},
                    "memory staging snapshot read failed"));
            }

            cursor cur{*data, 0};
            auto parsed = parse_snapshot(cur);
            if (!parsed)
            {
                log_warn("memory staging snapshot malformed; using empty snapshot");
                stage_snapshot snapshot;
                snapshot.next_id = 1;
                return snapshot;
            }
            skip_ws(cur);
            if (cur.pos != cur.input.size())
            {
                log_warn("memory staging snapshot trailing data ignored");
            }
            return *parsed;
        }

        core::error::result<void> apply_patch(stage_state& s, const stage_patch& patch)
        {
            if (patch.kind == patch_kind::add)
            {
                const auto& record = *patch.record;
                if (s.committed.contains(record.id))
                {
                    return std::unexpected(already_committed("memory id already committed"));
                }
                if (s.staged.contains(record.id))
                {
                    return std::unexpected(core::error::invalid_argument("staged memory already exists"));
                }
                s.staged.emplace(record.id, record);
                s.next_id = std::max(s.next_id, record.id.value + 1);
                return {};
            }

            if (patch.kind == patch_kind::edit)
            {
                const auto& record = *patch.record;
                if (s.committed.contains(record.id))
                {
                    return std::unexpected(already_committed("memory id already committed"));
                }
                auto it = s.staged.find(record.id);
                if (it == s.staged.end())
                {
                    return std::unexpected(not_found("staged memory not found"));
                }
                it->second = record;
                return {};
            }

            if (patch.kind == patch_kind::discard)
            {
                for (const auto& id : patch.ids)
                {
                    if (s.committed.contains(id))
                    {
                        return std::unexpected(already_committed("memory id already committed"));
                    }
                    auto it = s.staged.find(id);
                    if (it == s.staged.end())
                    {
                        return std::unexpected(not_found("staged memory not found"));
                    }
                    s.staged.erase(it);
                }
                return {};
            }

            if (patch.kind == patch_kind::commit)
            {
                for (const auto& id : patch.ids)
                {
                    if (s.committed.contains(id))
                    {
                        log_warn("memory staging commit ignored for already committed id");
                        continue;
                    }
                    auto it = s.staged.find(id);
                    if (it == s.staged.end())
                    {
                        return std::unexpected(not_found("staged memory not found"));
                    }
                    s.staged.erase(it);
                    s.committed.insert(id);
                }
                return {};
            }

            return std::unexpected(core::error::invalid_argument("unknown staging patch"));
        }

        core::error::result<void> load_state(stage_state& s)
        {
            if (s.loaded)
            {
                return {};
            }
            s.loaded = true;

            const auto root = memory_root();
            auto snapshot_result = load_snapshot(root);
            if (!snapshot_result)
            {
                return std::unexpected(snapshot_result.error());
            }
            auto snapshot = *snapshot_result;
            for (auto& record : snapshot.staged)
            {
                s.staged.insert_or_assign(record.id, std::move(record));
                s.next_id = std::max(s.next_id, record.id.value + 1);
            }
            for (const auto& id : snapshot.committed)
            {
                s.committed.insert(id);
            }
            s.next_id = std::max(s.next_id, snapshot.next_id);

            const auto path = root / journal_filename;
            std::error_code ec;
            if (!std::filesystem::exists(path, ec))
            {
                return {};
            }

            std::ifstream input(path, std::ios::in | std::ios::binary);
            if (!input)
            {
                return std::unexpected(core::error::make({core::error::core_domain::id, core::error::core_domain::io_error},
                    "memory staging journal read failed"));
            }

            std::string line;
            while (std::getline(input, line))
            {
                if (line.empty())
                {
                    continue;
                }
                cursor cur{line, 0};
                auto patch = parse_patch(cur);
                if (!patch)
                {
                    log_warn("memory staging journal entry malformed; skipped");
                    continue;
                }
                skip_ws(cur);
                if (cur.pos != cur.input.size())
                {
                    log_warn("memory staging journal entry trailing data ignored");
                }
                auto applied = apply_patch(s, *patch);
                if (!applied)
                {
                    log_warn(applied.error().message);
                }
            }

            return {};
        }
    }

    core::error::result<memory_id> add(std::string_view text)
    {
        if (text.empty())
        {
            return std::unexpected(core::error::invalid_argument("memory text empty"));
        }
        auto& s = state();
        auto load_result = load_state(s);
        if (!load_result)
        {
            return std::unexpected(load_result.error());
        }

        memory_record record;
        record.id = memory_id{s.next_id};
        record.text = std::string{text};
        record.created_ns = core::time::now_ns();
        record.updated_ns = record.created_ns;
        record.strength = 1.0f;
        core::asserts::that(record.id.valid(), "generated memory id invalid");

        stage_patch patch;
        patch.kind = patch_kind::add;
        patch.record = record;

        const auto root = memory_root();
        auto append_result = append_patch(root, patch);
        if (!append_result)
        {
            return std::unexpected(append_result.error());
        }
        auto applied = apply_patch(s, patch);
        if (!applied)
        {
            return std::unexpected(applied.error());
        }
        s.next_id = std::max(s.next_id, record.id.value + 1);
        core::log::info("memory.stage", "staged new memory record");
        return record.id;
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
        auto load_result = load_state(s);
        if (!load_result)
        {
            return std::unexpected(load_result.error());
        }

        auto it = s.staged.find(id);
        if (it == s.staged.end())
        {
            if (s.committed.contains(id))
            {
                return std::unexpected(already_committed("memory id already committed"));
            }
            return std::unexpected(not_found("staged memory not found"));
        }

        memory_record record = it->second;
        record.text = std::string{text};
        record.updated_ns = core::time::now_ns();

        stage_patch patch;
        patch.kind = patch_kind::edit;
        patch.record = record;

        const auto root = memory_root();
        auto append_result = append_patch(root, patch);
        if (!append_result)
        {
            return std::unexpected(append_result.error());
        }
        auto applied = apply_patch(s, patch);
        if (!applied)
        {
            return std::unexpected(applied.error());
        }
        core::log::info("memory.stage", "updated staged memory record");
        return {};
    }

    core::error::result<std::vector<memory_record>> list()
    {
        auto& s = state();
        auto load_result = load_state(s);
        if (!load_result)
        {
            return std::unexpected(load_result.error());
        }
        std::vector<memory_record> records;
        records.reserve(s.staged.size());
        for (const auto& [id, record] : s.staged)
        {
            records.push_back(record);
        }
        return records;
    }

    core::error::result<std::vector<memory_record>> commit()
    {
        auto& s = state();
        auto load_result = load_state(s);
        if (!load_result)
        {
            return std::unexpected(load_result.error());
        }
        if (s.staged.empty())
        {
            return std::vector<memory_record>{};
        }

        std::vector<memory_record> committed_records;
        std::vector<memory_id> ids;
        committed_records.reserve(s.staged.size());
        ids.reserve(s.staged.size());
        for (const auto& [id, record] : s.staged)
        {
            committed_records.push_back(record);
            ids.push_back(id);
        }

        stage_patch patch;
        patch.kind = patch_kind::commit;
        patch.ids = ids;

        const auto root = memory_root();
        auto append_result = append_patch(root, patch);
        if (!append_result)
        {
            return std::unexpected(append_result.error());
        }
        auto applied = apply_patch(s, patch);
        if (!applied)
        {
            return std::unexpected(applied.error());
        }
        auto snapshot_result = save_snapshot(root, s);
        if (!snapshot_result)
        {
            return std::unexpected(snapshot_result.error());
        }
        auto truncate_result = truncate_journal(root);
        if (!truncate_result)
        {
            return std::unexpected(truncate_result.error());
        }
        core::log::info("memory.stage", "committed staged memory records");
        return committed_records;
    }

    core::error::result<void> discard()
    {
        auto& s = state();
        auto load_result = load_state(s);
        if (!load_result)
        {
            return std::unexpected(load_result.error());
        }
        if (s.staged.empty())
        {
            return {};
        }

        std::vector<memory_id> ids;
        ids.reserve(s.staged.size());
        for (const auto& [id, record] : s.staged)
        {
            ids.push_back(id);
        }

        stage_patch patch;
        patch.kind = patch_kind::discard;
        patch.ids = ids;

        const auto root = memory_root();
        auto append_result = append_patch(root, patch);
        if (!append_result)
        {
            return std::unexpected(append_result.error());
        }
        auto applied = apply_patch(s, patch);
        if (!applied)
        {
            return std::unexpected(applied.error());
        }
        auto snapshot_result = save_snapshot(root, s);
        if (!snapshot_result)
        {
            return std::unexpected(snapshot_result.error());
        }
        auto truncate_result = truncate_journal(root);
        if (!truncate_result)
        {
            return std::unexpected(truncate_result.error());
        }
        core::log::info("memory.stage", "discarded staged memory records");
        return {};
    }
}
