static const uint3 gl_WorkGroupSize = uint3(8u, 8u, 1u);

static const uint _507[7] = { 2u, 3u, 4u, 6u, 7u, 8u, 9u };

cbuffer Params : register(b0, space2)
{
    uint4 _2015_sizes : packoffset(c0);
    int4 _2015_origin : packoffset(c1);
    int4 _2015_sampling : packoffset(c2);
    uint4 _2015_job : packoffset(c3);
};

ByteAddressBuffer _2031 : register(t0, space0);
RWByteAddressBuffer _2224 : register(u0, space1);

static uint3 gl_GlobalInvocationID;
struct SPIRV_Cross_Input
{
    uint3 gl_GlobalInvocationID : SV_DispatchThreadID;
};

uint source_at(int2 p)
{
    bool _2007 = any(bool2(p.x < int2(0, 0).x, p.y < int2(0, 0).y));
    bool _2024;
    if (!_2007)
    {
        int2 _2021 = int2(_2015_sizes.zw);
        _2024 = any(bool2(p.x >= _2021.x, p.y >= _2021.y));
    }
    else
    {
        _2024 = _2007;
    }
    if (_2024)
    {
        return 0u;
    }
    return _2031.Load(((uint(p.y) * _2015_sizes.z) + uint(p.x)) * 4 + 0);
}

uint translated_sample(int2 p)
{
    int2 fixed_pos = (p << int2(16, 16)) - _2015_sampling.xy;
    int2 q = fixed_pos >> int2(16, 16);
    int2 f = (fixed_pos & int2(65024, 65024)) >> int2(9, 9);
    int2 param = q;
    int2 param_1 = q + int2(1, 0);
    int2 param_2 = q + int2(0, 1);
    int2 param_3 = q + int2(1, 1);
    uint _2090[4] = { source_at(param), source_at(param_1), source_at(param_2), source_at(param_3) };
    uint taps[4] = _2090;
    uint result = 0u;
    for (int shift = 0; shift < 32; shift += 8)
    {
        int a = int((taps[0] >> uint(shift)) & 255u);
        int b = int((taps[1] >> uint(shift)) & 255u);
        int c = int((taps[2] >> uint(shift)) & 255u);
        int d = int((taps[3] >> uint(shift)) & 255u);
        int top = (((a - b) * (128 - f.x)) >> 7) + b;
        int bottom = (((c - d) * (128 - f.x)) >> 7) + d;
        result |= (uint(clamp((((top - bottom) * (128 - f.y)) >> 7) + bottom, 0, 255)) << uint(shift));
    }
    return result;
}

int4 px_unpack(uint v)
{
    return int4(int(v & 255u), int((v >> uint(8)) & 255u), int((v >> uint(16)) & 255u), int(v >> uint(24)));
}

uint px_pack(int4 c)
{
    return ((uint(c.x & 255) | (uint(c.y & 255) << uint(8))) | (uint(c.z & 255) << uint(16))) | (uint(c.w & 255) << uint(24));
}

uint crossfade_pixel(uint av, uint bv, uint ratio, bool has_alpha)
{
    uint param = av;
    int4 a = px_unpack(param);
    uint param_1 = bv;
    int4 b = px_unpack(param_1);
    if (!has_alpha)
    {
        int4 param_2 = clamp(a + (((b - a) * int(ratio).xxxx) >> int4(8, 8, 8, 8)), int4(0, 0, 0, 0), int4(255, 255, 255, 255));
        return px_pack(param_2);
    }
    uint wa = uint(a.w) * (256u - ratio);
    uint wb = uint(b.w) * ratio;
    uint sum = (wa + wb) & 65535u;
    if (sum == 0u)
    {
        return 0u;
    }
    int w = int((wa << uint(7)) / sum);
    int3 rgb = clamp(b.xyz + (((a.xyz - b.xyz) * w.xxx) >> int3(7, 7, 7)), int3(0, 0, 0), int3(255, 255, 255));
    int4 param_3 = int4(rgb, int((sum >> uint(8)) & 255u));
    return px_pack(param_3);
}

