#include <metal_stdlib>
using namespace metal;

struct VertexIn
{
    float4 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
};

struct VertexOut
{
    float4 position [[position]];
    float2 texCoord;
};

struct FrameUniforms
{
    float4x4 mvp;
};

vertex
VertexOut vertexMain(VertexIn in [[stage_in]], constant FrameUniforms& uniforms [[buffer(1)]])
{
    VertexOut out;
    out.position = uniforms.mvp * in.position;
    out.texCoord = in.texCoord;
    return out;
}

fragment
float4 fragmentMain(VertexOut in [[stage_in]], texture2d<float> colorTexture [[texture(0)]], sampler colorSampler [[sampler(0)]])
{
    return colorTexture.sample(colorSampler, in.texCoord);
}