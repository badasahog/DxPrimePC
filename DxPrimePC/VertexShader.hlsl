struct VS_INPUT
{
    float4 pos : POSITION;
    float4 normal : NORMAL;
    float2 texCoord0 : TEXCOORD0_;
    float2 texCoord1: TEXCOORD1_;
};

struct VS_OUTPUT
{
    float4 pos: SV_POSITION;
    float2 texCoord: TEXCOORD;
};

cbuffer ConstantBuffer : register(b0)
{
    float4x4 wvpMat;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.pos = mul(input.pos, wvpMat);
    output.texCoord = input.texCoord0;
    return output;
}