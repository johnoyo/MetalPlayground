#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

namespace MTLE
{
    struct Vertex
    {
        alignas(16) glm::vec4 position;
        alignas(16) glm::vec4 color;
    };

    struct FrameUniforms
    {
        glm::mat4 rotation;
    };
    static_assert(sizeof(FrameUniforms) == 64, "FrameUniforms must match the MSL layout exactly");

    static constexpr auto MAX_FRAMES_IN_FLIGHT = 3u;
    static constexpr auto PIXEL_FORMAT = MTL::PixelFormatBGRA8Unorm_sRGB;
    static constexpr uint32_t VERTEX_BUFFER_BINDING_IDX = 0;
    static constexpr uint32_t UNIFORM_BUFFER_BINDING_IDX = 1;

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
        std::array<MTL::Buffer*, MAX_FRAMES_IN_FLIGHT> m_VertexBuffers;
        std::array<MTL::Buffer*, MAX_FRAMES_IN_FLIGHT> m_UniformBuffers;
        std::array<MTL4::ArgumentTable*, MAX_FRAMES_IN_FLIGHT> m_ArgTables;
        MTL::ResidencySet* m_ResidencySet = nullptr;
    };
}
