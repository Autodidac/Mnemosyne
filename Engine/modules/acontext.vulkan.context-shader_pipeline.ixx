// ============================================================================
// modules/acontext.vulkan.context-shader_pipeline.ixx
// Partition: acontext.vulkan.context:shader_pipeline
// RenderPass + descriptor set layout + graphics pipelines (main + GUI).
//
// Fixes:
// - SPIR-V must be 4-byte aligned and codeSize multiple-of-4.
//   Reading into std::vector<std::uint32_t> avoids misaligned pCode.
// - Do NOT `export` free helpers (or anything with internal linkage).
// - Keep GUI pipeline compatible with the same render pass (subpass 1) and
//   reuse the existing descriptorSetLayout + pipelineLayout already declared
//   on Application in :shared_vk.
// ============================================================================

module;

#include <include/acontext.vulkan.hpp>
// Include Vulkan-Hpp after config.
#include <vulkan/vulkan.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

export module acontext.vulkan.context:shader_pipeline;

import :shared_vk;

namespace almondnamespace::vulkancontext
{
    namespace
    {
        [[nodiscard]] std::vector<std::uint8_t> read_file_bytes_candidates(std::string_view filename)
        {
            namespace fs = std::filesystem;

            const fs::path target{ filename };

            const std::array<fs::path, 10> candidates = {
                // As-given (e.g. "shaders/vert.spv")
                target,

                // Repo layouts we commonly run from.
                fs::path("Engine") / "assets" / "almondshell" / "vulkan" / target,
                fs::path("..") / "Engine" / "assets" / "almondshell" / "vulkan" / target,
                fs::path("assets") / "almondshell" / "vulkan" / target,
                fs::path("..") / "assets" / "almondshell" / "vulkan" / target,

                // Older pack layouts.
                fs::path("AlmondShell") / "assets" / "almondshell" / "vulkan" / target,
                fs::path("..") / "AlmondShell" / "assets" / "almondshell" / "vulkan" / target,

                // Legacy fallback (if someone kept the old folder name).
                fs::path("assets") / "vulkan" / target,
                fs::path("..") / "assets" / "vulkan" / target,
            };

            for (const auto& path : candidates)
            {
                std::ifstream file(path, std::ios::binary | std::ios::ate);
                if (!file.is_open())
                    continue;

                const auto end = file.tellg();
                if (end <= 0)
                    continue;

                const auto size = static_cast<std::size_t>(end);
                std::vector<std::uint8_t> bytes(size);

                file.seekg(0);
                file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
                if (!file)
                    continue;

                return bytes;
            }

            std::ostringstream oss;
            oss << "[ Vulkan ] -  Failed to open file: " << filename << "\nTried paths:";
            for (const auto& p : candidates)
                oss << "\n  - " << p.string();
            throw std::runtime_error(oss.str());
        }

        [[nodiscard]] std::vector<std::uint32_t> spirv_bytes_to_u32(std::span<const std::uint8_t> bytes)
        {
            if (bytes.empty())
                throw std::runtime_error("[ Vulkan ] -  SPIR-V file was empty.");

            if ((bytes.size() % 4u) != 0u)
                throw std::runtime_error("[ Vulkan ] -  SPIR-V size is not a multiple of 4 bytes.");

            const std::size_t wordCount = bytes.size() / 4u;
            std::vector<std::uint32_t> words(wordCount);

            // Copy into aligned u32 storage. Endianness on Windows/x64 is little.
            std::memcpy(words.data(), bytes.data(), bytes.size());
            return words;
        }

        [[nodiscard]] std::vector<std::uint32_t> read_spirv_u32(std::string_view filename)
        {
            const auto bytes = read_file_bytes_candidates(filename);
            return spirv_bytes_to_u32(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
        }

        [[nodiscard]] vk::UniqueShaderModule make_shader_module(vk::Device dev, std::span<const std::uint32_t> code)
        {
            if (code.empty())
                throw std::runtime_error("[ Vulkan ] -  make_shader_module: empty code.");

            vk::ShaderModuleCreateInfo info{};
            info.codeSize = static_cast<std::size_t>(code.size_bytes());
            info.pCode = code.data();

            auto r = dev.createShaderModuleUnique(info);
            if (r.result != vk::Result::eSuccess)
                throw std::runtime_error("[ Vulkan ] -  createShaderModuleUnique failed.");
            return std::move(r.value);
        }
    }

