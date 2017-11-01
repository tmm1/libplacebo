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

#include "ra_gl.h"

static struct ra_fns ra_fns_gl;

// For ra.priv
struct ra_gl {
};

static void gl_destroy_ra(const struct ra *ra)
{
    talloc_free((void *) ra);
}

static void get_size(GLenum pname, size_t *data)
{
    GLint idata;
    glGetIntegerv(pname, &idata);
    *data = idata;
}

static void gl_setup_formats(struct ra *ra)
{
    // TODO
}

const struct ra *ra_create_gl(struct pl_context *ctx)
{
    struct ra *ra = talloc_zero(NULL, struct ra);
    ra->ctx = ctx;
    ra->impl = &ra_fns_gl;
    ra->glsl.gles = !epoxy_is_desktop_gl();

    struct ra_gl *p = ra->priv = talloc_zero(ra, struct ra_gl);

    // Query support for the capabilities
    ra->caps |= RA_CAP_INPUT_VARIABLES;
    if (epoxy_has_gl_extension("GL_ARB_compute_shader"))
        ra->caps |= RA_CAP_COMPUTE;

    // If possible, query the GLSL version from the implementation
    const char *glslver = glGetString(GL_SHADING_LANGUAGE_VERSION);
    if (glslver) {
        PL_INFO(ra, "    GL_SHADING_LANGUAGE_VERSION: %s", glslver);
        int major = 0, minor = 0;
        if (sscanf(glslver, "%d.%d", &major, &minor) == 2)
            ra->glsl.version = major * 100 + minor;
    } else {
        // Otherwise, use the fixed magic versions 200 and 300 for early GLES,
        // and otherwise fall back to 110 if all else fails.
        if (ra->glsl.gles && epoxy_gl_version() >= 300) {
            ra->glsl.version = 300;
        } else if (ra->glsl.gles && epoxy_gl_version() >= 200) {
            ra->glsl.version = 200;
        } else {
            ra->glsl.version = 110;
        }
    }

    // Query all device limits
    struct ra_limits *l = &ra->limits;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &l->max_tex_2d_dim);
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &l->max_tex_3d_dim);

    // There's no equivalent limit for 1D textures for whatever reason, so
    // just set it to the same as the 2D limit
    if (epoxy_gl_version() > 21)
        l->max_tex_1d_dim = l->max_tex_2d_dim;

    if (epoxy_has_gl_extension("GL_ARB_pixel_buffer_object"))
        l->max_xfer_size = SIZE_MAX; // no limit imposed by GL
    if (epoxy_has_gl_extension("GL_ARB_uniform_buffer_object"))
        get_size(GL_MAX_UNIFORM_BLOCK_SIZE, &l->max_ubo_size);
    if (epoxy_has_gl_extension("GL_ARB_shader_storage_buffer_object"))
        get_size(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &l->max_ssbo_size);

    if (epoxy_has_gl_extension("GL_ARB_texture_gather")) {
        glGetIntegerv(GL_MIN_PROGRAM_TEXTURE_GATHER_OFFSET_ARB, &l->min_gather_offset);
        glGetIntegerv(GL_MAX_PROGRAM_TEXTURE_GATHER_OFFSET_ARB, &l->max_gather_offset);
    }

    if (ra->caps & RA_CAP_COMPUTE) {
        get_size(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE, &l->max_shmem_size);
        glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &l->max_group_threads);
        for (int i = 0; i < 3; i++) {
            glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, i, &l->max_dispatch[i]);
            glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, i, &l->max_group_size[i]);
        }
    }

    // We simply don't know, so make up some values
    ra->limits.align_tex_xfer_stride = 4;
    ra->limits.align_tex_xfer_offset = 32;

    gl_setup_formats(ra);
    return ra;
}

static struct ra_fns ra_fns_gl = {
    .destroy                = gl_destroy_ra,
};
