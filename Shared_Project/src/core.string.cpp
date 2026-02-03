module;

#include <cctype>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module core.string;

namespace core::string
{
    std::string_view trim(std::string_view s) noexcept
    {
        auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };

        std::size_t b = 0;
        while (b < s.size() && is_ws(static_cast<unsigned char>(s[b])))
            ++b;

        std::size_t e = s.size();
        while (e > b && is_ws(static_cast<unsigned char>(s[e - 1])))
            --e;

        return s.substr(b, e - b);
    }

    std::vector<std::string_view> split(std::string_view s, char delim)
    {
        std::vector<std::string_view> out;
        std::size_t start = 0;

        for (std::size_t i = 0; i <= s.size(); ++i)
        {
            if (i == s.size() || s[i] == delim)
            {
                out.emplace_back(s.substr(start, i - start));
                start = i + 1;
            }
        }
        return out;
    }

    std::string join(std::span<const std::string_view> parts, std::string_view delim)
    {
        std::string out;
        std::size_t total = 0;
        for (auto p : parts) total += p.size();
        total += (parts.size() > 0 ? (parts.size() - 1) * delim.size() : 0);
        out.reserve(total);

        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            if (i) out.append(delim);
            out.append(parts[i]);
        }
        return out;
    }
}
