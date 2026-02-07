//// acontext.vulkan.platform.context.cpp
//
// This file MUST be a module implementation unit for `acontext.vulkan.context`
// because it defines `almondnamespace::vulkancontext::Application` methods.
//
// It also MUST be the single TU that provides:
//  - STB_IMAGE_IMPLEMENTATION
//  - Vulkan-Hpp dynamic dispatch storage (see acontext.vulkan.dispatch_storage.cpp)
//
// If you keep vulkan.hpp in the module interface BMI, the safest way to guarantee
// the dispatch storage exists is to include vulkan.hpp textually here (with the
// same config macros) before `module acontext.vulkan.context;`.
//
// -----------------------------------------------------------------------------
//
// NOTE: No `import acontext.vulkan.context;` here. This file *is* that module.
//

module;

// ---- One-TU-only implementations / storage ---------------------------------
#ifndef STB_IMAGE_IMPLEMENTATION
#   define STB_IMAGE_IMPLEMENTATION
#endif

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#   define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#endif
#ifndef VULKAN_HPP_NO_EXCEPTIONS
#   define VULKAN_HPP_NO_EXCEPTIONS
#endif

#include <include/aengine.config.hpp>

#if defined(_WIN32)
#   ifndef VK_USE_PLATFORM_WIN32_KHR
#       define VK_USE_PLATFORM_WIN32_KHR
#   endif
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#endif

#if defined(ALMOND_USING_OPENGL)
#   include <glad/glad.h>
#endif

#if defined(ALMOND_VULKAN_STANDALONE)
#   ifndef GLFW_INCLUDE_VULKAN
#       define GLFW_INCLUDE_VULKAN
#   endif
#   include <GLFW/glfw3.h>
#endif

#include <vulkan/vulkan.hpp>

// STB must be included after its implementation define:
#include "src/stb/stb_image.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

// -----------------------------------------------------------------------------
// Now enter the named module (implementation unit)
// -----------------------------------------------------------------------------
module acontext.vulkan.context;

import :window;
import :instance;
import :device;
import :swapchain;
import :shader_pipeline;
import :depth;
import :memory;
import :texture;
import :descriptor;
import :commands;

import aengine.context.commandqueue;
import aengine.core.context;
import aengine.input;
import :shared_vk;

#if defined(ALMOND_USING_OPENGL)
import acontext.opengl.platform;
#endif

// -----------------------------------------------------------------------------
// Correct namespace for the exported type declared in the interface:
// export namespace almondnamespace::vulkancontext { export class Application ... }
// -----------------------------------------------------------------------------
namespace almondnamespace::vulkancontext
{
    void Application::run()
    {
        almondnamespace::core::CommandQueue queue;
        initWindow();
        initVulkan();

#ifdef _DEBUG
        std::cout << "Vertex struct size: " << sizeof(Vertex) << "\n";
        std::cout << "Position offset: " << offsetof(Vertex, pos) << "\n";
        std::cout << "Normal offset: " << offsetof(Vertex, normal) << "\n";
        std::cout << "TexCoord offset: " << offsetof(Vertex, texCoord) << "\n";
#endif

        while (process(nullptr, queue)) {}
        cleanup();
    }

    void Application::initVulkan()
    {
#ifdef _DEBUG
        std::cout << "initVulkan() called\n";
#endif
        createInstance();
        createSurface();

        if (validationLayersEnabled)
        {
            // setupDebugMessenger(); // if you implement it
        }

        physicalDevice = pickPhysicalDevice();
        if (!physicalDevice)
            throw std::runtime_error("Failed to pick a physical device!");

        queueFamilyIndices = findQueueFamilies(physicalDevice);

        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createRenderPass();
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createGuiPipeline();
        createCommandPool();
        createDepthResources();
        createFramebuffers();
        createTextureImage();
        createTextureImageView();
        createTextureSampler();
        createVertexBuffer();
        createIndexBuffer();
        createUniformBuffers();
        createGuiUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();
    }

    void Application::initWindow()
    {
#if defined(ALMOND_VULKAN_STANDALONE)
        if (!glfwInit())
            throw std::runtime_error("Failed to initialize GLFW");

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        window = glfwCreateWindow(framebufferWidth, framebufferHeight, "Vulkan Cube", nullptr, nullptr);
        if (!window)
            throw std::runtime_error("Failed to create GLFW window");

        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
        glfwSetCursorPosCallback(window, mouseCallback);

        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
#else
        window = nullptr;
        if (!nativeWindowHandle)
            throw std::runtime_error("Vulkan requires a native window handle.");
#endif

        // IMPORTANT: with VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE defined
        // in THIS TU, this init call is valid and links cleanly.
        VULKAN_HPP_DEFAULT_DISPATCHER.init();

#ifdef _DEBUG
        std::cout << "Window configured\n";
#endif
    }

