/*
 * This file is part of libplacebo.
 *
 * libplacebo is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libplacebo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libplacebo. If not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>
#include "shaders.h"

void pl_shader_decode_color(struct pl_shader *sh, struct pl_color_repr *repr,
                            struct pl_color_adjustment params, int texture_bits)
{
    if (!sh_require(sh, PL_SHADER_SIG_COLOR, 0, 0))
        return;

    GLSL("// pl_shader_decode_color\n");

    // For the non-linear color systems we need some special input handling
    // to make sure we don't accidentally screw everything up because of the
    // alpha multiplication, which only commutes with linear operations.
    bool is_nonlinear = !pl_color_system_is_linear(repr->sys);
    if (is_nonlinear && repr->alpha == PL_ALPHA_PREMULTIPLIED) {
        GLSL("color.rgb /= vec3(color.a);\n");
        repr->alpha = PL_ALPHA_INDEPENDENT;
    }

    // XYZ needs special handling due to the input gamma logic
    if (repr->sys == PL_COLOR_SYSTEM_XYZ) {
        float scale = pl_color_repr_normalize(repr);
        GLSL("color.rgb = pow(%f * color.rgb, vec3(2.6));\n", scale);
    }

    enum pl_color_system orig_sys = repr->sys;
    struct pl_transform3x3 tr = pl_color_repr_decode(repr, params);

    ident_t cmat = sh_var(sh, (struct pl_shader_var) {
        .var  = ra_var_mat3("cmat"),
        .data = PL_TRANSPOSE_3X3(tr.mat.m),
    });

    ident_t cmat_c = sh_var(sh, (struct pl_shader_var) {
        .var  = ra_var_vec3("cmat_m"),
        .data = tr.c,
    });

    GLSL("color.rgb = %s * color.rgb + %s;\n", cmat, cmat_c);

    if (orig_sys == PL_COLOR_SYSTEM_BT_2020_C) {
        // Conversion for C'rcY'cC'bc via the BT.2020 CL system:
        // C'bc = (B'-Y'c) / 1.9404  | C'bc <= 0
        //      = (B'-Y'c) / 1.5816  | C'bc >  0
        //
        // C'rc = (R'-Y'c) / 1.7184  | C'rc <= 0
        //      = (R'-Y'c) / 0.9936  | C'rc >  0
        //
        // as per the BT.2020 specification, table 4. This is a non-linear
        // transformation because (constant) luminance receives non-equal
        // contributions from the three different channels.
        GLSL("// constant luminance conversion                            \n"
             "color.br = color.br * mix(vec2(1.5816, 0.9936),             \n"
             "                          vec2(1.9404, 1.7184),             \n"
             "                          lessThanEqual(color.br, vec2(0))) \n"
             "           + color.gg;                                      \n"
        // Expand channels to camera-linear light. This shader currently just
        // assumes everything uses the BT.2020 12-bit gamma function, since the
        // difference between 10 and 12-bit is negligible for anything other
        // than 12-bit content.
             "color.rgb = mix(color.rgb * vec3(1.0/4.5),                       \n"
             "                pow((color.rgb + vec3(0.0993))*vec3(1.0/1.0993), \n"
             "                    vec3(1.0/0.45)),                             \n"
             "                lessThanEqual(vec3(0.08145), color.rgb));        \n"
        // Calculate the green channel from the expanded RYcB
        // The BT.2020 specification says Yc = 0.2627*R + 0.6780*G + 0.0593*B
             "color.g = (color.g - 0.2627*color.r - 0.0593*color.b)*1.0/0.6780; \n"
        // Recompress to receive the R'G'B' result, same as other systems
             "color.rgb = mix(color.rgb * vec3(4.5),                    \n"
             "                vec3(1.0993) * pow(color.rgb, vec3(0.45)) \n"
             "                   - vec3(0.0993),                        \n"
             "                lessThanEqual(vec3(0.0181), color.rgb));  \n");
    }

    if (repr->alpha == PL_ALPHA_INDEPENDENT) {
        GLSL("color.rgb *= vec3(color.a)\n");
        repr->alpha = PL_ALPHA_PREMULTIPLIED;
    }
}

// Common constants for SMPTE ST.2084 (PQ)
static const float PQ_M1 = 2610./4096 * 1./4,
                   PQ_M2 = 2523./4096 * 128,
                   PQ_C1 = 3424./4096,
                   PQ_C2 = 2413./4096 * 32,
                   PQ_C3 = 2392./4096 * 32;

// Common constants for ARIB STD-B67 (HLG)
static const float HLG_A = 0.17883277,
                   HLG_B = 0.28466892,
                   HLG_C = 0.55991073;

// Common constants for Panasonic V-Log
static const float VLOG_B = 0.00873,
                   VLOG_C = 0.241514,
                   VLOG_D = 0.598206;

// Common constants for Sony S-Log
static const float SLOG_A = 0.432699,
                   SLOG_B = 0.037584,
                   SLOG_C = 0.616596 + 0.03,
                   SLOG_P = 3.538813,
                   SLOG_Q = 0.030001,
                   SLOG_K2 = 155.0 / 219.0;

void pl_shader_linearize(struct pl_shader *sh, enum pl_color_transfer trc)
{
    if (!sh_require(sh, PL_SHADER_SIG_COLOR, 0, 0))
        return;

    if (trc == PL_COLOR_TRC_LINEAR)
        return;

    // Note that this clamp may technically violate the definition of
    // ITU-R BT.2100, which allows for sub-blacks and super-whites to be
    // displayed on the display where such would be possible. That said, the
    // problem is that not all gamma curves are well-defined on the values
    // outside this range, so we ignore it and just clip anyway for sanity.
    GLSL("// pl_shader_linearize                  \n"
         "color.rgb = clamp(color.rgb, 0.0, 1.0); \n");

    switch (trc) {
    case PL_COLOR_TRC_SRGB:
        GLSL("color.rgb = mix(color.rgb * vec3(1.0/12.92),               \n"
             "                pow((color.rgb + vec3(0.055))/vec3(1.055), \n"
             "                    vec3(2.4)),                            \n"
             "                lessThan(vec3(0.04045), color.rgb));       \n");
        break;
    case PL_COLOR_TRC_BT_1886:
        GLSL("color.rgb = pow(color.rgb, vec3(2.4));\n");
        break;
    case PL_COLOR_TRC_GAMMA18:
        GLSL("color.rgb = pow(color.rgb, vec3(1.8));\n");
        break;
    case PL_COLOR_TRC_UNKNOWN:
    case PL_COLOR_TRC_GAMMA22:
        GLSL("color.rgb = pow(color.rgb, vec3(2.2));\n");
        break;
    case PL_COLOR_TRC_GAMMA28:
        GLSL("color.rgb = pow(color.rgb, vec3(2.8));\n");
        break;
    case PL_COLOR_TRC_PRO_PHOTO:
        GLSL("color.rgb = mix(color.rgb * vec3(1.0/16.0),          \n"
             "                pow(color.rgb, vec3(1.8)),           \n"
             "                lessThan(vec3(0.03125), color.rgb)); \n");
        break;
    case PL_COLOR_TRC_PQ:
        GLSL("color.rgb = pow(color.rgb, vec3(1.0/%f));         \n"
             "color.rgb = max(color.rgb - vec3(%f), vec3(0.0))  \n"
             "             / (vec3(%f) - vec3(%f) * color.rgb); \n"
             "color.rgb = pow(color.rgb, vec3(1.0/%f));         \n"
             // PQ's output range is 0-10000, but we need it to be relative to
             // to PL_COLOR_REF_WHITE instead, so rescale
             "color.rgb *= vec3(%f);\n",
             PQ_M2, PQ_C1, PQ_C2, PQ_C3, PQ_M1, 10000 / PL_COLOR_REF_WHITE);
        break;
    case PL_COLOR_TRC_HLG:
        GLSL("color.rgb = mix(vec3(4.0) * color.rgb * color.rgb,         \n"
             "                exp((color.rgb - vec3(%f)) * vec3(1.0/%f)) \n"
             "                    + vec3(%f),                            \n"
             "                lessThan(vec3(0.5), color.rgb));           \n",
             HLG_C, HLG_A, HLG_B);
        break;
    case PL_COLOR_TRC_V_LOG:
        GLSL("color.rgb = mix((color.rgb - vec3(0.125)) * vec3(1.0/5.6), \n"
             "    pow(vec3(10.0), (color.rgb - vec3(%f)) * vec3(1.0/%f)) \n"
             "              - vec3(%f),                                  \n"
             "    lessThanEqual(vec3(0.181), color.rgb));                \n",
             VLOG_D, VLOG_C, VLOG_B);
        break;
    case PL_COLOR_TRC_S_LOG1:
        GLSL("color.rgb = pow(vec3(10.0), (color.rgb - vec3(%f)) * vec3(1.0/%f)) \n"
             "            - vec3(%f);                                            \n",
             SLOG_C, SLOG_A, SLOG_B);
        break;
    case PL_COLOR_TRC_S_LOG2:
        GLSL("color.rgb = mix((color.rgb - vec3(%f)) * vec3(1.0/%f),      \n"
             "    (pow(vec3(10.0), (color.rgb - vec3(%f)) * vec3(1.0/%f)) \n"
             "              - vec3(%f)) * vec3(1.0/%f),                   \n"
             "    lessThanEqual(vec3(%f), color.rgb));                    \n",
             SLOG_Q, SLOG_P, SLOG_C, SLOG_A, SLOG_B, SLOG_K2, SLOG_Q);
        break;
    default:
        abort();
    }

    // Rescale to prevent clipping on non-float textures
    GLSL("color.rgb *= vec3(1.0/%f);\n", pl_color_transfer_nominal_peak(trc));
}

void pl_shader_delinearize(struct pl_shader *sh, enum pl_color_transfer trc)
{
    if (!sh_require(sh, PL_SHADER_SIG_COLOR, 0, 0))
        return;

    if (trc == PL_COLOR_TRC_LINEAR)
        return;

    GLSL("// pl_shader_delinearize\n"
         "color.rgb = clamp(color.rgb, 0.0, 1.0);\n"
         "color.rgb *= vec3(%f);\n", pl_color_transfer_nominal_peak(trc));

    switch (trc) {
    case PL_COLOR_TRC_SRGB:
        GLSL("color.rgb = mix(color.rgb * vec3(12.92),                    \n"
             "                vec3(1.055) * pow(color.rgb, vec3(1.0/2.4)) \n"
             "                    - vec3(0.055),                          \n"
             "                lessThanEqual(vec3(0.0031308), color.rgb)); \n");
        break;
    case PL_COLOR_TRC_BT_1886:
        GLSL("color.rgb = pow(color.rgb, vec3(1.0/2.4));\n");
        break;
    case PL_COLOR_TRC_GAMMA18:
        GLSL("color.rgb = pow(color.rgb, vec3(1.0/1.8));\n");
        break;
    case PL_COLOR_TRC_UNKNOWN:
    case PL_COLOR_TRC_GAMMA22:
        GLSL("color.rgb = pow(color.rgb, vec3(1.0/2.2));\n");
        break;
    case PL_COLOR_TRC_GAMMA28:
        GLSL("color.rgb = pow(color.rgb, vec3(1.0/2.8));\n");
        break;
    case PL_COLOR_TRC_PRO_PHOTO:
        GLSL("color.rgb = mix(color.rgb * vec3(16.0),                    \n"
             "                pow(color.rgb, vec3(1.0/1.8)),             \n"
             "                lessThanEqual(vec3(0.001953), color.rgb)); \n");
        break;
    case PL_COLOR_TRC_PQ:
        GLSL("color.rgb *= vec3(1.0/%f);                         \n"
             "color.rgb = pow(color.rgb, vec3(%f));              \n"
             "color.rgb = (vec3(%f) + vec3(%f) * color.rgb)      \n"
             "             / (vec3(1.0) + vec3(%f) * color.rgb); \n"
             "color.rgb = pow(color.rgb, vec3(%f));              \n",
             10000 / PL_COLOR_REF_WHITE, PQ_M1, PQ_C1, PQ_C2, PQ_C3, PQ_M2);
        break;
    case PL_COLOR_TRC_HLG:
        GLSL("color.rgb = mix(vec3(0.5) * sqrt(color.rgb),                     \n"
             "                vec3(%f) * log(color.rgb - vec3(%f)) + vec3(%f), \n"
             "                lessThan(vec3(1.0), color.rgb));                 \n",
             HLG_A, HLG_B, HLG_C);
        break;
    case PL_COLOR_TRC_V_LOG:
        GLSL("color.rgb = mix(vec3(5.6) * color.rgb + vec3(0.125),   \n"
             "                vec3(%f) * log(color.rgb + vec3(%f))   \n"
             "                    + vec3(%f),                        \n"
             "                lessThanEqual(vec3(0.01), color.rgb)); \n",
             VLOG_C / M_LN10, VLOG_B, VLOG_D);
        break;
    case PL_COLOR_TRC_S_LOG1:
        GLSL("color.rgb = vec3(%f) * log(color.rgb + vec3(%f)) + vec3(%f);\n",
             SLOG_A / M_LN10, SLOG_B, SLOG_C);
        break;
    case PL_COLOR_TRC_S_LOG2:
        GLSL("color.rgb = mix(vec3(%f) * color.rgb + vec3(%f),                \n"
             "                vec3(%f) * log(vec3(%f) * color.rgb + vec3(%f)) \n"
             "                    + vec3(%f),                                 \n"
             "                lessThanEqual(vec3(0.0), color.rgb));           \n",
             SLOG_P, SLOG_Q, SLOG_A / M_LN10, SLOG_K2, SLOG_B, SLOG_C);
        break;
    default:
        abort();
    }
}

// Applies the OOTF / inverse OOTF. `peak` corresponds to the nominal peak
// (needed to scale the functions correctly)
static void pl_shader_ootf(struct pl_shader *sh, enum pl_color_light light,
                           float peak, ident_t luma)
{
    if (!sh_require(sh, PL_SHADER_SIG_COLOR, 0, 0))
        return;

    if (!light || light == PL_COLOR_LIGHT_DISPLAY)
        return;

    GLSL("// pl_shader_ootf      \n"
         "color.rgb *= vec3(%f); \n", peak);

    switch (light)
    {
    case PL_COLOR_LIGHT_SCENE_HLG:
        // HLG OOTF from BT.2100, assuming a reference display with a
        // peak of 1000 cd/m² -> gamma = 1.2
        GLSL("color.rgb *= vec3(%f * pow(dot(%s, color.rgb), 0.2));\n",
             (1000 / PL_COLOR_REF_WHITE) / pow(12, 1.2), luma);
        break;
    case PL_COLOR_LIGHT_SCENE_709_1886:
        // This OOTF is defined by encoding the result as 709 and then decoding
        // it as 1886; although this is called 709_1886 we actually use the
        // more precise (by one decimal) values from BT.2020 instead
        GLSL("color.rgb = mix(color.rgb * vec3(4.5),                    \n"
             "                vec3(1.0993) * pow(color.rgb, vec3(0.45)) \n"
             "                             - vec3(0.0993),              \n"
             "                lessThan(vec3(0.0181), color.rgb));       \n"
             "color.rgb = pow(color.rgb, vec3(2.4));                    \n");
        break;
    case PL_COLOR_LIGHT_SCENE_1_2:
        GLSL("color.rgb = pow(color.rgb, vec3(1.2));\n");
        break;
    default:
        abort();
    }

    GLSL("color.rgb *= vec3(1.0/%f);\n", peak);
}

static void pl_shader_inverse_ootf(struct pl_shader *sh, enum pl_color_light light,
                                   float peak, ident_t luma)
{
    if (!light || light == PL_COLOR_LIGHT_DISPLAY)
        return;

    GLSL("// pl_shader_inverse_ootf\n"
         "color.rgb *= vec3(%f);\n", peak);

    switch (light)
    {
    case PL_COLOR_LIGHT_SCENE_HLG:
        GLSL("color.rgb *= vec3(1.0/%f);                                \n"
             "color.rgb /= vec3(max(1e-6, pow(dot(%s, color.rgb),       \n"
             "                                0.2/1.2)));               \n",
             (1000 / PL_COLOR_REF_WHITE) / pow(12, 1.2), luma);
        break;
    case PL_COLOR_LIGHT_SCENE_709_1886:
        GLSL("color.rgb = pow(color.rgb, vec3(1.0/2.4));                         \n"
             "color.rgb = mix(color.rgb * vec3(1.0/4.5),                         \n"
             "                pow((color.rgb + vec3(0.0993)) * vec3(1.0/1.0993), \n"
             "                    vec3(1/0.45)),                                 \n"
             "                lessThan(vec3(0.08145), color.rgb));               \n");
        break;
    case PL_COLOR_LIGHT_SCENE_1_2:
        GLSL("color.rgb = pow(color.rgb, vec3(1.0/1.2));\n");
        break;
    default:
        abort();
    }

    GLSL("color.rgb *= vec3(1.0/%f);\n", peak);
}

const struct pl_color_map_params pl_color_map_default_params = {
    .intent                  = PL_INTENT_RELATIVE_COLORIMETRIC,
    .tone_mapping_algo       = PL_TONE_MAPPING_MOBIUS,
    .tone_mapping_desaturate = 2.0,
};

static void pl_shader_tone_map(struct pl_shader *sh, float ref_peak, ident_t luma,
                               const struct pl_color_map_params *params)
{
    GLSL("// pl_shader_tone_map\n");

    // Desaturate the color using a coefficient dependent on the luminance
    if (params->tone_mapping_desaturate > 0) {
        GLSL("float luma = dot(%s, color.rgb);                           \n"
             "float overbright = max(luma - %f, 1e-6) / max(luma, 1e-6); \n"
             "color.rgb = mix(color.rgb, vec3(luma), overbright);        \n",
             luma, params->tone_mapping_desaturate);
    }

    // To prevent discoloration due to out-of-bounds clipping, we need to make
    // sure to reduce the value range as far as necessary to keep the entire
    // signal in range, so tone map based on the brightest component.
    GLSL("float sig = max(max(color.r, color.g), color.b); \n"
         "float sig_orig = sig;                            \n");

    // TODO: implement HDR peak detection
    GLSL("const float sig_peak = %f;\n", ref_peak);

    float param = params->tone_mapping_param;
    switch (params->tone_mapping_algo) {
    case PL_TONE_MAPPING_CLIP:
        GLSL("sig = %f * sig;\n", PL_DEF(param, 1.0));
        break;

    case PL_TONE_MAPPING_MOBIUS:
        GLSL("const float j = %f;                                           \n"
             // solve for M(j) = j; M(sig_peak) = 1.0; M'(j) = 1.0
             // where M(x) = scale * (x+a)/(x+b)
             "float a = -j*j * (sig_peak - 1.0) / (j*j - 2.0*j + sig_peak); \n"
             "float b = (j*j - 2.0*j*sig_peak + sig_peak) /                 \n"
             "          max(1e-6, sig_peak - 1.0);                          \n"
             "float scale = (b*b + 2.0*b*j + j*j) / (b-a);                  \n"
             "sig = sig > j ? (scale * (sig + a) / (sig + b)) : sig;        \n",
             PL_DEF(param, 0.3));
        break;

    case PL_TONE_MAPPING_REINHARD: {
        float contrast = PL_DEF(param, 0.5),
              offset = (1.0 - contrast) / contrast;
        GLSL("sig = sig / (sig + %f);                   \n"
             "float scale = (sig_peak + %f) / sig_peak; \n"
             "sig *= scale;                             \n",
             offset, offset);
        break;
    }

    case PL_TONE_MAPPING_HABLE: {
        float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
        ident_t hable = sh_fresh(sh, "hable");
        GLSLH("float %s(float x) {                                        \n"
              "return ((x * (%f*x + %f)+%f)/(x * (%f*x + %f) + %f)) - %f; \n"
              "}                                                          \n",
              hable, A, C*B, D*E, A, B, D*F, E/F);
        GLSL("sig = %s(sig) / %s(sig_peak);\n", hable, hable);
        break;
    }

    case PL_TONE_MAPPING_GAMMA: {
        GLSL("const float cutoff = 0.05, gamma = 1.0/%f;                     \n"
             "float scale = pow(cutoff / sig_peak, gamma) / cutoff;          \n"
             "sig = sig > cutoff ? pow(sig / sig_peak, gamma) : scale * sig; \n",
             PL_DEF(param, 1.8));
        break;
    }

    case PL_TONE_MAPPING_LINEAR: {
        GLSL("sig = %f / sig_peak * sig;\n", PL_DEF(param, 1.0));
        break;
    }

    default:
        abort();
    }

    // Apply the computed scale factor to the color, linearly to prevent
    // discoloration
    GLSL("color.rgb *= sig / sig_orig;\n");
}

void pl_shader_color_map(struct pl_shader *sh,
                         const struct pl_color_map_params *params,
                         struct pl_color_space src, struct pl_color_space dst,
                         bool prelinearized)
{
    if (!sh_require(sh, PL_SHADER_SIG_COLOR, 0, 0))
        return;

    GLSL("// pl_shader_color_map\n");
    GLSL("{\n");

    // To be as conservative as possible, color mapping is disabled by default
    // except for special cases which are considered to be "sufficiently
    // different" from the source space. For primaries, this means anything
    // wide gamut; and for transfers, this means anything radically different
    // from the typical SDR curves.
    if (!dst.primaries) {
        dst.primaries = src.primaries;
        if (pl_color_primaries_is_wide_gamut(dst.primaries))
            dst.primaries = PL_COLOR_PRIM_BT_709;
    }

    if (!dst.transfer) {
        dst.transfer = src.transfer;
        if (pl_color_transfer_is_hdr(dst.transfer) ||
                dst.transfer == PL_COLOR_TRC_LINEAR)
            dst.transfer = PL_COLOR_TRC_GAMMA22;
    }

    // If the source signal peak information is unknown, infer it from the
    // transfer function. (Note: The sig peak of the dst space is irrelevant)
    if (!src.sig_peak)
        src.sig_peak = pl_color_transfer_nominal_peak(src.transfer);

    // Compute the highest encodable level
    float src_range = pl_color_transfer_nominal_peak(src.transfer),
          dst_range = pl_color_transfer_nominal_peak(dst.transfer);
    float ref_peak = src.sig_peak / dst_range;

    // All operations from here on require linear light as a starting point,
    // so we linearize even if src.gamma == dst.gamma when one of the other
    // operations needs it
    bool need_linear = src.transfer != dst.transfer ||
                       src.primaries != dst.primaries ||
                       src_range != dst_range ||
                       src.sig_peak > dst_range ||
                       src.light != dst.light;
    bool is_linear = prelinearized;

    // Various operations need access to the src_luma and dst_luma respectively,
    // so just always make them available if we're doing anything at all
    ident_t src_luma = NULL, dst_luma = NULL;
    if (need_linear) {
        struct pl_matrix3x3 rgb2xyz;
        rgb2xyz = pl_get_rgb2xyz_matrix(pl_raw_primaries_get(src.primaries));
        src_luma = sh_var(sh, (struct pl_shader_var) {
            .var  = ra_var_vec3("src_luma"),
            .data = rgb2xyz.m[1], // RGB->Y vector
        });
        rgb2xyz = pl_get_rgb2xyz_matrix(pl_raw_primaries_get(dst.primaries));
        dst_luma = sh_var(sh, (struct pl_shader_var) {
            .var  = ra_var_vec3("dst_luma"),
            .data = rgb2xyz.m[1], // RGB->Y vector
        });
    }

    if (need_linear && !is_linear) {
        pl_shader_linearize(sh, src.transfer);
        is_linear = true;
    }

    if (src.light != dst.light)
        pl_shader_ootf(sh, src.light, src_range, src_luma);

    // Rescale the signal to compensate for differences in the encoding range
    // and reference white level. This is necessary because of the 0-1 value
    // normalization for HDR signals.
    if (src_range != dst_range) {
        GLSL("// rescale value range;\n");
        GLSL("color.rgb *= vec3(%f);\n", src_range / dst_range);
    }

    // Adapt to the right colorspace (primaries) if necessary
    if (src.primaries != dst.primaries) {
        const struct pl_raw_primaries *csp_src, *csp_dst;
        csp_src = pl_raw_primaries_get(src.primaries),
        csp_dst = pl_raw_primaries_get(dst.primaries);
        struct pl_matrix3x3 cms_mat;
        cms_mat = pl_get_color_mapping_matrix(csp_src, csp_dst, params->intent);
        GLSL("color.rgb = %s * color.rgb;\n", sh_var(sh, (struct pl_shader_var) {
            .var = ra_var_mat3("cms_matrix"),
            .data = PL_TRANSPOSE_3X3(cms_mat.m),
        }));
        // Since this can reduce the gamut, figure out by how much
        for (int c = 0; c < 3; c++)
            ref_peak = fmaxf(ref_peak, cms_mat.m[c][c]);
    }

    // Tone map to prevent clipping when the source signal peak exceeds the
    // encodable range or we've reduced the gamut
    if (ref_peak > 1)
        pl_shader_tone_map(sh, ref_peak, dst_luma, params);

    if (src.light != dst.light)
        pl_shader_inverse_ootf(sh, dst.light, dst_range, dst_luma);

    // Warn for remaining out-of-gamut colors is enabled
    if (params->gamut_warning) {
        GLSL("if (any(greaterThan(color.rgb, vec3(1.01))) ||\n"
             "    any(lessThan(color.rgb, vec3(-0.01))))\n"
             "    color.rgb = vec3(1.0) - color.rgb;) // invert\n");
    }

    if (is_linear)
        pl_shader_delinearize(sh, dst.transfer);

    GLSL("}\n");
}