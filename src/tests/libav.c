#include "tests.h"
#include <libavutil/pixdesc.h>

int main()
{
    struct pl_context *ctx = pl_test_context();
    struct pl_plane_data data[4] = {0};

#define TEST(pixfmt, reference)                                     \
    do {                                                            \
        int planes = pl_plane_data_from_pixfmt(ctx, data, pixfmt);  \
        REQUIRE(planes == sizeof(reference) / sizeof(*reference));  \
        REQUIRE(memcmp(data, reference, sizeof(reference)) == 0);   \
    } while (0)

    // Test typical planar and semiplanar formats
    static const struct pl_plane_data yuvp8[3] = {
        {
            .type = PL_FMT_UNORM,
            .component_size = {8},
            .component_map = {0},
            .pixel_stride = 1,
        }, {
            .type = PL_FMT_UNORM,
            .component_size = {8},
            .component_map = {1},
            .pixel_stride = 1,
        }, {
            .type = PL_FMT_UNORM,
            .component_size = {8},
            .component_map = {2},
            .pixel_stride = 1,
        }
    };

    TEST(AV_PIX_FMT_YUV420P, yuvp8);
    TEST(AV_PIX_FMT_YUV422P, yuvp8);
    TEST(AV_PIX_FMT_YUV444P, yuvp8);

    static const struct pl_plane_data yuvp16[3] = {
        {
            .type = PL_FMT_UNORM,
            .component_size = {16},
            .component_map = {0},
            .pixel_stride = 2,
        }, {
            .type = PL_FMT_UNORM,
            .component_size = {16},
            .component_map = {1},
            .pixel_stride = 2,
        }, {
            .type = PL_FMT_UNORM,
            .component_size = {16},
            .component_map = {2},
            .pixel_stride = 2,
        }
    };

    TEST(AV_PIX_FMT_YUV420P16, yuvp16);

    static const struct pl_plane_data nv12[2] = {
        {
            .type = PL_FMT_UNORM,
            .component_size = {8},
            .component_map = {0},
            .pixel_stride = 1,
        }, {
            .type = PL_FMT_UNORM,
            .component_size = {8, 8},
            .component_map = {1, 2},
            .pixel_stride = 2,
        }
    };

    TEST(AV_PIX_FMT_NV12, nv12);

    static const struct pl_plane_data nv21[2] = {
        {
            .type = PL_FMT_UNORM,
            .component_size = {8},
            .component_map = {0},
            .pixel_stride = 1,
        }, {
            .type = PL_FMT_UNORM,
            .component_size = {8, 8},
            .component_map = {2, 1},
            .pixel_stride = 2,
        }
    };

    TEST(AV_PIX_FMT_NV21, nv21);

    static const struct pl_plane_data p016[2] = {
        {
            .type = PL_FMT_UNORM,
            .component_size = {16},
            .component_map = {0},
            .pixel_stride = 2,
        }, {
            .type = PL_FMT_UNORM,
            .component_size = {16, 16},
            .component_map = {1, 2},
            .pixel_stride = 4,
        }
    };

    TEST(AV_PIX_FMT_P010, p016);
    TEST(AV_PIX_FMT_P016, p016);

    // Test typical packed formats
    static const struct pl_plane_data rgb24[1] = {
        {
            .type = PL_FMT_UNORM,
            .component_size = {8, 8, 8},
            .component_map = {0, 1, 2},
            .pixel_stride = 3,
        }
    };

    TEST(AV_PIX_FMT_RGB24, rgb24);

    static const struct pl_plane_data bgr24[1] = {
        {
            .type = PL_FMT_UNORM,
            .component_size = {8, 8, 8},
            .component_map = {2, 1, 0},
            .pixel_stride = 3,
        }
    };

    TEST(AV_PIX_FMT_BGR24, bgr24);

    static const struct pl_plane_data rgbx[1] = {
        {
            .type = PL_FMT_UNORM,
            .component_size = {8, 8, 8},
            .component_map = {0, 1, 2},
            .pixel_stride = 4,
        }
    };

    TEST(AV_PIX_FMT_RGB0, rgbx);

    static const struct pl_plane_data xrgb[1] = {
        {
            .type = PL_FMT_UNORM,
            .component_size = {8, 8, 8},
            .component_map = {0, 1, 2},
            .component_pad = {8, 0, 0},
            .pixel_stride = 4,
        }
    };

    TEST(AV_PIX_FMT_0RGB, xrgb);
}
