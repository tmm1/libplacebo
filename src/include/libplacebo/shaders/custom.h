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
 * License along with libplacebo.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBPLACEBO_SHADERS_CUSTOM_H_
#define LIBPLACEBO_SHADERS_CUSTOM_H_

// Framework for enabling custom user shader hooks, as well as compatibility
// functions for parsing shaders in mpv format.

#include <libplacebo/shaders.h>

// Which "rendering stages" are available for user shader hooking purposes.
// Except where otherwise noted, all stages are "non-resizable", i.e. the
// shaders already have specific output size requirements.
enum pl_hook_stage {
    // Hook stages for the untouched planes, as made available by the source.
    // These are all resizable, i.e. there are no specific output stage
    // requirements.
    PL_HOOK_RGB_INPUT       = 1 << 0,
    PL_HOOK_LUMA_INPUT      = 1 << 1,
    PL_HOOK_CHROMA_INPUT    = 1 << 2,
    PL_HOOK_ALPHA_INPUT     = 1 << 3,
    PL_HOOK_XYZ_INPUT       = 1 << 4,

    // Hook stages for the scaled/aligned planes
    PL_HOOK_CHROMA_SCALED   = 1 << 5,
    PL_HOOK_ALPHA_SCALED    = 1 << 6,

    PL_HOOK_NATIVE          = 1 << 7,  // Combined image in its native color space
    PL_HOOK_RGB             = 1 << 8,  // After conversion to RGB, before overlays (resizable)
    PL_HOOK_RGB_OVERLAY     = 1 << 9,  // After conversion to RGB, with overlays (resizable)
    PL_HOOK_LINEAR          = 1 << 10, // After linearization but before scaling
    PL_HOOK_SIGMOID         = 1 << 11, // After sigmoidization
    PL_HOOK_PREKERNEL       = 1 << 12, // Immediately before the main scaler kernel
    PL_HOOK_POSTKERNEL      = 1 << 13, // Immediately after the main scaler kernel
    PL_HOOK_SCALED          = 1 << 14, // After scaling, before color management
    PL_HOOK_OUTPUT          = 1 << 15, // After color management, before dithering
};

// Return flags for the `hook` function, indicating what the caller should do.
enum pl_hook_flags {
    PL_HOOK_STATUS_AGAIN     = 1 << 0,  // If true, the same hook is run again
    PL_HOOK_STATUS_SAVE      = 1 << 1,  // If true, run the `save` function
};

// Struct encapsulating a texture + metadata on how to use it
struct pl_hook_tex {
    // The actual texture object itself. This is owned by the renderer, and
    // users may expect its contents to remain untouched for the duration
    // of a frame, but not between frames.
    const struct pl_tex *tex;

    // The effective src rect we're interested in sampling from.
    struct pl_rect2df src_rect;

    // The effective representation of the color in this texture.
    struct pl_color_repr repr;
};

struct pl_hook_params {
    const struct pl_gpu *gpu;
    enum pl_hook_stage stage;   // Which stage triggered the hook
    int count;                  // Increments per invocation of this hook

    // The shader object, which the user may modify. The shader is guaranteed
    // to have the current signature requested by the user in `pl_hook.input`.
    // Note that this shader might have specific output size requirements,
    // depending on the exact shader stage hooked by the user.
    struct pl_shader *sh;

    // When the signature is `PL_SHADER_SIG_NONE`, the user may instead sample
    // from this texture. (Otherwise, this struct is {0})
    struct pl_hook_tex tex;

    // The current effective colorspace and representation, of either the
    // pre-sampled color (in `sh`), or the contents of `tex`, respectively.
    struct pl_color_repr repr;
    struct pl_color_space color;
    int components;

    // The (cropped) source and destination rectangles of the overall rendering.
    struct pl_rect2df src_rect;
    struct pl_rect2d dst_rect;
};

struct pl_save_params {
    // Same as the corresponding `pl_hook_params`
    const struct pl_gpu *gpu;
    enum pl_hook_stage stage;
    int count;

    // The output of the `hook` function's shader, after execution. The same
    // lifetime rules apply as for `pl_hook_params.tex`.
    struct pl_hook_tex tex;
};

struct pl_hook {
    enum pl_hook_stage stages;  // Which stages to hook on
    enum pl_shader_sig input;   // Which input signature this hook expects
    void *priv;                 // Arbitrary user context

    // Called at the beginning of passes, to reset/initialize the hook. (Optional)
    void (*reset)(void *priv);

    // The hook function itself. Called by the renderer at any of the indicated
    // hook stages. The return value of this function is interpreted as
    // a combination of `pl_hook_flags` indicating what the caller is supposed
    // to do next. If the caller instead returns any negative number, the
    // hook is assumed to have errored/failed in some way.
    int (*hook)(void *priv, const struct pl_hook_params *params);

    // If the previous `hook` invocation returned `PL_HOOK_STATUS_SAVE`, then
    // this function will be called on the texture representing the result
    // of executing that hook invocation's shader.
    void (*save)(void *priv, const struct pl_save_params *params);
};

// Compatibility layer with `mpv` user shaders. See the mpv man page for more
// information on the format. Will return `NULL` if the shader fails parsing.
//
// The resulting `pl_hook` objects should be destroyed with the corresponding
// destructor when no longer needed.
const struct pl_hook *pl_parse_mpv_user_shader(const struct pl_gpu *gpu,
                                               const char *shader_text);

void pl_destroy_mpv_user_shader(const struct pl_hook **hook);

#endif // LIBPLACEBO_SHADERS_CUSTOM_H_
