cbuffer Params : register(b0, space1)
{
    float4 _32_rect : packoffset(c0);
    float4 _32_uv_rect : packoffset(c1);
    float4 _32_tint : packoffset(c2);
};


static float4 gl_Position;
static int gl_VertexIndex;
static float2 v_uv;
static float4 v_tint;

struct SPIRV_Cross_Input
{
    uint gl_VertexIndex : SV_VertexID;
};

struct SPIRV_Cross_Output
{
    float2 v_uv : TEXCOORD0;
    float4 v_tint : TEXCOORD1;
    float4 gl_Position : SV_Position;
};

void main_inner()
{
    float2 c = float2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    gl_Position = float4(lerp(_32_rect.xy, _32_rect.zw, c), 0.0f, 1.0f);
    v_uv = lerp(_32_uv_rect.xy, _32_uv_rect.zw, c);
    v_tint = _32_tint;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_VertexIndex = int(stage_input.gl_VertexIndex);
    main_inner();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.v_uv = v_uv;
    stage_output.v_tint = v_tint;
    return stage_output;
}
