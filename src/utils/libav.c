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

#include "common.h"
#include "context.h"

int pl_plane_data_from_pixfmt(struct pl_context *ctx,
                              struct pl_plane_data *out_data,
                              enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    if (!desc) {
        pl_err(ctx, "Unknown AVPixelFormat: %d", (int) pix_fmt);
        return 0;
    }

    if (desc->flags & AV_PIX_FMT_FLAG_BE) {
        pl_err(ctx, "Big endian formats are most likely not supported "
               "in any reasonable manner, erroring as a safety precaution...");
        return 0;
    }

    if (desc->flags & AV_PIX_FMT_FLAG_BITSTREAM) {
        pl_err(ctx, "Bitstream formats are not supported! (Components must "
               "be byte-aligned)");
        return 0;
    }

    if (desc->flags & AV_PIX_FMT_FLAG_PAL) {
        pl_err(ctx, "Palette formats are (currently) not supported.");
        return 0;
    }

    if (desc->nb_components == 0) {
        pl_err(ctx, "Pixel format descriptor contains no components, possibly "
               "dealing with a fake/virtual/hwaccel format?");
        return 0;
    }

    int planes = av_pix_fmt_count_planes(pix_fmt);
    pl_assert(planes <= 4);

    // Construct a mapping from planes to components in that plane
    int map[4][4] = {0}, map_idx[4] = {0};
    for (int c = 0; c < desc->nb_components; c++) {
        int plane = desc->comp[c].plane;
        map[plane][map_idx[plane]++] = c;
    }

    // Sort this map by `comp.offset`
    for (int p = 0; p < planes; p++) {
        for (int c = 0; c < map_idx[p]; c++) {
            for (int o = c+1; o < map_idx[p]; o++) {
                if (desc->comp[map[p][o]].offset < desc->comp[map[p][c]].offset) {
                    int tmp = map[p][c];
                    map[p][c] = map[p][o];
                    map[p][o] = tmp;
                }
            }
        }
    }

    // Fill in the details for each plane by iterating through components
    // in memory order and keeping track of the current padding
    int plane_offset[4] = {0};
    for (int p = 0; p < planes; p++) {
        struct pl_plane_data *data = &out_data[p];
        data->type = (desc->flags & AV_PIX_FMT_FLAG_FLOAT)
                        ? PL_FMT_FLOAT
                        : PL_FMT_UNORM;

        for (int c = 0; c < map_idx[p]; c++) {
            const AVComponentDescriptor *comp = &desc->comp[map[p][c]];
            int comp_offset = comp->offset * 8;
            data->component_size[c] = comp->depth + comp->shift;
            data->component_map[c] = map[p][c];
            data->component_pad[c] = comp_offset - plane_offset[p];
            pl_assert(data->component_pad[c] >= 0);
            plane_offset[p] = comp_offset + data->component_size[c];

            // Basic sanity checking
            pl_assert(c == 0 || data->pixel_stride == comp->step);
            data->pixel_stride = comp->step;
        }

        // Explicitly clear the remaining components to make these defined
        for (int c = map_idx[p]; c < 4; c++) {
            data->component_size[c] = 0;
            data->component_map[c] = 0;
            data->component_pad[c] = 0;
        }
    }

    return planes;
}