    void Application::createRenderPass()
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChainImageFormat;
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
        colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

        vk::AttachmentDescription depthAttachment{};
        depthAttachment.format = findDepthFormat();
        depthAttachment.samples = vk::SampleCountFlagBits::e1;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        const std::array<vk::AttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

        vk::AttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::AttachmentReference depthRef{};
        depthRef.attachment = 1;
        depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        // Subpass 0: scene (color + depth)
        vk::SubpassDescription scene{};
        scene.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        scene.colorAttachmentCount = 1;
        scene.pColorAttachments = &colorRef;
        scene.pDepthStencilAttachment = &depthRef;

        // Subpass 1: GUI overlay (color only)
        vk::SubpassDescription gui{};
        gui.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        gui.colorAttachmentCount = 1;
        gui.pColorAttachments = &colorRef;
        gui.pDepthStencilAttachment = nullptr;

        const std::array<vk::SubpassDescription, 2> subpasses = { scene, gui };

        // External -> scene
        vk::SubpassDependency dep0{};
        dep0.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep0.dstSubpass = 0;
        dep0.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
            vk::PipelineStageFlagBits::eEarlyFragmentTests;
        dep0.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
            vk::PipelineStageFlagBits::eEarlyFragmentTests;
        dep0.srcAccessMask = {};
        dep0.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite |
            vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        dep0.dependencyFlags = vk::DependencyFlagBits::eByRegion;

        // scene -> gui (ensure GUI sees the color output)
        vk::SubpassDependency dep1{};
        dep1.srcSubpass = 0;
        dep1.dstSubpass = 1;
        dep1.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dep1.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dep1.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        dep1.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
            vk::AccessFlagBits::eColorAttachmentWrite;
        dep1.dependencyFlags = vk::DependencyFlagBits::eByRegion;

        const std::array<vk::SubpassDependency, 2> deps = { dep0, dep1 };

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        rpInfo.pAttachments = attachments.data();
        rpInfo.subpassCount = static_cast<std::uint32_t>(subpasses.size());
        rpInfo.pSubpasses = subpasses.data();
        rpInfo.dependencyCount = static_cast<std::uint32_t>(deps.size());
        rpInfo.pDependencies = deps.data();

        auto rp = device->createRenderPassUnique(rpInfo);
        if (rp.result != vk::Result::eSuccess)
            throw std::runtime_error("[ Vulkan ] -  createRenderPassUnique failed.");
        renderPass = std::move(rp.value);
    }

    void Application::createDescriptorSetLayout()
    {
        // binding 0: UBO (vertex)
        vk::DescriptorSetLayoutBinding ubo{};
        ubo.binding = 0;
        ubo.descriptorCount = 1;
        ubo.descriptorType = vk::DescriptorType::eUniformBuffer;
        ubo.stageFlags = vk::ShaderStageFlagBits::eVertex;

        // binding 1: sampler (fragment)
        vk::DescriptorSetLayoutBinding sampler{};
        sampler.binding = 1;
        sampler.descriptorCount = 1;
        sampler.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        sampler.stageFlags = vk::ShaderStageFlagBits::eFragment;

        const std::array<vk::DescriptorSetLayoutBinding, 2> bindings = { ubo, sampler };

        vk::DescriptorSetLayoutCreateInfo info{};
        info.bindingCount = static_cast<std::uint32_t>(bindings.size());
        info.pBindings = bindings.data();

        auto r = device->createDescriptorSetLayoutUnique(info);
        if (r.result != vk::Result::eSuccess)
            throw std::runtime_error("[ Vulkan ] -  createDescriptorSetLayoutUnique failed.");
        descriptorSetLayout = std::move(r.value);
    }

