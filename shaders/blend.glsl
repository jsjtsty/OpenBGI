// Bit-exact integer ports of the original engine's CPU blend kernels (analysis/混合公式.md).
// Pixels are packed little-endian as in memory: 32-bit = B | G<<8 | R<<16 | A<<24, 16-bit = RGB555, 8-bit = gray.
// Formats: 0 = 16-bit, 1 = 32-bit without alpha, 2 = 32-bit with alpha, 3 = 8-bit.
// These match the scalar path of the originals; the SIMD pair path differs by at most 1..3 levels (rcpps etc.).
#ifndef OBGI_BLEND_GLSL
#define OBGI_BLEND_GLSL

ivec4 px_unpack(uint v) { return ivec4(v & 255u, (v >> 8) & 255u, (v >> 16) & 255u, v >> 24); }
uint px_pack(ivec4 c) { return uint(c.x & 255) | uint(c.y & 255) << 8 | uint(c.z & 255) << 16 | uint(c.w & 255) << 24; }
int sat255(int x) { return clamp(x, 0, 255); }

// CrossfadeTo: ratio 0 keeps a, 256 takes b. Unlike source-over, alpha is
// interpolated rather than accumulated, including when a and b overlap.
uint crossfade_pixel(uint av, uint bv, uint ratio, bool has_alpha) {
    ivec4 a = px_unpack(av), b = px_unpack(bv);
    if (!has_alpha) return px_pack(clamp(a + (((b - a) * int(ratio)) >> 8), 0, 255));
    uint wa = uint(a.w) * (256u - ratio), wb = uint(b.w) * ratio;
    uint sum = (wa + wb) & 65535u;
    if (sum == 0u) return 0u;
    int w = int((wa << 7) / sum);
    ivec3 rgb = clamp(b.xyz + (((a.xyz - b.xyz) * w) >> 7), 0, 255);
    return px_pack(ivec4(rgb, int((sum >> 8) & 255u)));
}

ivec3 rgb555(uint v) { return ivec3((v >> 10) & 31u, (v >> 5) & 31u, v & 31u); }
uint rgb555_pack(ivec3 c) { return uint(c.x) << 10 | uint(c.y) << 5 | uint(c.z); }

// Over with the exact integer model of 0040cda0 (Q = 256 - P, or 256 for mode 0), including the alpha OR quirk.
uint over_int(ivec4 s, ivec4 d, int Q) {
    int sa = s.w, da = d.w;
    int e = sa * Q;
    int ecx = (da * (0x10000 - e)) >> 8;
    int edi = ecx + e;
    if (edi == 0) return px_pack(d);
    int ws = (e << 8) / edi, wd = (ecx << 8) / edi;
    ivec4 o;
    o.xyz = ((d.xyz * wd + s.xyz * ws) >> 8) & 255;
    o.w = (edi >> 8) | min(255, (sa * ws + da * wd) >> 8);
    return px_pack(o);
}

uint mode1_src_over_noalpha_dst(ivec4 s, ivec4 d, int P) { // 2 -> 1, also mode 0 with P = 0
    if ((s.w & 0xfe) == 0) return px_pack(d);
    int w = ((s.w >> 1) * (256 - P)) >> 8;
    ivec4 o = ivec4(sat255(d.x + (((s.x - d.x) * w) >> 7)), sat255(d.y + (((s.y - d.y) * w) >> 7)),
                    sat255(d.z + (((s.z - d.z) * w) >> 7)), d.w);
    return px_pack(o);
}

uint mix89(ivec4 s, ivec4 d, int P, bool hard) {
    ivec4 o = d;
    for (int c = 0; c < 3; ++c) {
        bool m = hard ? s[c] > 127 : d[c] > 127;
        int r = m ? 2 * (s[c] + d[c]) - 255 - ((s[c] * d[c]) >> 7) : (s[c] * d[c]) >> 7;
        o[c] = sat255(d[c] + (((r - d[c]) * P) >> 8));
    }
    return px_pack(o);
}

