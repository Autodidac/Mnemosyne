/**************************************************************
 *   █████╗ ██╗     ███╗   ███╗   ███╗   ██╗    ██╗██████╗    *
 *  ██╔══██╗██║     ████╗ ████║ ██╔═══██╗████╗  ██║██╔══██╗   *
 *  ███████║██║     ██╔████╔██║ ██║   ██║██╔██╗ ██║██║  ██║   *
 *  ██╔══██║██║     ██║╚██╔╝██║ ██║   ██║██║╚██╗██║██║  ██║   *
 *  ██║  ██║███████╗██║ ╚═╝ ██║ ╚██████╔╝██║ ╚████║██████╔╝   *
 *  ╚═╝  ╚═╝╚══════╝╚═╝     ╚═╝  ╚═════╝ ╚═╝  ╚═══╝╚═════╝    *
 *                                                            *
 *   This file is part of the Almond Project.                 *
 *   AlmondShell - Modular C++ Framework                      *
 *                                                            *
 *   SPDX-License-Identifier: LicenseRef-MIT-NoSell           *
 *                                                            *
 *   Provided "AS IS", without warranty of any kind.          *
 *   Use permitted for Non-Commercial Purposes ONLY,          *
 *   without prior commercial licensing agreement.            *
 *                                                            *
 *   Redistribution Allowed with This Notice and              *
 *   LICENSE file. No obligation to disclose modifications.   *
 *                                                            *
 *   See LICENSE file for full terms.                         *
 *                                                            *
 **************************************************************/
 //
 // aengine.cppm (converted from legacy aengine.cpp)
 //
 // FIXES APPLIED:
 //  - No direct access to core::Context private members (ctx->hwnd).
 //    We only query windows via MultiContextManager APIs.
 //  - Removed non-constant switch case labels for ContextType::Unknown/Noop
 //    because your ContextType in your current modules is not an enum with those
 //    exact enumerators (or they’re not visible here). Default handles it.
 //
//#include "pch.h"

#include "..\include\aengine.config.hpp"
#include "..\include\aengine.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

// -----------------------------
// Standard library imports
// -----------------------------
import <algorithm>;
import <chrono>;
//import <exception>;
import <format>;
import <iostream>;
import <memory>;
import <mutex>;
import <optional>;
import <queue>;
import <shared_mutex>;
import <string>;
import <string_view>;
import <thread>;
import <unordered_map>;
import <utility>;
import <vector>;

// -----------------------------
// Engine/module imports
// -----------------------------
import aengine.platform;
//import aengine.config;

import almondshell;

import aengine.cli;
import aengine.version;
import aengine.updater;
import aengine.input;
import aengine.engine_components;

import aengine.context.multiplexer;
import aengine.context.type;
import aengine.core.context;

import aengine.gui;
import aengine.gui.menu;
import aeditor;

import ascene;

import asnakelike;
import atetrislike;
import apacmanlike;
import afroggerlike;
import asokobanlike;
import amatch3like;

import aslidingpuzzlelike;
import aminesweeperlike;
import a2048like;

import asandsim;
import acellularsim;

#if defined(ALMOND_USING_OPENGL)
import acontext.opengl.context;
#endif
#if defined(ALMOND_USING_SOFTWARE_RENDERER)
import acontext.softrenderer.context;
#endif
#if defined(ALMOND_USING_SDL)
import acontext.sdl.context;
#endif
#if defined(ALMOND_USING_SFML)
import acontext.sfml.context;
#endif
#if defined(ALMOND_USING_RAYLIB)
import acontext.raylib.context;
#endif

namespace input = almondnamespace::input;
namespace menu = almondnamespace::menu;
namespace gui = almondnamespace::gui;

namespace almondnamespace::core
{
    void RunEngine();
    void StartEngine();
    void RunEditorInterface();

    struct TextureUploadTask
    {
        int w{};
        int h{};
        const void* pixels{};
    };

