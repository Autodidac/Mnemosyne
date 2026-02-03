module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

module core.memory.index;

import core.error;
import core.memory.types;
import core.memory.store;
import core.time;

namespace core::memory::index
{
    namespace
    {
        constexpr float age_normalizer_seconds = 86400.0f;

        struct memory_id_hash
        {
            std::size_t operator()(const memory_id& id) const noexcept
            {
                return std::hash<memory_id::value_type>{}(id.value);
            }
        };

        struct record_entry
        {
            memory_record record;
            std::vector<std::string> tokens;
            std::array<float, semantic_dimensions> semantic{};
        };

        struct index_state
        {
            std::unordered_map<std::string, std::vector<memory_id>> keyword_index;
            std::unordered_map<memory_id, record_entry, memory_id_hash> records;
        };

        index_state& state()
        {
            static index_state s;
            return s;
        }

        std::vector<std::string> tokenize(std::string_view text)
        {
            std::vector<std::string> tokens;
            std::string current;
            for (char ch : text)
            {
                unsigned char c = static_cast<unsigned char>(ch);
                if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
                {
                    char lowered = static_cast<char>((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c);
                    current.push_back(lowered);
                }
                else if (!current.empty())
                {
                    tokens.push_back(current);
                    current.clear();
                }
            }
            if (!current.empty())
            {
                tokens.push_back(current);
            }

            std::sort(tokens.begin(), tokens.end());
            tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
            return tokens;
        }

        std::array<float, semantic_dimensions> semantic_from_tokens(std::span<const std::string> tokens)
        {
            std::array<float, semantic_dimensions> vec{};
            std::hash<std::string_view> hasher;
            for (const auto& token : tokens)
            {
                const std::uint64_t h = static_cast<std::uint64_t>(hasher(token));
                for (int i = 0; i < 4; ++i)
                {
                    const std::uint64_t shift = static_cast<std::uint64_t>(i * 16);
                    const std::size_t idx = static_cast<std::size_t>((h >> shift) & 0xFFu);
                    const float mag = static_cast<float>((h >> (shift + 8)) & 0xFFu) / 255.0f;
                    const float value = (mag * 2.0f) - 1.0f;
                    vec[idx] += value;
                }
            }
            float norm = 0.0f;
            for (float v : vec)
            {
                norm += v * v;
            }
            if (norm > 0.0f)
            {
                const float inv = 1.0f / std::sqrt(norm);
                for (float& v : vec)
                {
                    v *= inv;
                }
            }
            return vec;
        }

        void remove_from_keyword_index(const std::vector<std::string>& tokens, memory_id id)
        {
            auto& keyword_index = state().keyword_index;
            for (const auto& token : tokens)
            {
                auto it = keyword_index.find(token);
                if (it == keyword_index.end())
                {
                    continue;
                }
                auto& ids = it->second;
                auto pos = std::lower_bound(ids.begin(), ids.end(), id,
                    [](const memory_id& lhs, const memory_id& rhs)
                    {
                        return lhs.value < rhs.value;
                    });
                if (pos != ids.end() && *pos == id)
                {
                    ids.erase(pos);
                }
            }
        }

        void add_to_keyword_index(const std::vector<std::string>& tokens, memory_id id)
        {
            auto& keyword_index = state().keyword_index;
            for (const auto& token : tokens)
            {
                auto& ids = keyword_index[token];
                auto pos = std::lower_bound(ids.begin(), ids.end(), id,
                    [](const memory_id& lhs, const memory_id& rhs)
                    {
                        return lhs.value < rhs.value;
                    });
                if (pos == ids.end() || *pos != id)
                {
                    ids.insert(pos, id);
                }
            }
        }

        void upsert_record(const memory_record& record)
        {
            auto& s = state();
            auto tokens = tokenize(record.text);
            auto semantic = semantic_from_tokens(tokens);

            auto it = s.records.find(record.id);
            if (it != s.records.end())
            {
                remove_from_keyword_index(it->second.tokens, record.id);
                it->second.record = record;
                it->second.tokens = std::move(tokens);
                it->second.semantic = semantic;
            }
            else
            {
                record_entry entry;
                entry.record = record;
                entry.tokens = std::move(tokens);
                entry.semantic = semantic;
                s.records.emplace(record.id, std::move(entry));
            }

            const auto& stored_tokens = s.records.find(record.id)->second.tokens;
            add_to_keyword_index(stored_tokens, record.id);
        }

        float keyword_overlap(std::span<const std::string> query_tokens,
                              std::span<const std::string> record_tokens)
        {
            if (query_tokens.empty())
            {
                return 0.0f;
            }
            std::size_t matches = 0;
            for (const auto& token : query_tokens)
            {
                if (std::binary_search(record_tokens.begin(), record_tokens.end(), token))
                {
                    ++matches;
                }
            }
            return static_cast<float>(matches) / static_cast<float>(query_tokens.size());
        }

        float cosine_similarity(const std::array<float, semantic_dimensions>& a,
                                const std::array<float, semantic_dimensions>& b)
        {
            float dot = 0.0f;
            for (std::size_t i = 0; i < semantic_dimensions; ++i)
            {
                dot += a[i] * b[i];
            }
            return dot;
        }
    }

