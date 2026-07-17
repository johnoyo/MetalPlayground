#include "MetalEngine.h"

#include "MtlUtils.h"

#include "stb_image.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

#include <iostream>
#include <fstream>
#include <filesystem>

namespace MTLE
{
    static std::filesystem::path ResolveAssetPath(const std::string& relativePath)
    {
        const auto& currentPath = std::filesystem::current_path().parent_path().parent_path().parent_path();
        return currentPath / "MetalEngine" / std::filesystem::path(relativePath);
    }

    static std::string ReadFile(const std::string& filepath)
    {
        const auto shaderFilePath = ResolveAssetPath(filepath);
        
        std::string result;
        std::ifstream in(shaderFilePath, std::ios::in | std::ios::binary);

        if (in)
        {
            in.seekg(0, std::ios::end);
            size_t size = in.tellg();
            if (size != -1)
            {
                result.resize(size);
                in.seekg(0, std::ios::beg);
                in.read(&result[0], size);
            }
            else
            {
                std::cerr << "Could not read from file " << filepath;
            }
        }
        else
        {
            std::cerr << "Could not open file " << filepath;
        }

        return result;
    }

    MTL::Texture* MetalEngine::CreateDepthTexture(uint32_t width, uint32_t height, uint8_t frameIdx)
    {
        MTL::TextureDescriptor* depthDesc = MTL::TextureDescriptor::alloc()->init();
        depthDesc->setTextureType(MTL::TextureType2D);
        depthDesc->setPixelFormat(MTL::PixelFormatDepth32Float);
        depthDesc->setWidth(width);
        depthDesc->setHeight(height);
        depthDesc->setUsage(MTL::TextureUsageRenderTarget);
        depthDesc->setStorageMode(MTL::StorageModePrivate);

        MTL::Texture* texture = m_Device->newTexture(depthDesc);
        depthDesc->release();

        const std::string label = "Depth Texture (frame " + std::to_string(frameIdx) + ")";
        texture->setLabel(NS::String::string(label.c_str(), NS::UTF8StringEncoding));

        return texture;
    }

