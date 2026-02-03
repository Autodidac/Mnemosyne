module;

#include <optional>
#include <string>
#include <string_view>

#if defined(_WIN32)
#  include <windows.h>
#  include <vector>
#else
#  include <cstdlib>
#endif

module core.env;

namespace core::env
{
#if defined(_WIN32)
    namespace
    {
        std::wstring utf8_to_wide(std::string_view s)
        {
            if (s.empty()) return {};
            const int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
            if (needed <= 0) return {};
            std::wstring w;
            w.resize((size_t)needed);
            MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), needed);
            return w;
        }

        std::string wide_to_utf8(std::wstring_view w)
        {
            if (w.empty()) return {};
            const int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
            if (needed <= 0) return {};
            std::string s;
            s.resize((size_t)needed);
            WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), needed, nullptr, nullptr);
            return s;
        }
    }
#endif

    std::optional<std::string> get(std::string_view name)
    {
#if defined(_WIN32)
        const std::wstring wname = utf8_to_wide(name);
        if (wname.empty()) return std::nullopt;

        DWORD needed = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
        if (needed == 0) return std::nullopt;

        std::wstring buf;
        buf.resize((size_t)needed);
        const DWORD got = GetEnvironmentVariableW(wname.c_str(), buf.data(), needed);
        if (got == 0) return std::nullopt;

        // buf contains NUL terminator; trim it
        if (!buf.empty() && buf.back() == L'\0') buf.pop_back();
        return wide_to_utf8(buf);
#else
        const std::string key{name};
        if (const char* v = std::getenv(key.c_str()); v != nullptr)
            return std::string{v};
        return std::nullopt;
#endif
    }

    bool set(std::string_view name, std::string_view value)
    {
#if defined(_WIN32)
        const std::wstring wname = utf8_to_wide(name);
        const std::wstring wval = utf8_to_wide(value);
        if (wname.empty()) return false;
        return SetEnvironmentVariableW(wname.c_str(), wval.c_str()) != 0;
#else
        return ::setenv(std::string{name}.c_str(), std::string{value}.c_str(), 1) == 0;
#endif
    }

    bool unset(std::string_view name)
    {
#if defined(_WIN32)
        const std::wstring wname = utf8_to_wide(name);
        if (wname.empty()) return false;
        return SetEnvironmentVariableW(wname.c_str(), nullptr) != 0;
#else
        return ::unsetenv(std::string{name}.c_str()) == 0;
#endif
    }
}
