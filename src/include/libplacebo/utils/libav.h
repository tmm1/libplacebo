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

#ifndef LIBPLACEBO_LIBAV_H_
#define LIBPLACEBO_LIBAV_H_

#include "config.h"
#if !PL_HAVE_LIBAVUTIL
#error Included <libplacebo/utils/libav.h> but libplacebo built without libavutil!
#endif

#include <libplacebo/utils/upload.h>
#include <libavutil/pixdesc.h>

// Map an AVPixelFormat to an array of pl_plane_data structs. The array must
// have at least `av_pix_fmt_count_planes(fmt)` elements, but never more than 4.
// This function leaves `width`, `height` and `row_stride`, as well as the
// data pointers, uninitialized.
//
// Returns the number of plane structs written to, or 0 on error.
//
// Note: For formats like P010, this returns `component_depth = 16`
// (corresponding to `depth = 10, shift = 6` in avutil's structs). In cases
// like these, the correct depth/shift must be provided by the user as part of
// `pl_color_repr.bits` when actually rendering from the plane.
int pl_plane_data_from_pixfmt(struct pl_context *ctx,
                              struct pl_plane_data *data,
                              enum AVPixelFormat pix_fmt);

#endif // LIBPLACEBO_LIBAV_H_
