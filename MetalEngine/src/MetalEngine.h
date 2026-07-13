#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

namespace MTLE
{
    static constexpr auto MAX_FRAMES_IN_FLIGHT = 3u;
    static constexpr auto PIXEL_FORMAT = MTL::PixelFormatBGRA8Unorm_sRGB;

    class MetalEngine
    {
    public:
        void Init();
        void Run();
        void Clean();
        
    private:
        GLFWwindow* m_Window;
        int m_Width = 800;
        int m_Height = 600;
        
        MTL::Device* m_Device = nullptr;
        CA::MetalLayer* m_MetalLayer = nullptr;
        
        MTL4::CommandQueue* m_CommandQueue = nullptr;
        MTL4::CommandBuffer* m_CommandBuffer = nullptr;
        std::array<MTL4::CommandAllocator*, MAX_FRAMES_IN_FLIGHT> m_CommandAllocators;
        
        MTL::SharedEvent* m_FrameAvailableSharedEvent = nullptr;
        size_t m_FrameNum = 0;
        
        MTL::RenderPipelineState* m_Pso = nullptr;
    };
}