    void Application::framebufferResizeCallback(GLFWwindow* window_, int width, int height)
    {
#if defined(ALMOND_VULKAN_STANDALONE)
        auto* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window_));
        if (!app)
            return;

        int fbWidth = width;
        int fbHeight = height;
        if (fbWidth == 0 || fbHeight == 0)
            glfwGetFramebufferSize(window_, &fbWidth, &fbHeight);

        app->set_framebuffer_size(fbWidth, fbHeight);
#else
        (void)window_;
        (void)width;
        (void)height;
#endif
    }

    // NOTE:
    //   Application::process(...) is implemented in the module partition
    //   `modules/acontext.vulkan.context-runtime.ixx`.
    //
    //   This TU historically carried a copy, but that causes ODR/linker conflicts
    //   when both the module and this TU are part of the same static lib.
    //
    //   If you ever need the legacy TU-local implementation (standalone experiments),
    //   define ALMOND_VULKAN_DEFINE_PROCESS_IN_PLATFORM_TU and re-introduce it.
#if defined(ALMOND_VULKAN_DEFINE_PROCESS_IN_PLATFORM_TU)
    bool Application::process(std::shared_ptr<almondnamespace::core::Context> ctx,
        almondnamespace::core::CommandQueue& queue)
    {
        // Intentionally not compiled by default.
        (void)ctx;
        (void)queue;
        return false;
    }
#endif

    void Application::set_context(std::shared_ptr<almondnamespace::core::Context> ctx, void* nativeWindow)
    {
        context = std::move(ctx);
        nativeWindowHandle = nativeWindow;
        activeGuiContext = context.lock().get();
    }

    void Application::set_active_context(const almondnamespace::core::Context* ctx)
    {
        activeGuiContext = ctx;
    }

    void Application::cleanup_gui_context(const almondnamespace::core::Context* ctx)
    {
        if (!ctx)
            return;

        guiContexts.erase(ctx);
        if (activeGuiContext == ctx)
            activeGuiContext = nullptr;
    }

    Application::GuiContextState& Application::gui_state_for_context(
        const almondnamespace::core::Context* ctx)
    {
        return guiContexts[ctx];
    }

    Application::GuiContextState* Application::find_gui_state(
        const almondnamespace::core::Context* ctx) noexcept
    {
        auto it = guiContexts.find(ctx);
        if (it == guiContexts.end())
            return nullptr;
        return &it->second;
    }

    void Application::reset_gui_swapchain_state(GuiContextState& guiState)
    {
        guiState.guiPipeline.reset();
        guiState.guiUniformBuffers.clear();
        guiState.guiUniformBuffersMemory.clear();
        guiState.guiUniformBuffersMapped.clear();
        guiState.guiAtlases.clear();
    }

    void Application::set_framebuffer_size(int width, int height)
    {
        framebufferWidth = (std::max)(1, width);
        framebufferHeight = (std::max)(1, height);
        framebufferResized = true;
    }

    int Application::get_framebuffer_width() const noexcept
    {
        return (std::max)(1, framebufferWidth);
    }

    int Application::get_framebuffer_height() const noexcept
    {
        return (std::max)(1, framebufferHeight);
    }

    void Application::cleanup()
    {
        if (device)
            (void)device->waitIdle();

        imageAvailableSemaphores.clear();
        renderFinishedSemaphores.clear();
        inFlightFences.clear();

        if (!commandBuffers.empty() && commandPool && device)
        {
           auto retval = device->resetCommandPool(*commandPool);
            commandBuffers.clear();
        }

        cleanupSwapChain();

        framebuffers.clear();

        descriptorPool.reset();

        uniformBuffers.clear();
        uniformBuffersMemory.clear();
        uniformBuffersMapped.clear();

        indexBuffer.reset();
        indexBufferMemory.reset();
        vertexBuffer.reset();
        vertexBufferMemory.reset();
        guiContexts.clear();

        textureSampler.reset();
        textureImageView.reset();
        textureImage.reset();
        textureImageMemory.reset();

        pipelineLayout.reset();
        graphicsPipeline.reset();
        descriptorSetLayout.reset();
        renderPass.reset();

        depthImage.reset();
        depthImageMemory.reset();
        depthImageView.reset();

        commandPool.reset();

        device.reset();

        // surface is a UniqueSurfaceKHR; do not manually destroy it.
        surface.reset();

        if (validationLayersEnabled && debugMessenger && instance)
        {
            instance->destroyDebugUtilsMessengerEXT(debugMessenger, nullptr);
            debugMessenger = VK_NULL_HANDLE;
        }

        instance.reset();

#if defined(ALMOND_VULKAN_STANDALONE)
        if (window)
        {
            glfwDestroyWindow(window);
            window = nullptr;
        }
        glfwTerminate();
#endif
    }

} // namespace almondnamespace::vulkancontext
