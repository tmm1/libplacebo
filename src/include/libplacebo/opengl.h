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

#ifndef LIBPLACEBO_OPENGL_H_
#define LIBPLACEBO_OPENGL_H_

#include "ra.h"

struct pl_opengl {
    const struct ra *ra;
    void *priv;
};

struct pl_opengl_params {
    bool debug;
};

// Default/recommended parameters
extern const struct pl_opengl_params pl_opengl_default_params;

// Creates a new OpenGL renderer based on the given parameters. This will
// internally use whatever platform-defined mechanism (WGL, X11, EGL) is
// appropriate for loading the OpenGL function calls, so the user doesn't need
// to pass in a getProcAddress callback. If `params` is left as NULL, it
// defaults to &pl_opengl_default_params.
const struct pl_opengl *pl_opengl_create(struct pl_context *ctx,
                                         const struct pl_opengl_params *params);

// All resources allocated from the `ra` contained by this pl_opengl must be
// explicitly destroyed by the user before calling pl_opengl_destroy.
void pl_opengl_destroy(const struct pl_opengl **gl);

#endif // LIBPLACEBO_OPENGL_H_