    struct TextureUploadQueue
    {
        std::queue<TextureUploadTask> tasks;
        std::mutex mtx;

        void push(TextureUploadTask&& task)
        {
            std::lock_guard lock(mtx);
            tasks.push(std::move(task));
        }

        std::optional<TextureUploadTask> try_pop()
        {
            std::lock_guard lock(mtx);
            if (tasks.empty()) return {};
            auto task = tasks.front();
            tasks.pop();
            return task;
        }
    };

    inline std::vector<std::unique_ptr<TextureUploadQueue>> uploadQueues;

#if defined(_WIN32)
    inline void ShowConsole()
    {
#if defined(_DEBUG)
        AllocConsole();
        FILE* fp = nullptr;
        freopen_s(&fp, "CONIN$", "r", stdin);
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
#else
        FreeConsole();
#endif
    }
#endif

    namespace engine
    {
        template <typename PumpFunc>
        int RunEditorInterfaceLoop(MultiContextManager& mgr, PumpFunc&& pump_events)
        {
            // Editor loop only (no legacy games/menu overlays).

            auto collect_backend_contexts = []()
                {
                    using ContextGroup = std::pair<
                        almondnamespace::core::ContextType,
                        std::vector<std::shared_ptr<almondnamespace::core::Context>>
                    >;

                    std::vector<ContextGroup> snapshot;

                    {
                        std::shared_lock lock(almondnamespace::core::g_backendsMutex);
                        snapshot.reserve(almondnamespace::core::g_backends.size());

                        for (auto& [type, state] : almondnamespace::core::g_backends)
                        {
                            std::vector<std::shared_ptr<almondnamespace::core::Context>> contexts;
                            contexts.reserve(1 + state.duplicates.size());

                            if (state.master) contexts.push_back(state.master);
                            for (auto& dup : state.duplicates) contexts.push_back(dup);

                            snapshot.emplace_back(type, std::move(contexts));
                        }
                    }

                    return snapshot;
                };

            std::unordered_map<Context*, std::chrono::steady_clock::time_point> last_frame_times;
            bool running = true;
            auto pump = std::forward<PumpFunc>(pump_events);

            while (running)
            {
                if (!pump())
                {
                    running = false;
                    break;
                }

                mgr.CleanupFinishedWindows();

                auto snapshot = collect_backend_contexts();
#if !defined(ALMOND_SINGLE_PARENT)
                bool any_context_alive = false;
#endif
                for (auto& [type, contexts] : snapshot)
                {
                    auto update_on_ctx = [&](std::shared_ptr<Context> ctx) -> bool
                        {
                            if (!ctx) return true;

                            auto* win = mgr.findWindowByContext(ctx);
                            if (!win) return true;

                            bool ctx_running = win->running;

                            const auto now = std::chrono::steady_clock::now();
                            const auto raw = ctx.get();

                            float dt = 0.0f;
                            auto [it, inserted] = last_frame_times.emplace(raw, now);
                            if (!inserted)
                            {
                                dt = std::chrono::duration<float>(now - it->second).count();
                                it->second = now;
                            }

                            // Editor UI generation only. Rendering happens inside ctx->process (per backend).

                            int mx = 0, my = 0;
                            ctx->get_mouse_position_safe(mx, my);

                            const gui::Vec2 mouse_pos{ static_cast<float>(mx), static_cast<float>(my) };

                            const bool mouse_left_down =
                                almondnamespace::input::mouseDown.test(almondnamespace::input::MouseButton::MouseLeft);

                            // IMPORTANT:
                            //  - Do not call ctx->clear_safe() / ctx->present_safe() here.
                            //  - GUI drawing is enqueued into ctx->windowData->commandQueue and must execute on the
                            //    backend's render pass (OpenGL/Vulkan/SFML/SDL/software) inside ctx->process().
                            gui::begin_frame(ctx, dt, mouse_pos, mouse_left_down);

                            // Unreal-ish editor layout (docked panels + viewport).
                            (void)almondnamespace::editor_run(ctx, nullptr);

                            gui::end_frame();

                            if (!ctx_running)
                                last_frame_times.erase(raw);

                            return ctx_running;
                        };

#if defined(ALMOND_SINGLE_PARENT)
                    if (!contexts.empty())
                    {
                        auto master = contexts.front();
                        if (master && !update_on_ctx(master)) { running = false; break; }

                        for (std::size_t i = 1; i < contexts.size(); ++i)
                            if (!update_on_ctx(contexts[i])) running = false;
                    }
#else
                    bool any_alive = false;
                    for (std::size_t i = 0; i < contexts.size(); ++i)
                    {
                        auto& ctx = contexts[i];
                        if (!ctx) continue;

                        const bool alive = update_on_ctx(ctx);
                        if (alive) any_alive = true;
                    }
                    if (any_alive) any_context_alive = true;
#endif
                    if (!running) break;
                }
#if !defined(ALMOND_SINGLE_PARENT)
                if (!any_context_alive) running = false;
#endif

                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }

            auto snapshot2 = collect_backend_contexts();
            for (auto& [type, contexts] : snapshot2)
            {
                auto cleanup_backend = [&](std::shared_ptr<almondnamespace::core::Context> ctx)
                    {
                        if (!ctx) return;

                        switch (type)
                        {
#if defined(ALMOND_USING_OPENGL)
                        case almondnamespace::core::ContextType::OpenGL:
                            almondnamespace::openglcontext::opengl_cleanup(ctx);
                            break;
#endif
#if defined(ALMOND_USING_SOFTWARE_RENDERER)
                        case almondnamespace::core::ContextType::Software:
                            // almondnamespace::anativecontext::softrenderer_cleanup(ctx);
                            break;
#endif
#if defined(ALMOND_USING_SDL)
                        case almondnamespace::core::ContextType::SDL:
                            //  almondnamespace::sdlcontext::sdl_cleanup(ctx);
                            break;
#endif
#if defined(ALMOND_USING_SFML)
                        case almondnamespace::core::ContextType::SFML:
                            almondnamespace::sfmlcontext::sfml_cleanup(ctx);
                            break;
#endif
#if defined(ALMOND_USING_RAYLIB)
                        case almondnamespace::core::ContextType::RayLib:
                            almondnamespace::raylibcontext::raylib_cleanup(ctx);
                            break;
#endif
                        case almondnamespace::core::ContextType::Noop:
                            break;
                        default:
                            break;
                        }
                    };

                for (auto& ctx : contexts) cleanup_backend(ctx);
            }

            mgr.StopAll();

            return 0;
        }

