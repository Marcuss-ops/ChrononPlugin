// Standalone test for the native camera/motion and label primitives
// (fasi 3-4 of docs/tickets/TICKET-MAPS-NATIVE-PORT.md). Pins the properties
// that do not need the Python reference: easing boundaries, pose shape,
// projection anchoring and label placement. Bit-exact parity against the
// Python closures is a separate gate (tools/map_native/parity_camera_label_check.py).
#include "chrononplugin/maps/map_camera.hpp"
#include "chrononplugin/maps/map_label_render.hpp"

#include <opencv2/core.hpp>

#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

}  // namespace

int main() {
    using namespace chrononplugin::maps;

    // Easing boundaries are exact.
    CHECK(smootherstep(0.0) == 0.0);
    CHECK(smootherstep(1.0) == 1.0);
    CHECK(smootherstep(-3.0) == 0.0);
    CHECK(smootherstep(4.0) == 1.0);
    CHECK(smootherstep(0.5) == 0.5);
    CHECK(roll_bank(0.0) == 0.0);
    // sin(pi) is ~1.2e-16 in IEEE, exactly like CPython's math.sin.
    CHECK(std::abs(roll_bank(1.0)) < 1e-12);
    CHECK(std::abs(roll_bank(0.5) + 8.0) < 1e-12);

    // Web Mercator: the anchor projects to the global pixel of the point.
    const auto [ax, ay] = latlon_to_global_px(41.9028, 12.4964, 7);
    CHECK(ax > 0.0 && ay > 0.0);
    // Clamping: beyond the limits the projection equals the limit.
    CHECK(latlon_to_global_px(89.0, 0.0, 5) == latlon_to_global_px(85.05112878, 0.0, 5));
    CHECK(latlon_to_global_px(0.0, 400.0, 5) == latlon_to_global_px(0.0, 180.0, 5));

    // geo_to_screen: the anchor lands at the frame centre.
    const auto [sx, sy] = geo_to_screen(41.9028, 12.4964, {41.9028, 12.4964},
                                        7.5, 1280, 720);
    CHECK(sx == 640 && sy == 360);

    // pure_dive_pose: pose holds the anchor and eases z0 -> z1.
    const CameraPose first = pure_dive_pose({41.9, 12.5}, 5.0, 9.5, 60, 0);
    const CameraPose last = pure_dive_pose({41.9, 12.5}, 5.0, 9.5, 60, 59);
    CHECK(first[0] == 41.9 && first[1] == 12.5);
    CHECK(first[2] == 5.0 && last[2] == 9.5);
    CHECK(first[3] == 0.0 && first[5] == 0.0);

    // dive_tilt_pose: after the tilt end the pose is fully tilted with roll 0.
    const CameraPose tilted = dive_tilt_pose({41.9, 12.5}, 6.0, 11.0, 20, 50, 12.5,
                                             60, 50);
    CHECK(tilted[2] == 11.0);
    CHECK(std::abs(tilted[3] - 12.5) < 1e-12);
    CHECK(std::abs(tilted[5]) < 1e-12);
    const CameraPose mid = dive_tilt_pose({41.9, 12.5}, 6.0, 11.0, 20, 50, 12.5,
                                          60, 35);
    CHECK(mid[3] > 0.0 && mid[3] < 12.5);

    // draw_camera_aligned_label paints pixels near the anchor.
    cv::Mat frame(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
    draw_camera_aligned_label(frame, {320.0, 240.0}, "Roma");
    CHECK(cv::countNonZero(frame.reshape(1)) > 0);

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("map_camera/map_label_render: all checks passed\n");
    return 0;
}
