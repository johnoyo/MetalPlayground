#include "MetalEngine.h"

#include "MtlUtils.h"

#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <fstream>
#include <filesystem>

namespace MTLE
{
    static std::string ReadFile(const std::string& filepath)
    {
        const auto& currentPath = std::filesystem::current_path().parent_path().parent_path().parent_path();
        const auto& shaderFilePath = currentPath / "MetalEngine" / std::filesystem::path(filepath);
        
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
        
        vertexDesc->attributes()->object(1)->setFormat(MTL::VertexFormatFloat4); // color
        vertexDesc->attributes()->object(1)->setOffset(offsetof(Vertex, color));
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
        
        // Vertex data.
        static const std::array<Vertex, 3> triangleVertices =
        {
            Vertex{ .position = glm::vec4( 0.0f,  0.5f, 0.f, 1.f), .color = glm::vec4(1.0f, 0.f, 0.f, 1.f) },
            Vertex{ .position = glm::vec4( 0.5f, -0.5f, 0.f, 1.f), .color = glm::vec4(0.0f, 1.f, 0.f, 1.f) },
            Vertex{ .position = glm::vec4(-0.5f, -0.5f, 0.f, 1.f), .color = glm::vec4(0.0f, 0.f, 1.f, 1.f) },
        };
        constexpr size_t vertexBufferSize = sizeof(Vertex) * triangleVertices.size();

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
            memcpy(m_VertexBuffers[i]->contents(), triangleVertices.data(), vertexBufferSize);
            m_ResidencySet->addAllocation(m_VertexBuffers[i]);
            
            // Uniform buffer. One per frame in flight, same rationale as the vertex
            // buffers: the CPU must never write into a slot the GPU might still be
            // reading. Contents are written fresh in Run() each frame, so no initial
            // memcpy is needed here.
            m_UniformBuffers[i] = m_Device->newBuffer(sizeof(FrameUniforms), MTL::ResourceStorageModeShared);
            const std::string ubLabel = "Frame Uniforms (frame " + std::to_string(i) + ")";
            m_UniformBuffers[i]->setLabel(NS::String::string(ubLabel.c_str(), NS::UTF8StringEncoding));
            m_ResidencySet->addAllocation(m_UniformBuffers[i]);

            // Argument table, one per frame in flight, permanently bound to that
            // frame's buffer, so we never mutate a table the GPU might still be reading.
            auto* argTableDesc = MTL4::ArgumentTableDescriptor::alloc();
            argTableDesc->setMaxBufferBindCount(2);
            m_ArgTables[i] = m_Device->newArgumentTable(argTableDesc, /* error */ nullptr);
            argTableDesc->release();

            m_ArgTables[i]->setAddress(m_VertexBuffers[i]->gpuAddress(), VERTEX_BUFFER_BINDING_IDX);
            m_ArgTables[i]->setAddress(m_UniformBuffers[i]->gpuAddress(), UNIFORM_BUFFER_BINDING_IDX);
        }
        
        m_ResidencySet->commit();
        m_CommandQueue->addResidencySet(m_ResidencySet);
        m_CommandQueue->addResidencySet(m_MetalLayer->residencySet());
    }

    void MetalEngine::Run()
    {
        while(!glfwWindowShouldClose(m_Window))
        {
            glfwPollEvents();
            
            int fbWidth, fbHeight;
            glfwGetFramebufferSize(m_Window, &fbWidth, &fbHeight);
            m_MetalLayer->setDrawableSize(CGSizeMake(fbWidth, fbHeight));
            
            if (m_FrameNum >= MAX_FRAMES_IN_FLIGHT)
            {
                m_FrameAvailableSharedEvent->waitUntilSignaledValue(m_FrameNum - MAX_FRAMES_IN_FLIGHT, 1000);
            }
            
            const uint8_t frameIdx = m_FrameNum % MAX_FRAMES_IN_FLIGHT;
            MTL4::CommandAllocator* cmdAlloc = m_CommandAllocators[frameIdx];
            cmdAlloc->reset();
            
            // Safe to write now: the wait above (when m_FrameNum >= MAX_FRAMES_IN_FLIGHT)
            // guarantees the GPU is finished with whatever command buffer last read
            // m_UniformBuffers[frameIdx].
            FrameUniforms uniforms{};
            uniforms.rotation = glm::rotate(glm::mat4(1.0f), static_cast<float>(glfwGetTime()), glm::vec3(0.0f, 0.0f, 1.0f));
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
            
            // RenderCommandEncoder similar to vkCmdBeginRenderPass.
            MTL4::RenderCommandEncoder* encoder = m_CommandBuffer->renderCommandEncoder(passDesc);
            
            encoder->setRenderPipelineState(m_Pso);
            encoder->setArgumentTable(m_ArgTables[frameIdx], MTL::RenderStageVertex);
            encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
            
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
        for (uint8_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            m_VertexBuffers[i]->release();
            m_UniformBuffers[i]->release();
            m_ArgTables[i]->release();
        }
        
        m_ResidencySet->release();
        m_Pso->release();
        
        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }
}