// mode: script-level blend mode (aliases resolved here like FUN_0040b9d0), sf/df: source/destination format,
// P: strength/transparency parameter 0..0x100 as the mode defines it.
uint blend_pixel(uint mode, uint sf, uint df, int P, uint sv, uint dv) {
    // dispatcher aliases
    if (mode == 0x20u) mode = 1u;
    else if (mode == 0xC0u) mode = 5u;
    else if (mode >= 0x21u && mode <= 0x27u) {
        const uint tab[7] = uint[7](2u, 3u, 4u, 6u, 7u, 8u, 9u);
        mode = tab[mode - 0x21u];
        P = 256 - P;
    }
    ivec4 s = px_unpack(sv), d = px_unpack(dv);

    switch (mode) {
    case 0x81u: // runtime-internal rectangular CrossfadeTo (not a script blend mode)
        return crossfade_pixel(dv, sv, uint(P), sf == 2u);
    case 0u:
        if (sf == 0u && df == 0u) return sv != 0u ? sv : dv;
        if (sf == 1u && df == 1u) return sv;
        if (sf == 1u && df == 2u) return sv | 0xff000000u;
        if (sf == 2u && df == 2u) {
            if (s.w == 0) return dv;
            if (s.w == 255) return sv;
            return over_int(s, d, 256);
        }
        if (sf == 2u && df == 1u) {
            if (s.w == 255) return (sv & 0x00ffffffu) | (dv & 0xff000000u);
            return mode1_src_over_noalpha_dst(s, d, 0);
        }
        break;
    case 1u:
        if (sf == 2u && df == 1u) return mode1_src_over_noalpha_dst(s, d, P);
        if (sf == 1u && df == 1u) {
            int w = P >> 1;
            ivec4 o;
            for (int c = 0; c < 4; ++c) o[c] = sat255(s[c] + (((d[c] - s[c]) * w) >> 7));
            return px_pack(o);
        }
        if (sf == 2u && df == 2u) {
            if (s.w == 0) return dv;
            return over_int(s, d, 256 - P);
        }
        if (sf == 0u && df == 0u) {
            if (sv == 0u) return dv;
            ivec3 a = rgb555(sv), b = rgb555(dv);
            ivec3 o = min(ivec3(31), (a * (256 - P) + b * P) >> 8);
            return rgb555_pack(o);
        }
        if (sf == 1u && df == 2u) {
            uint e = uint(255 * (256 - P));
            uint ecx = (uint(d.w) * (0x10000u - e)) >> 8;
            uint edi = ecx + e;
            if (edi == 0u) return dv;
            uint ws = (e << 16) / edi, wd = (ecx << 16) / edi;
            ivec4 o;
            o.xyz = ivec3(((uvec3(s.xyz) * ws + uvec3(d.xyz) * wd) >> 16) & 255u);
            o.w = int(edi >> 8);
            return px_pack(o);
        }
        break;
    case 2u:
        if (sf == 2u && df == 2u) {
            if ((s.w & 0xfe) == 0) return dv;
            int f = ((s.w >> 1) * P) >> 8;
            ivec4 o;
            for (int c = 0; c < 3; ++c) o[c] = min(255, d[c] + ((s[c] * f) >> 7));
            o.w = min(255, d.w + ((s.w * (P >> 1)) >> 7));
            return px_pack(o);
        }
        if (sf == 2u && df == 1u) {
            if ((s.w & 0xfe) == 0) return dv;
            int w = ((s.w >> 1) * P) >> 8;
            ivec4 o = d;
            for (int c = 0; c < 3; ++c) o[c] = min(255, d[c] + ((s[c] * w) >> 7));
            return px_pack(o);
        }
        if (sf == 1u && (df == 1u || df == 2u)) {
            if (s.x + s.y + s.z == 0) return dv;
            ivec4 o = d;
            for (int c = 0; c < 3; ++c) o[c] = min(255, d[c] + ((s[c] * P) >> 8));
            if (df == 2u) o.w = min(255, d.w + P);
            return px_pack(o);
        }
        if (sf == 0u && df == 0u) {
            if (sv == 0u) return dv;
            ivec3 o = min(ivec3(31), ((rgb555(sv) * P) >> 8) + rgb555(dv));
            return rgb555_pack(o);
        }
        break;
    case 3u:
        if (sf == 2u && df == 1u) {
            if ((s.w & 0xfe) == 0) return dv;
            int f = ((s.w >> 1) * P) >> 8;
            ivec4 o = d;
            for (int c = 0; c < 3; ++c) o[c] = max(0, d[c] - ((s[c] * f) >> 7));
            return px_pack(o);
        }
        break;
    case 4u:
        if (sf == 1u && df == 1u) {
            ivec4 o = d;
            for (int c = 0; c < 3; ++c) o[c] = d[c] + ((((s[c] - 256) * (P / 2)) * (2 * d[c])) >> 16);
            return px_pack(o);
        }
        if (sf == 1u && df == 2u) {
            ivec4 o = d;
            for (int c = 0; c < 3; ++c) o[c] = d[c] + (((((s[c] * d[c]) >> 8) - d[c]) * (P >> 1)) >> 7);
            return px_pack(o);
        }
        if (sf == 2u && df == 1u) {
            if ((s.w & 0xfe) == 0) return dv;
            int w = ((s.w >> 1) * P) >> 8;
            ivec4 o = d;
            for (int c = 0; c < 3; ++c) o[c] = d[c] + (((((s[c] * d[c]) >> 8) - d[c]) * w) >> 7);
            return px_pack(o);
        }
        if (sf == 3u && df == 3u) {
            int sg = int(sv & 255u), dg = int(dv & 255u);
            int m = ((sg * dg + 1) * 0x101) >> 16;
            return uint((dg + (((m - dg) * P) >> 8)) & 255);
        }
        break;
    case 5u:
        if (sf == 1u && df == 1u) {
            ivec4 o;
            for (int c = 0; c < 4; ++c) o[c] = (s[c] * (256 - P)) >> 8;
            return px_pack(o);
        }
        if (sf == 2u && df == 1u) {
            if ((s.w & 0xfe) == 0) return dv;
            ivec4 o = d;
            for (int c = 0; c < 3; ++c) {
                int sp = (s[c] * (256 - P)) >> 8;
                o[c] = d[c] + (((sp - d[c]) * (s.w >> 1)) >> 7);
            }
            return px_pack(o);
        }
        if (sf == 0u && df == 0u) return rgb555_pack((rgb555(sv) * (256 - P)) >> 8);
        if (sf == 2u && df == 2u) {
            if (s.w == 0) return dv;
            uint Q = uint(256 - P), sa = uint(s.w), da = uint(d.w);
            uint den = 256u * sa + (256u - sa) * da;
            uint ws = ((sa * Q) << 16) / den, wd = (((256u - sa) * da) << 16) / den;
            ivec4 o;
            o.xyz = ivec3((uvec3(s.xyz) * ws + uvec3(d.xyz) * wd) >> 16);
            o.w = int(den >> 8);
            return px_pack(o);
        }
        break;
    case 6u:
        if (sf == 1u && df == 1u) {
            ivec4 o = d;
            for (int c = 0; c < 3; ++c) {
                int sp = (s[c] * P) >> 8;
                o[c] = sat255(d[c] + sp - ((sp * d[c]) >> 8));
            }
            return px_pack(o);
        }
        if (sf == 2u && df == 1u) {
            if (s.w == 0) return dv;
            ivec4 o = d;
            for (int c = 0; c < 3; ++c) {
                int sp = (s[c] * ((s.w * P) >> 8)) >> 8;
                o[c] = sat255(d[c] + sp - ((sp * d[c]) >> 8));
            }
            return px_pack(o);
        }
        break;
    case 7u:
        if (s.w == 0) return dv;
        if (sf == 2u && df == 1u) {
            int t = 256 - ((s.w * P) >> 8);
            ivec4 o = d;
            for (int c = 0; c < 3; ++c) o[c] = (d[c] * t) >> 8;
            return px_pack(o);
        }
        if (sf == 2u && df == 2u) {
            ivec4 o = d;
            o.w = P < 256 ? (d.w * (0x10000 - s.w * P)) >> 16 : (d.w * (256 - s.w)) >> 8;
            return px_pack(o);
        }
        if (sf == 2u && df == 3u) {
            int dg = int(dv & 255u);
            return uint((P < 256 ? (dg * (0xff00 - s.w * P)) >> 16 : (dg * (255 - s.w)) >> 8) & 255);
        }
        break;
    case 8u:
        if (sf == 1u && df == 1u) return mix89(s, d, P, false);
        break;
    case 9u:
        if (sf == 1u && df == 1u) return mix89(s, d, P, true);
        break;
    case 0x40u:
        if (sf == 2u && df == 2u) return (P != 0 ? s.w != 0 : s.w == 255) ? 0u : dv;
        if (sf == 1u && df == 1u) return (s.x + s.y + s.z) != 0 ? 0u : dv;
        if (sf == 0u && df == 0u) return sv != 0u ? 0u : dv;
        break;
    case 0x80u: // format-converting copy (FUN_0040c040, analysis/混合公式.md); other combinations leave the destination alone
        if (sf == df) return sv;
        if (sf == 1u && df == 2u) return sv | 0xff000000u;
        if (sf == 2u && df == 1u) { // premultiplied onto black (the reference vectors, taken from the original, confirm this)
            ivec4 o;
            for (int c = 0; c < 4; ++c) o[c] = (s[c] * (s.w >> 1)) >> 7;
            return px_pack(o);
        }
        if (sf == 3u && df == 2u) return (sv & 255u) << 24;
        return dv;
    case 0xC1u:
        if (sf == 1u && df == 1u) {
            ivec4 o = s;
            for (int c = 0; c < 3; ++c) o[c] = (s[c] * (256 - P) + 255 * P) >> 8;
            return px_pack(o);
        }
        break;
    case 0xF0u:
        if (sf == 1u && df == 1u) {
            int w = P >> 1;
            ivec4 o;
            for (int c = 0; c < 4; ++c) o[c] = sat255(s[c] + (((d[c] - s[c]) * w) >> 7));
            return px_pack(o);
        }
        if (sf == 0u && df == 0u) {
            ivec3 a = rgb555(sv), b = rgb555(dv);
            return rgb555_pack(min(ivec3(31), (a * (256 - P) + b * P) >> 8));
        }
        break;
    case 0xFFu:
        if (sf == 2u && df == 1u) {
            if (P == 0) return 0xff000000u | uint(s.x);
            if (P == 1) return 0xff000000u | uint(s.y) << 8;
            if (P == 2) return 0xff000000u | uint(s.z) << 16;
            return 0xff000000u | uint(s.w) * 0x010101u;
        }
        break;
    }
    return 0xdeadbeefu; // combination the original dispatcher rejects (or not implemented here)
}

#endif