    void MetalEngine::CreateAndUploadTexture(const std::string& path)
    {
        int texWidth, texHeight, texChannels;
        const auto texturePath = ResolveAssetPath(path);
        stbi_uc* pixels = stbi_load(texturePath.string().c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

        if (!pixels)
        {
            std::cerr << "Failed to load texture: " << texturePath << "\n";
            return;
        }

        const size_t textureSize = static_cast<size_t>(texWidth) * texHeight * 4;

        MTL::Buffer* stagingBuffer = m_Device->newBuffer(textureSize, MTL::ResourceStorageModeShared);
        memcpy(stagingBuffer->contents(), pixels, textureSize);
        stbi_image_free(pixels);

        MTL::TextureDescriptor* texDesc = MTL::TextureDescriptor::alloc()->init();
        texDesc->setTextureType(MTL::TextureType2D);
        texDesc->setPixelFormat(MTL::PixelFormatRGBA8Unorm_sRGB); // albedo data: sample-time sRGB->linear conversion
        texDesc->setWidth(static_cast<NS::UInteger>(texWidth));
        texDesc->setHeight(static_cast<NS::UInteger>(texHeight));
        texDesc->setUsage(MTL::TextureUsageShaderRead);
        texDesc->setStorageMode(MTL::StorageModePrivate); // GPU-only; CPU never touches it again after this upload

        m_CubeTexture = m_Device->newTexture(texDesc);
        texDesc->release();
        m_CubeTexture->setLabel(NS::String::string("Cube Texture", NS::UTF8StringEncoding));

        // Sampler — created once, shared read-only across every frame-in-flight.
        MTL::SamplerDescriptor* samplerDesc = MTL::SamplerDescriptor::alloc()->init();
        samplerDesc->setMinFilter(MTL::SamplerMinMagFilterLinear);
        samplerDesc->setMagFilter(MTL::SamplerMinMagFilterLinear);
        samplerDesc->setMipFilter(MTL::SamplerMipFilterNotMipmapped);
        samplerDesc->setSAddressMode(MTL::SamplerAddressModeRepeat);
        samplerDesc->setTAddressMode(MTL::SamplerAddressModeRepeat);
        m_Sampler = m_Device->newSamplerState(samplerDesc);
        samplerDesc->release();

        // One-shot upload: reuse the persistent command buffer/allocator[0] before
        // the render loop starts, nothing else has touched them yet, and MTL4
        // command buffers are explicitly designed to be reset and re-encoded.
        m_ResidencySet->addAllocation(stagingBuffer);
        m_ResidencySet->addAllocation(m_CubeTexture);
        m_ResidencySet->commit();

        m_CommandBuffer->beginCommandBuffer(m_CommandAllocators[0]);

        MTL4::ComputeCommandEncoder* uploadEncoder = m_CommandBuffer->computeCommandEncoder();

        uploadEncoder->copyFromBuffer(stagingBuffer, 0,
                                       static_cast<NS::UInteger>(texWidth) * 4,
                                       textureSize,
                                       MTL::Size(static_cast<NS::UInteger>(texWidth), static_cast<NS::UInteger>(texHeight), 1),
                                       m_CubeTexture, 0, 0,
                                       MTL::Origin(0, 0, 0));

        uploadEncoder->endEncoding();
        m_CommandBuffer->endCommandBuffer();

        m_CommandQueue->commit(&m_CommandBuffer, 1);

        // A dedicated event, not m_FrameAvailableSharedEvent — that one's per-frame
        // counter must stay monotonic starting from frame 0, so it can't be reused
        // for this one-off wait without corrupting Run()'s frame bookkeeping.
        MTL::SharedEvent* uploadDoneEvent = m_Device->newSharedEvent();
        uploadDoneEvent->setSignaledValue(0);
        m_CommandQueue->signalEvent(uploadDoneEvent, 1);
        uploadDoneEvent->waitUntilSignaledValue(1, UINT64_MAX);
        uploadDoneEvent->release();

        stagingBuffer->release();
        m_ResidencySet->removeAllocation(stagingBuffer); // texture stays resident; staging buffer doesn't need to
    }

    void MetalEngine::Init()
    {
        // Window.
        if (!glfwInit())
        {
            std::cerr << "Error initializing window!";
            exit(-1);
        }
        
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        
        m_Window = glfwCreateWindow(m_Width, m_Height, "Metal Triangle", nullptr, nullptr);
        
        if (!m_Window)
        {
            std::cerr << "Error creating window!";
            glfwTerminate();
            exit(-1);
        }
        
        // Device.
        m_Device = MTL::CreateSystemDefaultDevice();
        m_MetalLayer = CA::MetalLayer::layer();
        {
            m_MetalLayer->setDevice(m_Device);
            m_MetalLayer->setPixelFormat(PIXEL_FORMAT);
            m_MetalLayer->setMaximumDrawableCount(MAX_FRAMES_IN_FLIGHT);
            m_MetalLayer->setFramebufferOnly(true);
        }
        
        ConnectWindowWithMetal(m_Window, m_MetalLayer);
        
        // Commands.
        m_CommandQueue = m_Device->newMTL4CommandQueue();
        m_CommandBuffer = m_Device->newCommandBuffer();
        
        for(auto& alloc: m_CommandAllocators)
        {
            alloc = m_Device->newCommandAllocator();
        }
        
        // Synchronization (shared events are similar to vulkan fences).
        m_FrameAvailableSharedEvent = m_Device->newSharedEvent();
        m_FrameAvailableSharedEvent->setSignaledValue(0);
        
        // Create compiler.
        MTL4::Compiler* compiler;
        {
            auto* compilerDesc = MTL4::CompilerDescriptor::alloc()->init();
            compiler = m_Device->newCompiler(compilerDesc, nullptr);
            compilerDesc->release();
        }
        
        // Use new compilation api to compile shader at runtime.
        MTL::Library* library;
        {
            const auto& mslSource = ReadFile("assets/shaders/triangle.metal");
            
            MTL4::LibraryDescriptor* libDesc = MTL4::LibraryDescriptor::alloc()->init();
            libDesc->setSource(NS::String::string(mslSource.c_str(), NS::UTF8StringEncoding));
            libDesc->setName(NS::String::string("triangle", NS::UTF8StringEncoding));
            
            NS::Error* error = nullptr;
            library = compiler->newLibrary(libDesc, &error);
            libDesc->release();
            
            if (!library)
            {
                std::cerr << "Shader compile error: " << error->localizedDescription()->utf8String() << "\n";
                return;
            }
        }
        
        // Get the shader functions out of the library.
        MTL4::LibraryFunctionDescriptor* vertexFn = MTL4::LibraryFunctionDescriptor::alloc()->init();
        vertexFn->setLibrary(library);
        vertexFn->setName(NS::String::string("vertexMain", NS::UTF8StringEncoding));

        MTL4::LibraryFunctionDescriptor* fragmentFn = MTL4::LibraryFunctionDescriptor::alloc()->init();
        fragmentFn->setLibrary(library);
        fragmentFn->setName(NS::String::string("fragmentMain", NS::UTF8StringEncoding));
        
        // Vertex buffer layout.
        MTL::VertexDescriptor* vertexDesc = MTL::VertexDescriptor::alloc()->init();
        
        vertexDesc->attributes()->object(0)->setFormat(MTL::VertexFormatFloat4); // position
        vertexDesc->attributes()->object(0)->setOffset(offsetof(Vertex, position));
        vertexDesc->attributes()->object(0)->setBufferIndex(VERTEX_BUFFER_BINDING_IDX);
        
        vertexDesc->attributes()->object(1)->setFormat(MTL::VertexFormatFloat2); // color
        vertexDesc->attributes()->object(1)->setOffset(offsetof(Vertex, texCoord));
        vertexDesc->attributes()->object(1)->setBufferIndex(VERTEX_BUFFER_BINDING_IDX);
        
        vertexDesc->layouts()->object(VERTEX_BUFFER_BINDING_IDX)->setStride(sizeof(Vertex));
        
        // Build pso.
        MTL4::RenderPipelineDescriptor* pipelineDesc = MTL4::RenderPipelineDescriptor::alloc()->init();
        pipelineDesc->setLabel(NS::String::string("Hello Triangle PSO", NS::UTF8StringEncoding));
        pipelineDesc->setVertexFunctionDescriptor(vertexFn);
        pipelineDesc->setFragmentFunctionDescriptor(fragmentFn);
        pipelineDesc->colorAttachments()->object(0)->setPixelFormat(PIXEL_FORMAT);
        pipelineDesc->setVertexDescriptor(vertexDesc);
        
        vertexDesc->release();

        NS::Error* psoError = nullptr;
        m_Pso = compiler->newRenderPipelineState(pipelineDesc, (MTL4::CompilerTaskOptions*)nullptr, &psoError);
        
        if (!m_Pso)
        {
            std::cerr << "PSO compile error: " << psoError->localizedDescription()->utf8String() << "\n";
            return;
        }

        vertexFn->release();
        fragmentFn->release();
        pipelineDesc->release();
        compiler->release();
        
        // Residency sets.
        auto* residencySetDesc = MTL::ResidencySetDescriptor::alloc();
        m_ResidencySet = m_Device->newResidencySet(residencySetDesc, nullptr );
        residencySetDesc->release();
        
        m_CommandQueue->addResidencySet(m_ResidencySet);
        
        // Vertex data.
        static const std::array<Vertex, 24> cubeVertices =
        {
            // +Z front - red
            Vertex{ .position = glm::vec4(-0.5f, -0.5f,  0.5f, 1.f), .texCoord = glm::vec2(0,1) },
            Vertex{ .position = glm::vec4( 0.5f, -0.5f,  0.5f, 1.f), .texCoord = glm::vec2(1,1) },
            Vertex{ .position = glm::vec4( 0.5f,  0.5f,  0.5f, 1.f), .texCoord = glm::vec2(1,0) },
            Vertex{ .position = glm::vec4(-0.5f,  0.5f,  0.5f, 1.f), .texCoord = glm::vec2(0,0) },
            // -Z back - green
            Vertex{ .position = glm::vec4( 0.5f, -0.5f, -0.5f, 1.f), .texCoord = glm::vec2(0,1) },
            Vertex{ .position = glm::vec4(-0.5f, -0.5f, -0.5f, 1.f), .texCoord = glm::vec2(1,1) },
            Vertex{ .position = glm::vec4(-0.5f,  0.5f, -0.5f, 1.f), .texCoord = glm::vec2(1,0) },
            Vertex{ .position = glm::vec4( 0.5f,  0.5f, -0.5f, 1.f), .texCoord = glm::vec2(0,0) },
            // +X right - blue
            Vertex{ .position = glm::vec4( 0.5f, -0.5f,  0.5f, 1.f), .texCoord = glm::vec2(0,1) },
            Vertex{ .position = glm::vec4( 0.5f, -0.5f, -0.5f, 1.f), .texCoord = glm::vec2(1,1) },
            Vertex{ .position = glm::vec4( 0.5f,  0.5f, -0.5f, 1.f), .texCoord = glm::vec2(1,0) },
            Vertex{ .position = glm::vec4( 0.5f,  0.5f,  0.5f, 1.f), .texCoord = glm::vec2(0,0) },
            // -X left - yellow
            Vertex{ .position = glm::vec4(-0.5f, -0.5f, -0.5f, 1.f), .texCoord = glm::vec2(0,1) },
            Vertex{ .position = glm::vec4(-0.5f, -0.5f,  0.5f, 1.f), .texCoord = glm::vec2(1,1) },
            Vertex{ .position = glm::vec4(-0.5f,  0.5f,  0.5f, 1.f), .texCoord = glm::vec2(1,0) },
            Vertex{ .position = glm::vec4(-0.5f,  0.5f, -0.5f, 1.f), .texCoord = glm::vec2(0,0) },
            // +Y top - magenta
            Vertex{ .position = glm::vec4(-0.5f,  0.5f,  0.5f, 1.f), .texCoord = glm::vec2(0,1) },
            Vertex{ .position = glm::vec4( 0.5f,  0.5f,  0.5f, 1.f), .texCoord = glm::vec2(1,1) },
            Vertex{ .position = glm::vec4( 0.5f,  0.5f, -0.5f, 1.f), .texCoord = glm::vec2(1,0) },
            Vertex{ .position = glm::vec4(-0.5f,  0.5f, -0.5f, 1.f), .texCoord = glm::vec2(0,0) },
            // -Y bottom - cyan
            Vertex{ .position = glm::vec4(-0.5f, -0.5f, -0.5f, 1.f), .texCoord = glm::vec2(0,1) },
            Vertex{ .position = glm::vec4( 0.5f, -0.5f, -0.5f, 1.f), .texCoord = glm::vec2(1,1) },
            Vertex{ .position = glm::vec4( 0.5f, -0.5f,  0.5f, 1.f), .texCoord = glm::vec2(1,0) },
            Vertex{ .position = glm::vec4(-0.5f, -0.5f,  0.5f, 1.f), .texCoord = glm::vec2(0,0) },
        };

        static const std::array<uint16_t, CUBE_INDEX_COUNT> cubeIndices =
        {
            0,1,2,   0,2,3,     // +Z
            4,5,6,   4,6,7,     // -Z
            8,9,10,  8,10,11,   // +X
            12,13,14,12,14,15,  // -X
            16,17,18,16,18,19,  // +Y
            20,21,22,20,22,23,  // -Y
        };
        constexpr size_t vertexBufferSize = sizeof(Vertex) * cubeVertices.size();
        
        // Index Buffer.
        constexpr size_t indexBufferSize = sizeof(uint16_t) * cubeIndices.size();
        m_IndexBuffer = m_Device->newBuffer(indexBufferSize, MTL::ResourceStorageModeShared);
        m_IndexBuffer->setLabel(NS::String::string("Cube Index Buffer", NS::UTF8StringEncoding));
        memcpy(m_IndexBuffer->contents(), cubeIndices.data(), indexBufferSize);
        m_ResidencySet->addAllocation(m_IndexBuffer);
        
        CreateAndUploadTexture("assets/textures/Brick.png");

        // Argument table.
        //
        // (Deprecated) We are gonna have one argument table, this is similar to the 4 descriptor sets we have on vulkan.
        // And we update each time with a call to argTable->setAdress the resources needed.
        // The other diference is that the vertex buffers also live in this argument table.
        // We are gonna put them after the 4 descriptors at slot 4.
        //
        // Alternative, have an argument table per vulkan descriptor set + extra for vertex buffer data.
        for (uint8_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            // Buffer.
            m_VertexBuffers[i] = m_Device->newBuffer(vertexBufferSize, MTL::ResourceStorageModeShared);
            const std::string label = "Vertex Buffer (frame " + std::to_string(i) + ")";
            m_VertexBuffers[i]->setLabel(NS::String::string(label.c_str(), NS::UTF8StringEncoding));
            // Initial contents. For truly dynamic geometry, move this memcpy into
            // Run() and write fresh data into m_VertexBuffers[frameIdx] each frame
            // instead, the triple-buffering here already supports that safely.
            memcpy(m_VertexBuffers[i]->contents(), cubeVertices.data(), vertexBufferSize);
            m_ResidencySet->addAllocation(m_VertexBuffers[i]);
            
            // Uniform buffer. One per frame in flight, same rationale as the vertex
            // buffers: the CPU must never write into a slot the GPU might still be
            // reading. Contents are written fresh in Run() each frame, so no initial
            // memcpy is needed here.
            m_UniformBuffers[i] = m_Device->newBuffer(sizeof(FrameUniforms), MTL::ResourceStorageModeShared);
            const std::string ubLabel = "Frame Uniforms (frame " + std::to_string(i) + ")";
            m_UniformBuffers[i]->setLabel(NS::String::string(ubLabel.c_str(), NS::UTF8StringEncoding));
            m_ResidencySet->addAllocation(m_UniformBuffers[i]);
            
            // Depth texture.
            m_DepthTextures[i] = CreateDepthTexture(m_Width, m_Height, i);
            m_ResidencySet->addAllocation(m_DepthTextures[i]);

            // Argument table, one per frame in flight, permanently bound to that
            // frame's buffer, so we never mutate a table the GPU might still be reading.
            auto* argTableDesc = MTL4::ArgumentTableDescriptor::alloc();
            argTableDesc->setMaxBufferBindCount(2);
            argTableDesc->setMaxTextureBindCount(1);
            argTableDesc->setMaxSamplerStateBindCount(1);
            m_ArgTables[i] = m_Device->newArgumentTable(argTableDesc, /* error */ nullptr);
            argTableDesc->release();

            m_ArgTables[i]->setAddress(m_VertexBuffers[i]->gpuAddress(), VERTEX_BUFFER_BINDING_IDX);
            m_ArgTables[i]->setAddress(m_UniformBuffers[i]->gpuAddress(), UNIFORM_BUFFER_BINDING_IDX);
            
            m_ArgTables[i]->setTexture(m_CubeTexture->gpuResourceID(), 0);
            m_ArgTables[i]->setSamplerState(m_Sampler->gpuResourceID(), 0);
        }
        
        // Depth state.
        MTL::DepthStencilDescriptor* depthStencilDesc = MTL::DepthStencilDescriptor::alloc()->init();
        depthStencilDesc->setDepthCompareFunction(MTL::CompareFunctionLess);
        depthStencilDesc->setDepthWriteEnabled(true);
        m_DepthStencilState = m_Device->newDepthStencilState(depthStencilDesc);
        depthStencilDesc->release();
        
        m_DepthWidth = m_Width;
        m_DepthHeight = m_Height;
        
        m_ResidencySet->commit();
        m_CommandQueue->addResidencySet(m_MetalLayer->residencySet());
    }

    void MetalEngine::Run()
    {
        while(!glfwWindowShouldClose(m_Window))
        {
            glfwPollEvents();
            
            int fbWidth, fbHeight;
            glfwGetFramebufferSize(m_Window, &fbWidth, &fbHeight);
            
            // Minimized, nothing to size a depth texture or projection against.
            if (fbWidth == 0 || fbHeight == 0)
            {
                continue;
            }
            
            m_MetalLayer->setDrawableSize(CGSizeMake(fbWidth, fbHeight));
            
            // Resize: same idea as recreating a Vulkan swapchain's depth image. All
            // MAX_FRAMES_IN_FLIGHT depth textures could still be referenced by
            // in-flight GPU work, so idle fully before touching any of them.
            if ((uint32_t)fbWidth != m_DepthWidth || (uint32_t)fbHeight != m_DepthHeight)
            {
                if (m_FrameNum > 0)
                {
                    m_FrameAvailableSharedEvent->waitUntilSignaledValue(m_FrameNum - 1, UINT64_MAX);
                }

                for (uint8_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
                {
                    m_ResidencySet->removeAllocation(m_DepthTextures[i]);
                    m_DepthTextures[i]->release();
                    m_DepthTextures[i] = CreateDepthTexture(fbWidth, fbHeight, i);
                    m_ResidencySet->addAllocation(m_DepthTextures[i]);
                }
                m_ResidencySet->commit();

                m_DepthWidth = static_cast<uint32_t>(fbWidth);
                m_DepthHeight = static_cast<uint32_t>(fbHeight);
            }
            
            if (m_FrameNum >= MAX_FRAMES_IN_FLIGHT)
            {
                m_FrameAvailableSharedEvent->waitUntilSignaledValue(m_FrameNum - MAX_FRAMES_IN_FLIGHT, 1000);
            }
            
            const uint8_t frameIdx = m_FrameNum % MAX_FRAMES_IN_FLIGHT;
            MTL4::CommandAllocator* cmdAlloc = m_CommandAllocators[frameIdx];
            cmdAlloc->reset();
            
            /// Model: fixed tilt so more than one face is ever visible, plus a
            // time-driven spin. View/projection: standard camera setup.
            // perspectiveZO (not the plain glm::perspective) matches Metal's
            // [0,1] clip-space z range — Vulkan uses the same range, but note
            // Metal's NDC y is up, so unlike your Vulkan projection, no y-flip here.
            const float aspect = static_cast<float>(fbWidth) / static_cast<float>(fbHeight);
            glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(25.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, static_cast<float>(glfwGetTime()), glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 proj = glm::perspectiveZO(glm::radians(60.0f), aspect, 0.1f, 100.0f);

            FrameUniforms uniforms{};
            uniforms.mvp = proj * view * model;
            memcpy(m_UniformBuffers[frameIdx]->contents(), &uniforms, sizeof(FrameUniforms));
            
            CA::MetalDrawable* surface = m_MetalLayer->nextDrawable();
            
            m_CommandBuffer->beginCommandBuffer(cmdAlloc);
            
            // RenderPassDescriptor has bundled both the framebuffer and renderpass functionality of vulkan.
            MTL4::RenderPassDescriptor* passDesc = MTL4::RenderPassDescriptor::alloc()->init();
            
            MTL::RenderPassColorAttachmentDescriptor* colorAttachment = passDesc->colorAttachments()->object(0);
            colorAttachment->setTexture(surface->texture());
            colorAttachment->setLoadAction(MTL::LoadActionClear);
            colorAttachment->setStoreAction(MTL::StoreActionStore);
            colorAttachment->setClearColor(MTL::ClearColor::Make(1.0, 0.4118, 0.7059, 1.0)); // pink
            
            MTL::RenderPassDepthAttachmentDescriptor* depthAttachment = passDesc->depthAttachment();
            depthAttachment->setTexture(m_DepthTextures[frameIdx]);
            depthAttachment->setLoadAction(MTL::LoadActionClear);
            depthAttachment->setStoreAction(MTL::StoreActionDontCare); // transient, never read back
            depthAttachment->setClearDepth(1.0);
            
            // RenderCommandEncoder similar to vkCmdBeginRenderPass.
            MTL4::RenderCommandEncoder* encoder = m_CommandBuffer->renderCommandEncoder(passDesc);
            
            encoder->setRenderPipelineState(m_Pso);
            encoder->setDepthStencilState(m_DepthStencilState);
            encoder->setArgumentTable(m_ArgTables[frameIdx], MTL::RenderStageVertex | MTL::RenderStageFragment);
            encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle,
                                                NS::UInteger(CUBE_INDEX_COUNT),
                                                MTL::IndexTypeUInt16,
                                                m_IndexBuffer->gpuAddress(),
                                                NS::UInteger(CUBE_INDEX_COUNT * sizeof(uint16_t)),
                                                NS::UInteger(1));
            
            encoder->endEncoding();
            
            passDesc->release();
            
            m_CommandBuffer->endCommandBuffer();
            
            m_CommandQueue->wait(surface);
            m_CommandQueue->commit(&m_CommandBuffer, 1);
            m_CommandQueue->signalDrawable(surface);
            surface->present();
            
            m_CommandQueue->signalEvent(m_FrameAvailableSharedEvent, m_FrameNum);
            m_FrameNum++;
        }
    }

    void MetalEngine::Clean()
    {
        m_CubeTexture->release();
        m_Sampler->release();
        
        for (uint8_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            m_VertexBuffers[i]->release();
            m_UniformBuffers[i]->release();
            m_DepthTextures[i]->release();
            m_ArgTables[i]->release();
        }
        
        m_IndexBuffer->release();
        m_DepthStencilState->release();
        m_ResidencySet->release();
        m_Pso->release();
        
        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }
}
