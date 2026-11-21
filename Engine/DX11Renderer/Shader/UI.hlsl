// Screen- and world-space UI. Drawn after final output into a UNORM view of the output target,
// so blending happens on sRGB-encoded values like common image editors.
// World-space commands compare against the scene depth texture here instead of binding a
// depth-stencil view, because the camera depth may be multisampled while the UI target is not.
// UI_DEPTH_MS=1 compiles the variant that reads a multisampled depth texture (sample 0).

// Keep the 80-byte layout in sync with ShaderConstants::UIConstants.
cbuffer UIConstants : register(b0)
{
    // Canvas pixels -> clip space. Screen space: an orthographic pixel mapping.
    // World space: canvas -> world -> view -> projection (row vectors, row-major packing).
    float4x4 ClipFromCanvas;
    uint EncodeSrgb; // Set when the bound texture decodes sRGB to linear on sample.
    uint DepthTest;  // Set for world-space commands: hide fragments behind the scene depth.
    uint2 UIPadding;
};

Texture2D UITexture : register(t0);
SamplerState UISampler : register(s0);
#if UI_DEPTH_MS
Texture2DMS<float> SceneDepth : register(t1);
#else
Texture2D<float> SceneDepth : register(t1);
#endif

struct UIVertexInput
{
    float2 Position : POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR0;
};

struct UIVertexOutput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR0;
};

UIVertexOutput VSMain(UIVertexInput input)
{
    UIVertexOutput output;
    output.Position = mul(float4(input.Position, 0.0f, 1.0f), ClipFromCanvas);
    output.UV = input.UV;
    output.Color = input.Color;
    return output;
}

float3 LinearToSrgb(float3 color)
{
    color = saturate(color);
    const float3 low = color * 12.92f;
    const float3 high = 1.055f * pow(color, 1.0f / 2.4f) - 0.055f;
    return (color <= 0.0031308f) ? low : high; // Component-wise in SM 5.0.
}

float LoadSceneDepth(int2 pixel)
{
#if UI_DEPTH_MS
    return SceneDepth.Load(pixel, 0);
#else
    return SceneDepth.Load(int3(pixel, 0));
#endif
}

float4 PSMain(UIVertexOutput input) : SV_TARGET
{
    // LESS_EQUAL against the scene: SV_Position.z is the same viewport depth the scene wrote.
    if (DepthTest != 0 && input.Position.z > LoadSceneDepth(int2(input.Position.xy)))
        discard;

    float4 texel = UITexture.Sample(UISampler, input.UV);
    if (EncodeSrgb != 0)
        texel.rgb = LinearToSrgb(texel.rgb);
    const float4 color = texel * input.Color;
    // Premultiplied output; the blend state is ONE / INV_SRC_ALPHA.
    return float4(color.rgb * color.a, color.a);
}
