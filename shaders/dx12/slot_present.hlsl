static const uint3 gl_WorkGroupSize = uint3(8u, 8u, 1u);

cbuffer Params : register(b0, space2)
{
    uint4 _23_info : packoffset(c0);
};

ByteAddressBuffer _47 : register(t0, space0);
RWTexture2D<unorm float4> out_img : register(u0, space1);

static uint3 gl_GlobalInvocationID;
struct SPIRV_Cross_Input
{
    uint3 gl_GlobalInvocationID : SV_DispatchThreadID;
};

void main_inner()
{
    uint2 p = gl_GlobalInvocationID.xy;
    bool _29 = p.x >= _23_info.x;
    bool _39;
    if (!_29)
    {
        _39 = p.y >= _23_info.y;
    }
    else
    {
        _39 = _29;
    }
    if (_39)
    {
        return;
    }
    uint v = _47.Load(((p.y * _23_info.x) + p.x) * 4 + 0);
    float4 c;
    if (_23_info.z == 0u)
    {
        float3 rgb = float3(float((v >> uint(10)) & 31u), float((v >> uint(5)) & 31u), float(v & 31u)) / 31.0f.xxx;
        c = float4(rgb, 1.0f);
    }
    else
    {
        if (_23_info.z == 3u)
        {
            float g = float(v & 255u) / 255.0f;
            c = float4(g, g, g, 1.0f);
        }
        else
        {
            float _130;
            if (_23_info.z == 2u)
            {
                _130 = float(v >> uint(24));
            }
            else
            {
                _130 = 255.0f;
            }
            c = float4(float((v >> uint(16)) & 255u), float((v >> uint(8)) & 255u), float(v & 255u), _130) / 255.0f.xxxx;
        }
    }
    out_img[int2(p)] = c;
}

[numthreads(8, 8, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    main_inner();
}