    core::error::result<void> build_from_snapshot(const core::memory::store::memory_snapshot& snapshot)
    {
        auto& s = state();
        s.keyword_index.clear();
        s.records.clear();
        for (const auto& record : snapshot.records)
        {
            upsert_record(record);
        }
        return {};
    }

    core::error::result<void> update_on_commit(const std::vector<memory_record>& committed)
    {
        for (const auto& record : committed)
        {
            upsert_record(record);
        }
        return {};
    }

    core::error::result<std::vector<memory_result>> query(const memory_query& query)
    {
        auto& s = state();
        std::vector<memory_result> results;
        if (query.limit == 0)
        {
            return results;
        }

        const auto query_tokens = tokenize(query.text);
        const auto query_vector = semantic_from_tokens(query_tokens);

        std::unordered_set<memory_id, memory_id_hash> candidates;
        if (query_tokens.empty())
        {
            candidates.reserve(s.records.size());
            for (const auto& [id, entry] : s.records)
            {
                candidates.insert(id);
            }
        }
        else
        {
            for (const auto& token : query_tokens)
            {
                auto it = s.keyword_index.find(token);
                if (it == s.keyword_index.end())
                {
                    continue;
                }
                for (const auto& id : it->second)
                {
                    candidates.insert(id);
                }
            }
        }

        results.reserve(std::min<std::size_t>(query.limit, candidates.size()));
        const std::uint64_t now = core::time::now_ns();
        for (const auto& id : candidates)
        {
            auto it = s.records.find(id);
            if (it == s.records.end())
            {
                continue;
            }
            const auto& entry = it->second;
            const float overlap = keyword_overlap(query_tokens, entry.tokens);
            const float cosine = cosine_similarity(query_vector, entry.semantic);
            const float confidence = std::max(0.0f, entry.record.strength);
            const float age_seconds = (now > entry.record.updated_ns)
                ? static_cast<float>(now - entry.record.updated_ns) / 1'000'000'000.0f
                : 0.0f;
            const float age_penalty = age_seconds / age_normalizer_seconds;

            memory_result result;
            result.record = entry.record;
            result.score = (w_keyword * overlap)
                + (w_semantic * cosine)
                + (w_confidence * confidence)
                - (w_age * age_penalty);
            results.push_back(result);
        }

        std::sort(results.begin(), results.end(),
            [](const memory_result& lhs, const memory_result& rhs)
            {
                if (lhs.score == rhs.score)
                {
                    return lhs.record.id.value < rhs.record.id.value;
                }
                return lhs.score > rhs.score;
            });

        if (results.size() > query.limit)
        {
            results.resize(query.limit);
        }
        return results;
    }
}
