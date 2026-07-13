#include "MetalEngine.h"

#include "MtlUtils.h"

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
        
        // Build pso.
        MTL4::RenderPipelineDescriptor* pipelineDesc = MTL4::RenderPipelineDescriptor::alloc()->init();
        pipelineDesc->setLabel(NS::String::string("Hello Triangle PSO", NS::UTF8StringEncoding));
        pipelineDesc->setVertexFunctionDescriptor(vertexFn);
        pipelineDesc->setFragmentFunctionDescriptor(fragmentFn);
        pipelineDesc->colorAttachments()->object(0)->setPixelFormat(PIXEL_FORMAT);

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
        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }
}