uint over_int(int4 s, int4 d, int Q)
{
    int sa = s.w;
    int da = d.w;
    int e = sa * Q;
    int ecx = (da * (65536 - e)) >> 8;
    int edi = ecx + e;
    if (edi == 0)
    {
        int4 param = d;
        return px_pack(param);
    }
    int ws = (e << 8) / edi;
    int wd = (ecx << 8) / edi;
    int3 _311 = (((d.xyz * wd.xxx) + (s.xyz * ws.xxx)) >> int3(8, 8, 8)) & int3(255, 255, 255);
    int4 o;
    o.x = _311.x;
    o.y = _311.y;
    o.z = _311.z;
    o.w = edi >> 8;
    int4 param_1 = o;
    return px_pack(param_1);
}

int sat255(int x)
{
    return clamp(x, 0, 255);
}

uint mode1_src_over_noalpha_dst(int4 s, int4 d, int P)
{
    if ((s.w & 254) == 0)
    {
        int4 param = d;
        return px_pack(param);
    }
    int w = ((s.w >> 1) * (256 - P)) >> 8;
    int param_1 = d.x + (((s.x - d.x) * w) >> 7);
    int param_2 = d.y + (((s.y - d.y) * w) >> 7);
    int param_3 = d.z + (((s.z - d.z) * w) >> 7);
    int4 o = int4(sat255(param_1), sat255(param_2), sat255(param_3), d.w);
    int4 param_4 = o;
    return px_pack(param_4);
}

int3 rgb555(uint v)
{
    return int3(int((v >> uint(10)) & 31u), int((v >> uint(5)) & 31u), int(v & 31u));
}

uint rgb555_pack(int3 c)
{
    return ((uint(c.x) << uint(10)) | (uint(c.y) << uint(5))) | uint(c.z);
}

uint mix89(int4 s, int4 d, int P, bool hard)
{
    int4 o = d;
    bool _408;
    int _424;
    for (int c = 0; c < 3; c++)
    {
        if (hard)
        {
            _408 = s[c] > 127;
        }
        else
        {
            _408 = d[c] > 127;
        }
        bool m = _408;
        if (m)
        {
            _424 = ((2 * (s[c] + d[c])) - 255) - ((s[c] * d[c]) >> 7);
        }
        else
        {
            _424 = (s[c] * d[c]) >> 7;
        }
        int r = _424;
        int param = d[c] + (((r - d[c]) * P) >> 8);
        o[c] = sat255(param);
    }
    int4 param_1 = o;
    return px_pack(param_1);
}