        template <typename PumpFunc>
        int RunEngineMainLoopCommon(MultiContextManager& mgr, PumpFunc&& pump_events)
        {
            (void)mgr;
            // Always run the editor loop; the legacy menu/game selection loop has been removed.
            return RunEditorInterfaceLoop(mgr, std::forward<PumpFunc>(pump_events));
        }

#if defined(_WIN32)
        int RunEngineMainLoopInternal(HINSTANCE hInstance, int nCmdShow)
        {
            UNREFERENCED_PARAMETER(nCmdShow);

            try
            {
                almondnamespace::core::MultiContextManager mgr;

                HINSTANCE hi = hInstance ? hInstance : GetModuleHandleW(nullptr);

                const bool ok = mgr.Initialize(
                    hi,
                    /*RayLib*/   1,
                    /*SDL*/      1,
                    /*SFML*/     1,
                    /*Vulkan*/   1,
                    /*OpenGL*/   1,
                    /*Software*/ 1,
                    ALMOND_SINGLE_PARENT == 1
                );

                if (!ok)
                {
                    //MessageBoxW(nullptr, L"Failed to initialize contexts!", L"Error", MB_ICONERROR | MB_OK);
                    return -1;
                }

                input::designate_polling_thread_to_current();

                mgr.StartRenderThreads();
                mgr.ArrangeDockedWindowsGrid();

                auto pump = []() -> bool
                    {
                        MSG msg{};
                        bool keep = true;

                        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                        {
                            if (msg.message == WM_QUIT) keep = false;
                            else
                            {
                                TranslateMessage(&msg);
                                DispatchMessageW(&msg);
                            }
                        }

                        if (!keep) return false;

                        input::poll_input();
                        return true;
                    };

                return engine::RunEngineMainLoopCommon(mgr, pump);
            }
            catch (const std::exception& ex)
            {
                MessageBoxA(nullptr, ex.what(), "Error", MB_ICONERROR | MB_OK);
                return -1;
            }
        }
#elif defined(__linux__)
        int RunEngineMainLoopLinux()
        {
            try
            {
                almondnamespace::core::MultiContextManager mgr;

                const bool ok = mgr.Initialize(
                    nullptr,
                    /*RayLib*/   1,
                    /*SDL*/      1,
                    /*SFML*/     1,
                    /*Vulkan*/   1,
                    /*OpenGL*/   1,
                    /*Software*/ 1,
                    ALMOND_SINGLE_PARENT == 1
                );

                if (!ok)
                {
                    std::cerr << "[Engine] Failed to initialize contexts!\n";
                    return -1;
                }

                input::designate_polling_thread_to_current();

                mgr.StartRenderThreads();
                mgr.ArrangeDockedWindowsGrid();

                auto pump = []() -> bool
                    {
                        return almondnamespace::platform::pump_events();
                    };

                return RunEngineMainLoopCommon(mgr, pump);
            }
            catch (const std::exception& ex)
            {
                std::cerr << "[Engine] " << ex.what() << '\n';
                return -1;
            }
        }
#endif
    } // anonymous namespace

