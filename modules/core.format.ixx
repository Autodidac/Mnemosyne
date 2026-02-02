module;

#include <format>
#include <string>
#include <string_view>
#include <utility>

export module core.format;

export namespace core::format
{
    template <class... Args>
    [[nodiscard]] inline std::string str(std::format_string<Args...> fmt, Args&&... args)
    {
        return std::format(fmt, std::forward<Args>(args)...);
    }

    [[nodiscard]] inline std::string vstr(std::string_view fmt, std::format_args args)
    {
        return std::vformat(fmt, args);
    }

    template <class... Args>
    [[nodiscard]] inline std::string strv(std::string_view fmt, Args&&... args)
    {
        return std::vformat(fmt, std::make_format_args(std::forward<Args>(args)...));
    }
}