uint blend_pixel(inout uint mode, uint sf, uint df, inout int P, uint sv, uint dv)
{
    if (mode == 32u)
    {
        mode = 1u;
    }
    else
    {
        if (mode == 192u)
        {
            mode = 5u;
        }
        else
        {
            if ((mode >= 33u) && (mode <= 39u))
            {
                mode = _507[mode - 33u];
                P = 256 - P;
            }
        }
    }
    uint param = sv;
    int4 s = px_unpack(param);
    uint param_1 = dv;
    int4 d = px_unpack(param_1);
    switch (mode)
    {
        case 129u:
        {
            uint param_2 = dv;
            uint param_3 = sv;
            uint param_4 = uint(P);
            bool param_5 = sf == 2u;
            return crossfade_pixel(param_2, param_3, param_4, param_5);
        }
        case 0u:
        {
            if ((sf == 0u) && (df == 0u))
            {
                return (sv != 0u) ? sv : dv;
            }
            if ((sf == 1u) && (df == 1u))
            {
                return sv;
            }
            if ((sf == 1u) && (df == 2u))
            {
                return sv | 4278190080u;
            }
            if ((sf == 2u) && (df == 2u))
            {
                if (s.w == 0)
                {
                    return dv;
                }
                if (s.w == 255)
                {
                    return sv;
                }
                int4 param_6 = s;
                int4 param_7 = d;
                int param_8 = 256;
                return over_int(param_6, param_7, param_8);
            }
            if ((sf == 2u) && (df == 1u))
            {
                if (s.w == 255)
                {
                    return (sv & 16777215u) | (dv & 4278190080u);
                }
                int4 param_9 = s;
                int4 param_10 = d;
                int param_11 = 0;
                return mode1_src_over_noalpha_dst(param_9, param_10, param_11);
            }
            break;
        }
        case 1u:
        {
            if ((sf == 2u) && (df == 1u))
            {
                int4 param_12 = s;
                int4 param_13 = d;
                int param_14 = P;
                return mode1_src_over_noalpha_dst(param_12, param_13, param_14);
            }
            if ((sf == 1u) && (df == 1u))
            {
                int w = P >> 1;
                int4 o;
                for (int c = 0; c < 4; c++)
                {
                    int param_15 = s[c] + (((d[c] - s[c]) * w) >> 7);
                    o[c] = sat255(param_15);
                }
                int4 param_16 = o;
                return px_pack(param_16);
            }
            if ((sf == 2u) && (df == 2u))
            {
                if (s.w == 0)
                {
                    return dv;
                }
                int4 param_17 = s;
                int4 param_18 = d;
                int param_19 = 256 - P;
                return over_int(param_17, param_18, param_19);
            }
            if ((sf == 0u) && (df == 0u))
            {
                if (sv == 0u)
                {
                    return dv;
                }
                uint param_20 = sv;
                int3 a = rgb555(param_20);
                uint param_21 = dv;
                int3 b = rgb555(param_21);
                int3 o_1 = min(int3(31, 31, 31), (((a * (256 - P).xxx) + (b * P.xxx)) >> int3(8, 8, 8)));
                int3 param_22 = o_1;
                return rgb555_pack(param_22);
            }
            if ((sf == 1u) && (df == 2u))
            {
                uint e = uint(255 * (256 - P));
                uint ecx = (uint(d.w) * (65536u - e)) >> uint(8);
                uint edi = ecx + e;
                if (edi == 0u)
                {
                    return dv;
                }
                uint ws = (e << uint(16)) / edi;
                uint wd = (ecx << uint(16)) / edi;
                int3 _825 = int3((((uint3(s.xyz) * ws.xxx) + (uint3(d.xyz) * wd.xxx)) >> uint3(int3(16, 16, 16))) & uint3(255u, 255u, 255u));
                int4 o_2;
                o_2.x = _825.x;
                o_2.y = _825.y;
                o_2.z = _825.z;
                o_2.w = int(edi >> uint(8));
                int4 param_23 = o_2;
                return px_pack(param_23);
            }
            break;
        }
        case 2u:
        {
            if ((sf == 2u) && (df == 2u))
            {
                if ((s.w & 254) == 0)
                {
                    return dv;
                }
                int f = ((s.w >> 1) * P) >> 8;
                int4 o_3;
                for (int c_1 = 0; c_1 < 3; c_1++)
                {
                    o_3[c_1] = min(255, (d[c_1] + ((s[c_1] * f) >> 7)));
                }
                o_3.w = min(255, (d.w + ((s.w * (P >> 1)) >> 7)));
                int4 param_24 = o_3;
                return px_pack(param_24);
            }
            if ((sf == 2u) && (df == 1u))
            {
                if ((s.w & 254) == 0)
                {
                    return dv;
                }
                int w_1 = ((s.w >> 1) * P) >> 8;
                int4 o_4 = d;
                for (int c_2 = 0; c_2 < 3; c_2++)
                {
                    o_4[c_2] = min(255, (d[c_2] + ((s[c_2] * w_1) >> 7)));
                }
                int4 param_25 = o_4;
                return px_pack(param_25);
            }
            bool _954 = sf == 1u;
            bool _962;
            if (_954)
            {
                _962 = (df == 1u) || (df == 2u);
            }
            else
            {
                _962 = _954;
            }
            if (_962)
            {
                if (((s.x + s.y) + s.z) == 0)
                {
                    return dv;
                }
                int4 o_5 = d;
                for (int c_3 = 0; c_3 < 3; c_3++)
                {
                    o_5[c_3] = min(255, (d[c_3] + ((s[c_3] * P) >> 8)));
                }
                if (df == 2u)
                {
                    o_5.w = min(255, (d.w + P));
                }
                int4 param_26 = o_5;
                return px_pack(param_26);
            }
            if ((sf == 0u) && (df == 0u))
            {
                if (sv == 0u)
                {
                    return dv;
                }
                uint param_27 = sv;
                uint param_28 = dv;
                int3 o_6 = min(int3(31, 31, 31), (((rgb555(param_27) * P.xxx) >> int3(8, 8, 8)) + rgb555(param_28)));
                int3 param_29 = o_6;
                return rgb555_pack(param_29);
            }
            break;
        }
        case 3u:
        {
            if ((sf == 2u) && (df == 1u))
            {
                if ((s.w & 254) == 0)
                {
                    return dv;
                }
                int f_1 = ((s.w >> 1) * P) >> 8;
                int4 o_7 = d;
                for (int c_4 = 0; c_4 < 3; c_4++)
                {
                    o_7[c_4] = max(0, (d[c_4] - ((s[c_4] * f_1) >> 7)));
                }
                int4 param_30 = o_7;
                return px_pack(param_30);
            }
            break;
        }
        case 4u:
        {
            if ((sf == 1u) && (df == 1u))
            {
                int4 o_8 = d;
                for (int c_5 = 0; c_5 < 3; c_5++)
                {
                    o_8[c_5] = d[c_5] + ((((s[c_5] - 256) * (P / 2)) * (2 * d[c_5])) >> 16);
                }
                int4 param_31 = o_8;
                return px_pack(param_31);
            }
            if ((sf == 1u) && (df == 2u))
            {
                int4 o_9 = d;
                for (int c_6 = 0; c_6 < 3; c_6++)
                {
                    o_9[c_6] = d[c_6] + (((((s[c_6] * d[c_6]) >> 8) - d[c_6]) * (P >> 1)) >> 7);
                }
                int4 param_32 = o_9;
                return px_pack(param_32);
            }
            if ((sf == 2u) && (df == 1u))
            {
                if ((s.w & 254) == 0)
                {
                    return dv;
                }
                int w_2 = ((s.w >> 1) * P) >> 8;
                int4 o_10 = d;
                for (int c_7 = 0; c_7 < 3; c_7++)
                {
                    o_10[c_7] = d[c_7] + (((((s[c_7] * d[c_7]) >> 8) - d[c_7]) * w_2) >> 7);
                }
                int4 param_33 = o_10;
                return px_pack(param_33);
            }
            if ((sf == 3u) && (df == 3u))
            {
                int sg = int(sv & 255u);
                int dg = int(dv & 255u);
                int m = (((sg * dg) + 1) * 257) >> 16;
                return uint((dg + (((m - dg) * P) >> 8)) & 255);
            }
            break;
        }
        case 5u:
        {
            if ((sf == 1u) && (df == 1u))
            {
                int4 o_11;
                for (int c_8 = 0; c_8 < 4; c_8++)
                {
                    o_11[c_8] = (s[c_8] * (256 - P)) >> 8;
                }
                int4 param_34 = o_11;
                return px_pack(param_34);
            }
            if ((sf == 2u) && (df == 1u))
            {
                if ((s.w & 254) == 0)
                {
                    return dv;
                }
                int4 o_12 = d;
                for (int c_9 = 0; c_9 < 3; c_9++)
                {
                    int sp = (s[c_9] * (256 - P)) >> 8;
                    o_12[c_9] = d[c_9] + (((sp - d[c_9]) * (s.w >> 1)) >> 7);
                }
                int4 param_35 = o_12;
                return px_pack(param_35);
            }
            if ((sf == 0u) && (df == 0u))
            {
                uint param_36 = sv;
                int3 param_37 = (rgb555(param_36) * (256 - P).xxx) >> int3(8, 8, 8);
                return rgb555_pack(param_37);
            }
            if ((sf == 2u) && (df == 2u))
            {
                if (s.w == 0)
                {
                    return dv;
                }
                uint Q = uint(256 - P);
                uint sa = uint(s.w);
                uint da = uint(d.w);
                uint den = (256u * sa) + ((256u - sa) * da);
                uint ws_1 = ((sa * Q) << uint(16)) / den;
                uint wd_1 = (((256u - sa) * da) << uint(16)) / den;
                int3 _1452 = int3(((uint3(s.xyz) * ws_1.xxx) + (uint3(d.xyz) * wd_1.xxx)) >> uint3(int3(16, 16, 16)));
                int4 o_13;
                o_13.x = _1452.x;
                o_13.y = _1452.y;
                o_13.z = _1452.z;
                o_13.w = int(den >> uint(8));
                int4 param_38 = o_13;
                return px_pack(param_38);
            }
            break;
        }
        case 6u:
        {
            if ((sf == 1u) && (df == 1u))
            {
                int4 o_14 = d;
                for (int c_10 = 0; c_10 < 3; c_10++)
                {
                    int sp_1 = (s[c_10] * P) >> 8;
                    int param_39 = (d[c_10] + sp_1) - ((sp_1 * d[c_10]) >> 8);
                    o_14[c_10] = sat255(param_39);
                }
                int4 param_40 = o_14;
                return px_pack(param_40);
            }
            if ((sf == 2u) && (df == 1u))
            {
                if (s.w == 0)
                {
                    return dv;
                }
                int4 o_15 = d;
                for (int c_11 = 0; c_11 < 3; c_11++)
                {
                    int sp_2 = (s[c_11] * ((s.w * P) >> 8)) >> 8;
                    int param_41 = (d[c_11] + sp_2) - ((sp_2 * d[c_11]) >> 8);
                    o_15[c_11] = sat255(param_41);
                }
                int4 param_42 = o_15;
                return px_pack(param_42);
            }
            break;
        }
        case 7u:
        {
            if (s.w == 0)
            {
                return dv;
            }
            if ((sf == 2u) && (df == 1u))
            {
                int t = 256 - ((s.w * P) >> 8);
                int4 o_16 = d;
                for (int c_12 = 0; c_12 < 3; c_12++)
                {
                    o_16[c_12] = (d[c_12] * t) >> 8;
                }
                int4 param_43 = o_16;
                return px_pack(param_43);
            }
            if ((sf == 2u) && (df == 2u))
            {
                int4 o_17 = d;
                int _1628;
                if (P < 256)
                {
                    _1628 = (d.w * (65536 - (s.w * P))) >> 16;
                }
                else
                {
                    _1628 = (d.w * (256 - s.w)) >> 8;
                }
                o_17.w = _1628;
                int4 param_44 = o_17;
                return px_pack(param_44);
            }
            if ((sf == 2u) && (df == 3u))
            {
                int dg_1 = int(dv & 255u);
                int _1667;
                if (P < 256)
                {
                    _1667 = (dg_1 * (65280 - (s.w * P))) >> 16;
                }
                else
                {
                    _1667 = (dg_1 * (255 - s.w)) >> 8;
                }
                return uint(_1667 & 255);
            }
            break;
        }
        case 8u:
        {
            if ((sf == 1u) && (df == 1u))
            {
                int4 param_45 = s;
                int4 param_46 = d;
                int param_47 = P;
                bool param_48 = false;
                return mix89(param_45, param_46, param_47, param_48);
            }
            break;
        }
        case 9u:
        {
            if ((sf == 1u) && (df == 1u))
            {
                int4 param_49 = s;
                int4 param_50 = d;
                int param_51 = P;
                bool param_52 = true;
                return mix89(param_49, param_50, param_51, param_52);
            }
            break;
        }
        case 64u:
        {
            if ((sf == 2u) && (df == 2u))
            {
                bool _1736;
                if (P != 0)
                {
                    _1736 = s.w != 0;
                }
                else
                {
                    _1736 = s.w == 255;
                }
                return _1736 ? 0u : dv;
            }
            if ((sf == 1u) && (df == 1u))
            {
                return (((s.x + s.y) + s.z) != 0) ? 0u : dv;
            }
            if ((sf == 0u) && (df == 0u))
            {
                return (sv != 0u) ? 0u : dv;
            }
            break;
        }
        case 128u:
        {
            if (sf == df)
            {
                return sv;
            }
            if ((sf == 1u) && (df == 2u))
            {
                return sv | 4278190080u;
            }
            if ((sf == 2u) && (df == 1u))
            {
                int4 o_18;
                for (int c_13 = 0; c_13 < 4; c_13++)
                {
                    o_18[c_13] = (s[c_13] * (s.w >> 1)) >> 7;
                }
                int4 param_53 = o_18;
                return px_pack(param_53);
            }
            if ((sf == 3u) && (df == 2u))
            {
                return (sv & 255u) << uint(24);
            }
            return dv;
        }
        case 193u:
        {
            if ((sf == 1u) && (df == 1u))
            {
                int4 o_19 = s;
                for (int c_14 = 0; c_14 < 3; c_14++)
                {
                    o_19[c_14] = ((s[c_14] * (256 - P)) + (255 * P)) >> 8;
                }
                int4 param_54 = o_19;
                return px_pack(param_54);
            }
            break;
        }
        case 240u:
        {
            if ((sf == 1u) && (df == 1u))
            {
                int w_3 = P >> 1;
                int4 o_20;
                for (int c_15 = 0; c_15 < 4; c_15++)
                {
                    int param_55 = s[c_15] + (((d[c_15] - s[c_15]) * w_3) >> 7);
                    o_20[c_15] = sat255(param_55);
                }
                int4 param_56 = o_20;
                return px_pack(param_56);
            }
            if ((sf == 0u) && (df == 0u))
            {
                uint param_57 = sv;
                int3 a_1 = rgb555(param_57);
                uint param_58 = dv;
                int3 b_1 = rgb555(param_58);
                int3 param_59 = min(int3(31, 31, 31), (((a_1 * (256 - P).xxx) + (b_1 * P.xxx)) >> int3(8, 8, 8)));
                return rgb555_pack(param_59);
            }
            break;
        }
        case 255u:
        {
            if ((sf == 2u) && (df == 1u))
            {
                if (P == 0)
                {
                    return 4278190080u | uint(s.x);
                }
                if (P == 1)
                {
                    return 4278190080u | (uint(s.y) << uint(8));
                }
                if (P == 2)
                {
                    return 4278190080u | (uint(s.z) << uint(16));
                }
                return 4278190080u | (uint(s.w) * 65793u);
            }
            break;
        }
    }
    return 3735928559u;
}

