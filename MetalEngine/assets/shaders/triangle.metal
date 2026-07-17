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

vertex
VertexOut vertexMain(VertexIn in [[stage_in]])
{
    VertexOut out;
    out.position = in.position;
    out.color = in.color;
    return out;
}

fragment
float4 fragmentMain(const VertexOut in [[stage_in]])
{
    return in.color;
}