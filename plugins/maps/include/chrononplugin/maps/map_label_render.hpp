#pragma once

// Chronon3D native map label rendering — TICKET-MAPS-NATIVE-PORT fase 3
// («passo etichette»).
//
// PARITY CONTRACT (bit-exact, verified by
// tools/map_native/parity_camera_label_check.py): the label pass mirrors
// geo_documentary.draw_camera_aligned_label, in the same order of operations:
//
//   x, y = pose_label_anchor(latitude, longitude, pose, project)
//   cv2.putText(frame, text, (round(x), round(y)), cv2.FONT_HERSHEY_SIMPLEX,
//               0.6, color, 2, cv2.LINE_AA)
//
// "round" is Python's round(): half-to-even. The anchor comes pre-projected:
// the projection policy (the frame's full camera pose) belongs to the caller,
// exactly like `project` is passed into the Python function. Nothing here
// invents text or placement.

#include <string>
#include <utility>

#include <opencv2/core.hpp>

namespace chrononplugin::maps {

/// Draws one compact label at a pose-projected anchor, mirroring
/// geo_documentary.draw_camera_aligned_label.
void draw_camera_aligned_label(cv::Mat& frame, std::pair<double, double> projected,
                               const std::string& text, cv::Scalar color_bgr = {255, 255, 255});

}  // namespace chrononplugin::maps
