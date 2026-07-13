#include <metal_stdlib>

using namespace metal;

constant float4 positions[3] =
{
    {  0.0,  0.5, 0.0, 1.0 },
    {  0.5, -0.5, 0.0, 1.0 },
    { -0.5, -0.5, 0.0, 1.0 }
};

constant float4 colors[3] =
{
    { 1, 0, 0, 1 },
    { 0, 1, 0, 1 },
    { 0, 0, 1, 1 }
};

struct VertexOut
{
    float4 position [[position]];
    float4 color;
};

vertex
VertexOut vertexMain(uint id [[vertex_id]])
{
    return
    {
        .position = positions[ id ],
        .color    = colors[ id ]
    };
}

fragment
float4 fragmentMain(const VertexOut in [[stage_in]])
{
    return in.color;
}