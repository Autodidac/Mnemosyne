module;

#include <string>
#include <string_view>
#include <vector>
#include <span>

export module core.string;

export namespace core::string
{
    // Returns a view with leading/trailing ASCII whitespace removed.
    std::string_view trim(std::string_view s) noexcept;

    // Splits on a single character delimiter. Empty tokens are preserved.
    std::vector<std::string_view> split(std::string_view s, char delim);

    // Joins views with a delimiter into a new string.
    std::string join(std::span<const std::string_view> parts, std::string_view delim);
}
