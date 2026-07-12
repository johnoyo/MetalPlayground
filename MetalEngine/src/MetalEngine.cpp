#include "MetalEngine.h"

#include "MtlUtils.h"

#include <iostream>

namespace MTLE
{
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
            
            MTL4::RenderPassDescriptor* passDesc = MTL4::RenderPassDescriptor::alloc()->init();
            MTL::RenderPassColorAttachmentDescriptor* colorAttachment = passDesc->colorAttachments()->object(0);
            colorAttachment->setTexture(surface->texture());
            colorAttachment->setLoadAction(MTL::LoadActionClear);
            colorAttachment->setStoreAction(MTL::StoreActionStore);
            colorAttachment->setClearColor(MTL::ClearColor::Make(1.0, 0.4118, 0.7059, 1.0)); // pink
            
            MTL4::RenderCommandEncoder* encoder = m_CommandBuffer->renderCommandEncoder(passDesc);
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