    void RunEngine()
    {
#if defined(_WIN32)
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        const int result = engine::RunEngineMainLoopInternal(instance, SW_SHOWNORMAL);
        if (result != 0)
            std::cerr << "[Engine] RunEngine terminated with code " << result << "\n";
#elif defined(__linux__)
        const int result = RunEngineMainLoopLinux();
        if (result != 0)
            std::cerr << "[Engine] RunEngine terminated with code " << result << "\n";
#else
        std::cerr << "[Engine] RunEngine is not implemented for this platform yet.\n";
#endif
    }

    void StartEngine()
    {
        std::cout << "AlmondShell Engine v" << almondnamespace::GetEngineVersion() << '\n';
        RunEngine();
    }

    void RunEditorInterface()
    {
#if defined(_WIN32)
        try
        {
            almondnamespace::core::MultiContextManager mgr;

            const HINSTANCE hi = GetModuleHandleW(nullptr);

            const bool ok = mgr.Initialize(
                hi,
                /*RayLib*/   1,
                /*SDL*/      1,
                /*SFML*/     1,
                    /*Vulkan*/   1,
                /*OpenGL*/   1,
                /*Software*/ 1,
                ALMOND_SINGLE_PARENT == 1
            );

            if (!ok)
            {
                std::cerr << "[ Editor ] -  Failed to initialize contexts!\n";
                return;
            }

            input::designate_polling_thread_to_current();

            mgr.StartRenderThreads();
            mgr.ArrangeDockedWindowsGrid();

            auto pump = []() -> bool
                {
                    MSG msg{};
                    bool keep = true;

                    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                    {
                        if (msg.message == WM_QUIT) keep = false;
                        else
                        {
                            TranslateMessage(&msg);
                            DispatchMessageW(&msg);
                        }
                    }

                    if (!keep) return false;

                    input::poll_input();
                    return true;
                };

            const int result = engine::RunEditorInterfaceLoop(mgr, pump);
            if (result != 0)
                std::cerr << "[ Editor ] -  RunEditorInterface terminated with code " << result << "\n";
        }
        catch (const std::exception& ex)
        {
            MessageBoxA(nullptr, ex.what(), "Error", MB_ICONERROR | MB_OK);
        }
#elif defined(__linux__)
        try
        {
            almondnamespace::core::MultiContextManager mgr;

            const bool ok = mgr.Initialize(
                nullptr,
                /*RayLib*/   1,
                /*SDL*/      1,
                /*SFML*/     1,
                    /*Vulkan*/   1,
                /*OpenGL*/   1,
                /*Software*/ 1,
                ALMOND_SINGLE_PARENT == 1
            );

            if (!ok)
            {
                std::cerr << "[ Editor ] -  Failed to initialize contexts!\n";
                return;
            }

            input::designate_polling_thread_to_current();

            mgr.StartRenderThreads();
            mgr.ArrangeDockedWindowsGrid();

            auto pump = []() -> bool
                {
                    return almondnamespace::platform::pump_events();
                };

            const int result = engine::RunEditorInterfaceLoop(mgr, pump);
            if (result != 0)
                std::cerr << "[ Editor ] -  RunEditorInterface terminated with code " << result << "\n";
        }
        catch (const std::exception& ex)
        {
            std::cerr << "[ Editor ] -  " << ex.what() << '\n';
        }
#else
        std::cerr << "[ Editor ] -  RunEditorInterface is not implemented for this platform yet.\n";
#endif
    }
} // namespace almondnamespace::core