    void Application::createGraphicsPipeline()
    {
        // NOTE: Read SPIR-V as u32 (alignment + codeSize correctness).
        const auto vert = read_spirv_u32("shaders/vert.spv");
        const auto frag = read_spirv_u32("shaders/frag.spv");

        vk::UniqueShaderModule vMod = make_shader_module(*device, std::span<const std::uint32_t>(vert));
        vk::UniqueShaderModule fMod = make_shader_module(*device, std::span<const std::uint32_t>(frag));

        vk::PipelineShaderStageCreateInfo stages[2]{};
        stages[0].stage = vk::ShaderStageFlagBits::eVertex;
        stages[0].module = *vMod;
        stages[0].pName = "main";
        stages[1].stage = vk::ShaderStageFlagBits::eFragment;
        stages[1].module = *fMod;
        stages[1].pName = "main";

        const auto bindingDesc = Vertex::getBindingDescription();
        const auto attrDescs = Vertex::getAttributeDescriptions();

        vk::PipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &bindingDesc;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrDescs.size());
        vertexInput.pVertexAttributeDescriptions = attrDescs.data();

        vk::PipelineInputAssemblyStateCreateInfo inputAsm{};
        inputAsm.topology = vk::PrimitiveTopology::eTriangleList;
        inputAsm.primitiveRestartEnable = VK_FALSE;

        vk::Viewport viewport{};
        viewport.x = 0.f;
        viewport.y = 0.f;
        viewport.width = static_cast<float>(swapChainExtent.width);
        viewport.height = static_cast<float>(swapChainExtent.height);
        viewport.minDepth = 0.f;
        viewport.maxDepth = 1.f;

        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D{ 0, 0 };
        scissor.extent = swapChainExtent;

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo raster{};
        raster.depthClampEnable = VK_FALSE;
        raster.rasterizerDiscardEnable = VK_FALSE;
        raster.polygonMode = vk::PolygonMode::eFill;
        raster.cullMode = vk::CullModeFlagBits::eNone;
        raster.frontFace = vk::FrontFace::eCounterClockwise;
        raster.depthBiasEnable = VK_FALSE;
        raster.lineWidth = 1.0f;

        vk::PipelineMultisampleStateCreateInfo msaa{};
        msaa.sampleShadingEnable = VK_FALSE;
        msaa.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depth{};
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = vk::CompareOp::eLess;
        depth.depthBoundsTestEnable = VK_FALSE;
        depth.stencilTestEnable = VK_FALSE;

        vk::PipelineColorBlendAttachmentState blendAtt{};
        blendAtt.colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        blendAtt.blendEnable = VK_FALSE;

        vk::PipelineColorBlendStateCreateInfo blending{};
        blending.logicOpEnable = VK_FALSE;
        blending.attachmentCount = 1;
        blending.pAttachments = &blendAtt;

        // Pipeline layout (shared for scene + GUI)
        vk::PipelineLayoutCreateInfo plInfo{};
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &*descriptorSetLayout;

        auto pl = device->createPipelineLayoutUnique(plInfo);
        if (pl.result != vk::Result::eSuccess)
            throw std::runtime_error("[ Vulkan ] -  createPipelineLayoutUnique failed.");
        pipelineLayout = std::move(pl.value);

        vk::GraphicsPipelineCreateInfo gpInfo{};
        gpInfo.stageCount = 2;
        gpInfo.pStages = stages;
        gpInfo.pVertexInputState = &vertexInput;
        gpInfo.pInputAssemblyState = &inputAsm;
        gpInfo.pViewportState = &viewportState;
        gpInfo.pRasterizationState = &raster;
        gpInfo.pMultisampleState = &msaa;
        gpInfo.pDepthStencilState = &depth;
        gpInfo.pColorBlendState = &blending;
        gpInfo.layout = *pipelineLayout;
        gpInfo.renderPass = *renderPass;
        gpInfo.subpass = 0;

