// Screen-space UI. Drawn after final output into a UNORM view of the output target,
// so blending happens on sRGB-encoded values like common image editors.

// Keep the 16-byte layout in sync with ShaderConstants::UIConstants.
cbuffer UIConstants : register(b0)
{
    float2 InverseTargetSize;
    uint EncodeSrgb; // Set when the bound texture decodes sRGB to linear on sample.
    uint UIPadding;
};

Texture2D UITexture : register(t0);
SamplerState UISampler : register(s0);

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
    const float2 ndc = input.Position * InverseTargetSize * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    output.Position = float4(ndc, 0.0f, 1.0f);
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

float4 PSMain(UIVertexOutput input) : SV_TARGET
{
    float4 texel = UITexture.Sample(UISampler, input.UV);
    if (EncodeSrgb != 0)
        texel.rgb = LinearToSrgb(texel.rgb);
    const float4 color = texel * input.Color;
    // Premultiplied output; the blend state is ONE / INV_SRC_ALPHA.
    return float4(color.rgb * color.a, color.a);
}