namespace urls
{
    const std::string github_base = "https://github.com/";
    const std::string github_raw_base = "https://raw.githubusercontent.com/";

    const std::string owner = "Autodidac/";
    const std::string repo = "Cpp_Ultimate_Project_Updater";
    const std::string branch = "main/";

    const std::string version_url = github_raw_base + owner + repo + "/" + branch + "/modules/aengine.version.ixx";
    const std::string binary_url = github_base + owner + repo + "/releases/latest/download/ConsoleApplication1.exe";
}

#if defined(_WIN32) && defined(ALMOND_USING_WINMAIN)
int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_     LPWSTR    lpCmdLine,
    _In_     int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

#if defined(_DEBUG)
    almondnamespace::core::ShowConsole();
#endif

    try
    {
        const int argc = __argc;
        char** argv = __argv;

        const auto cli_result = almondnamespace::core::cli::parse(argc, argv);

        const almondnamespace::updater::UpdateChannel channel{
            .version_url = urls::version_url,
            .binary_url = urls::binary_url,
        };

        if (cli_result.update_requested)
        {
            const auto update_result =
                almondnamespace::updater::run_update_command(channel, cli_result.force_update);

            if (update_result.force_required && !cli_result.force_update)
                return 2;

            return 0;
        }

        if (cli_result.editor_requested)
        {
            almondnamespace::core::RunEditorInterface();
            return 0;
        }

        return almondnamespace::core::engine::RunEngineMainLoopInternal(hInstance, SW_SHOWNORMAL);
    }
    catch (const std::exception& ex)
    {
        MessageBoxA(nullptr, ex.what(), "Error", MB_ICONERROR | MB_OK);
        return -1;
    }
}
#endif

int main(int argc, char** argv)
{
#if defined(_WIN32) && defined(ALMOND_USING_WINMAIN)
    return wWinMain(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), SW_SHOWNORMAL);
#else
    try
    {
        const auto cli_result = almondnamespace::core::cli::parse(argc, argv);

        const almondnamespace::updater::UpdateChannel channel{
            .version_url = urls::version_url,
            .binary_url = urls::binary_url,
        };

        if (cli_result.update_requested)
        {
            const auto update_result =
                almondnamespace::updater::run_update_command(channel, cli_result.force_update);

            if (update_result.force_required && !cli_result.force_update)
                return 2;

            return 0;
        }

        if (cli_result.editor_requested)
        {
            almondnamespace::core::RunEditorInterface();
            return 0;
        }

        almondnamespace::core::StartEngine();
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[Fatal] " << ex.what() << '\n';
        return -1;
    }
#endif
}