void comp_main()
{
    uint2 p = gl_GlobalInvocationID.xy;
    bool _2183 = p.x >= _2015_job.x;
    bool _2192;
    if (!_2183)
    {
        _2192 = p.y >= _2015_job.y;
    }
    else
    {
        _2192 = _2183;
    }
    if (_2192)
    {
        return;
    }
    uint di = (uint(_2015_origin.y + int(p.y)) * _2015_sizes.x) + uint(_2015_origin.x + int(p.x));
    if (_2015_job.w == 1u)
    {
        _2224.Store(di * 4 + 0, uint(_2015_origin.z));
        return;
    }
    uint mode = _2015_job.z & 255u;
    uint sf = (_2015_job.z >> uint(8)) & 3u;
    uint df = (_2015_job.z >> uint(10)) & 3u;
    int P = int(_2015_job.z >> uint(16));
    if (_2015_job.w == 2u)
    {
        int2 param = _2015_origin.zw + int2(p);
        uint source = translated_sample(param);
        uint param_1 = mode;
        uint param_2 = sf;
        uint param_3 = df;
        int param_4 = P;
        uint param_5 = source;
        uint param_6 = _2224.Load(di * 4 + 0);
        uint _2279 = blend_pixel(param_1, param_2, param_3, param_4, param_5, param_6);
        _2224.Store(di * 4 + 0, _2279);
        return;
    }
    uint si = (uint(_2015_origin.w + int(p.y)) * _2015_sizes.z) + uint(_2015_origin.z + int(p.x));
    uint param_7 = mode;
    uint param_8 = sf;
    uint param_9 = df;
    int param_10 = P;
    uint param_11 = _2031.Load(si * 4 + 0);
    uint param_12 = _2224.Load(di * 4 + 0);
    uint _2318 = blend_pixel(param_7, param_8, param_9, param_10, param_11, param_12);
    _2224.Store(di * 4 + 0, _2318);
}

[numthreads(8, 8, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    comp_main();
}
