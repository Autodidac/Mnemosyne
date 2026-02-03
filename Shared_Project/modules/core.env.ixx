module;

#include <optional>
#include <string>
#include <string_view>

export module core.env;

export namespace core::env
{
    // Returns the value of an environment variable if present.
    std::optional<std::string> get(std::string_view name);

    // Sets an environment variable for the current process.
    // Returns true on success.
    bool set(std::string_view name, std::string_view value);

    // Unsets an environment variable for the current process.
    // Returns true on success.
    bool unset(std::string_view name);
}
