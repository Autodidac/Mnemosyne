module;

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

module core.memory.store;

import core.error;
import core.log;
import core.memory;

namespace core::memory::store
{
    namespace
    {
        constexpr std::string_view snapshot_filename = "memory.snapshot.json";
        constexpr std::string_view snapshot_tmp_filename = "memory.snapshot.json.tmp";
        constexpr std::string_view journal_filename = "memory.journal.jsonl";

        struct cursor
        {
            std::string_view input;
            std::size_t pos = 0;
        };

        void log_warn(std::string_view msg)
        {
            core::log::warn("memory.store", msg);
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

        std::vector<memory_record> sorted_records(const std::vector<memory_record>& records)
        {
            std::vector<memory_record> sorted = records;
            std::sort(sorted.begin(), sorted.end(),
                [](const memory_record& lhs, const memory_record& rhs)
                {
                    return lhs.id.value < rhs.id.value;
                });
            return sorted;
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

        std::optional<memory_snapshot> parse_snapshot(cursor& cur)
        {
            if (!consume(cur, '{'))
            {
                return std::nullopt;
            }
            std::optional<std::uint64_t> next_id;
            std::vector<memory_record> records;

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
                else if (*key == "records")
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
                                records.push_back(std::move(*record));
                                cur = attempt;
                            }
                            else
                            {
                                log_warn("memory snapshot record malformed; skipped");
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

            memory_snapshot snapshot;
            if (next_id)
            {
                snapshot.next_id = *next_id;
            }
            else
            {
                log_warn("memory snapshot missing next_id; defaulting to 1");
                snapshot.next_id = 1;
            }
            snapshot.records = std::move(records);
            return snapshot;
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
                    "memory store directory create failed"));
            }
            return {};
        }
    }

    core::error::result<memory_snapshot> load_snapshot(const std::filesystem::path& root)
    {
        const auto path = root / snapshot_filename;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            memory_snapshot snapshot;
            snapshot.next_id = 1;
            return snapshot;
        }

        auto data = read_file(path);
        if (!data)
        {
            return std::unexpected(core::error::make({core::error::core_domain::id, core::error::core_domain::io_error},
                "memory snapshot read failed"));
        }

        cursor cur{*data, 0};
        auto parsed = parse_snapshot(cur);
        if (!parsed)
        {
            log_warn("memory snapshot malformed; using empty snapshot");
            memory_snapshot snapshot;
            snapshot.next_id = 1;
            return snapshot;
        }
        skip_ws(cur);
        if (cur.pos != cur.input.size())
        {
            log_warn("memory snapshot trailing data ignored");
        }
        return *parsed;
    }

    core::error::result<void> save_snapshot(const std::filesystem::path& root, const memory_snapshot& snapshot)
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
                "memory snapshot write failed"));
        }

        output << '{';
        output << "\"next_id\":" << snapshot.next_id << ',';
        output << "\"records\":[";
        auto sorted = sorted_records(snapshot.records);
        for (std::size_t i = 0; i < sorted.size(); ++i)
        {
            if (i > 0)
            {
                output << ',';
            }
            write_record(output, sorted[i]);
        }
        output << "]}";
        output.flush();
        output.close();

        std::error_code ec;
        std::filesystem::rename(temp_path, final_path, ec);
        if (ec)
        {
            return std::unexpected(core::error::make({core::error::core_domain::id, core::error::core_domain::io_error},
                "memory snapshot rename failed"));
        }
        return {};
    }

    core::error::result<void> append_journal(const std::filesystem::path& root,
                                             const std::vector<memory_record>& records)
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
                "memory journal append failed"));
        }

        auto sorted = sorted_records(records);
        for (const auto& record : sorted)
        {
            write_record(output, record);
            output << '\n';
            output.flush();
        }
        return {};
    }

    core::error::result<memory_snapshot> rebuild_state(const std::filesystem::path& root)
    {
        auto snapshot_result = load_snapshot(root);
        if (!snapshot_result)
        {
            return std::unexpected(snapshot_result.error());
        }
        memory_snapshot snapshot = *snapshot_result;

        const auto path = root / journal_filename;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            return snapshot;
        }

        std::ifstream input(path, std::ios::in | std::ios::binary);
        if (!input)
        {
            return std::unexpected(core::error::make({core::error::core_domain::id, core::error::core_domain::io_error},
                "memory journal read failed"));
        }

        std::string line;
        while (std::getline(input, line))
        {
            if (line.empty())
            {
                continue;
            }
            cursor cur{line, 0};
            auto record = parse_record(cur);
            if (!record)
            {
                log_warn("memory journal entry malformed; skipped");
                continue;
            }
            skip_ws(cur);
            if (cur.pos != cur.input.size())
            {
                log_warn("memory journal entry trailing data ignored");
            }
            snapshot.records.push_back(std::move(*record));
            snapshot.next_id = std::max(snapshot.next_id, snapshot.records.back().id.value + 1);
        }

        return snapshot;
    }
}
