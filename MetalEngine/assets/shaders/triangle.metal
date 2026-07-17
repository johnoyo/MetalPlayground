#include <metal_stdlib>
using namespace metal;

struct VertexIn
{
    float4 position [[attribute(0)]];
    float4 color    [[attribute(1)]];
};

struct VertexOut
{
    float4 position [[position]];
    float4 color;
};

struct FrameUniforms
{
    float4x4 rotation;
};

vertex
VertexOut vertexMain(VertexIn in [[stage_in]], constant FrameUniforms& uniforms [[buffer(1)]])
{
    VertexOut out;
    out.position = uniforms.rotation * in.position;
    out.color = in.color;
    return out;
}

fragment
float4 fragmentMain(const VertexOut in [[stage_in]])
{
    return in.color;
}