        auto gp = device->createGraphicsPipelineUnique(vk::PipelineCache{}, gpInfo);
        if (gp.result != vk::Result::eSuccess)
            throw std::runtime_error("[ Vulkan ] -  createGraphicsPipelineUnique failed.");
        graphicsPipeline = std::move(gp.value);
    }

    void Application::createGuiPipeline()
    {
        // Store per-context GUI pipeline in the active GUI context state.
        auto& guiState = gui_state_for_context(activeGuiContext);

        const auto vert = read_spirv_u32("shaders/gui_vert.spv");
        const auto frag = read_spirv_u32("shaders/gui_frag.spv");

        vk::UniqueShaderModule vMod = make_shader_module(*device, std::span<const std::uint32_t>(vert));
        vk::UniqueShaderModule fMod = make_shader_module(*device, std::span<const std::uint32_t>(frag));

        vk::PipelineShaderStageCreateInfo stages[2]{};
        stages[0].stage = vk::ShaderStageFlagBits::eVertex;
        stages[0].module = *vMod;
        stages[0].pName = "main";
        stages[1].stage = vk::ShaderStageFlagBits::eFragment;
        stages[1].module = *fMod;
        stages[1].pName = "main";

        // GUI uses the same vertex format in your current codebase (pos/normal/uv).
        // If your GUI shaders expect a different format, you must change the GUI vertex layout
        // where you build the GUI vertex buffer + update this binding/attributes to match.
        const auto bindingDesc = Vertex::getBindingDescription();
        const auto attrDescs = Vertex::getAttributeDescriptions();

        vk::PipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &bindingDesc;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrDescs.size());
        vertexInput.pVertexAttributeDescriptions = attrDescs.data();

        vk::PipelineInputAssemblyStateCreateInfo inputAsm{};
        inputAsm.topology = vk::PrimitiveTopology::eTriangleList;
        inputAsm.primitiveRestartEnable = VK_FALSE;

        vk::Viewport viewport{};
        viewport.x = 0.f;
        viewport.y = 0.f;
        viewport.width = static_cast<float>(swapChainExtent.width);
        viewport.height = static_cast<float>(swapChainExtent.height);
        viewport.minDepth = 0.f;
        viewport.maxDepth = 1.f;

        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D{ 0, 0 };
        scissor.extent = swapChainExtent;

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo raster{};
        raster.depthClampEnable = VK_FALSE;
        raster.rasterizerDiscardEnable = VK_FALSE;
        raster.polygonMode = vk::PolygonMode::eFill;
        raster.cullMode = vk::CullModeFlagBits::eNone;
        raster.frontFace = vk::FrontFace::eCounterClockwise;
        raster.depthBiasEnable = VK_FALSE;
        raster.lineWidth = 1.0f;

        vk::PipelineMultisampleStateCreateInfo msaa{};
        msaa.sampleShadingEnable = VK_FALSE;
        msaa.rasterizationSamples = vk::SampleCountFlagBits::e1;

        // GUI: no depth test/write.
        vk::PipelineDepthStencilStateCreateInfo depth{};
        depth.depthTestEnable = VK_FALSE;
        depth.depthWriteEnable = VK_FALSE;
        depth.depthCompareOp = vk::CompareOp::eAlways;
        depth.depthBoundsTestEnable = VK_FALSE;
        depth.stencilTestEnable = VK_FALSE;

        vk::PipelineColorBlendAttachmentState blendAtt{};
        blendAtt.colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        blendAtt.blendEnable = VK_TRUE;
        blendAtt.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        blendAtt.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        blendAtt.colorBlendOp = vk::BlendOp::eAdd;
        blendAtt.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        blendAtt.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        blendAtt.alphaBlendOp = vk::BlendOp::eAdd;

        vk::PipelineColorBlendStateCreateInfo blending{};
        blending.logicOpEnable = VK_FALSE;
        blending.attachmentCount = 1;
        blending.pAttachments = &blendAtt;

        // Reuse the already-created pipelineLayout (set layout matches binding 0/1).
        if (!pipelineLayout || !(*pipelineLayout))
            throw std::runtime_error("[ Vulkan ] -  createGuiPipeline called before pipelineLayout was created.");

        vk::GraphicsPipelineCreateInfo gpInfo{};
        gpInfo.stageCount = 2;
        gpInfo.pStages = stages;
        gpInfo.pVertexInputState = &vertexInput;
        gpInfo.pInputAssemblyState = &inputAsm;
        gpInfo.pViewportState = &viewportState;
        gpInfo.pRasterizationState = &raster;
        gpInfo.pMultisampleState = &msaa;
        gpInfo.pDepthStencilState = &depth;
        gpInfo.pColorBlendState = &blending;
        gpInfo.layout = *pipelineLayout;
        gpInfo.renderPass = *renderPass;
        gpInfo.subpass = 1;

        auto gp = device->createGraphicsPipelineUnique(vk::PipelineCache{}, gpInfo);
        if (gp.result != vk::Result::eSuccess)
            throw std::runtime_error("[ Vulkan ] -  createGuiPipeline failed.");

        guiState.guiPipeline = std::move(gp.value);
    }
}
