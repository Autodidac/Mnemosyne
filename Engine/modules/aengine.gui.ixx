module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <span>

export module aengine.gui;

import aengine.core.context;
import aspritehandle;

export namespace almondnamespace::gui
{
    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct WidgetBounds
    {
        Vec2 position{};
        Vec2 size{};
    };

    enum class EventType : std::uint8_t
    {
        None = 0,
        MouseMove,
        MouseDown,
        MouseUp,
        TextInput,
        KeyDown,
        KeyUp
    };

    struct InputEvent
    {
        EventType type = EventType::None;
        Vec2 mouse_pos{};
        std::uint32_t key = 0;          // platform virtual key / scancode as provided by caller
        std::string text{};             // UTF-8 (best-effort); engine currently treats as bytes for ASCII UI
    };

    struct EditBoxResult
    {
        bool active = false;
        bool changed = false;
        bool submitted = false;
    };

    struct ConsoleWindowOptions
    {
        std::string_view title{};
        Vec2 position{};
        Vec2 size{};

        // Log lines
        std::span<const std::string> lines{};
        std::size_t max_visible_lines = 64;

        // Input
        std::string* input = nullptr;      // optional (legacy)
        std::size_t max_input_chars = 1024;
        bool multiline_input = false;
    };

    struct ConsoleWindowResult
    {
        EditBoxResult input{};
    };

    // Cursor/layout
    export void set_cursor(Vec2 position) noexcept;
    export void advance_cursor(Vec2 delta) noexcept;

    // Input
    export void push_input(const InputEvent& e) noexcept;

    // Frame
    export void begin_frame(const std::shared_ptr<core::Context>& ctx, float dt, Vec2 mouse_pos, bool mouse_down) noexcept;
    export void end_frame() noexcept;

    // Window
    export void begin_window(std::string_view title, Vec2 position, Vec2 size) noexcept;
    export void end_window() noexcept;

    // Widgets
    export bool button(std::string_view label, Vec2 size = {}) noexcept;
    export bool image_button(const SpriteHandle& sprite, Vec2 size) noexcept;
    export std::optional<WidgetBounds> last_button_bounds() noexcept;

    export float line_height() noexcept;
    export float glyph_width() noexcept;

    export void label(std::string_view text) noexcept;

    export EditBoxResult edit_box(std::string& text, Vec2 size = {}, std::size_t max_chars = 0, bool multiline = false) noexcept;
    export void text_box(std::string_view text, Vec2 size = {}) noexcept;

    export ConsoleWindowResult console_window(const ConsoleWindowOptions& options, std::string& input);
}
