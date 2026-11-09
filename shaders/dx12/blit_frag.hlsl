Texture2D<float4> tex : register(t0, space2);
SamplerState _tex_sampler : register(s0, space2);

static float4 out_color;
static float2 v_uv;
static float4 v_tint;

struct SPIRV_Cross_Input
{
    float2 v_uv : TEXCOORD0;
    float4 v_tint : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float4 out_color : SV_Target0;
};

void main_inner()
{
    out_color = tex.Sample(_tex_sampler, v_uv) * v_tint;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    v_uv = stage_input.v_uv;
    v_tint = stage_input.v_tint;
    main_inner();
    SPIRV_Cross_Output stage_output;
    stage_output.out_color = out_color;
    return stage_output;
}
