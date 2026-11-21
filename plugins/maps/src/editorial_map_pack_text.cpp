// FreeType text helpers of the editorial pack. The Font/Fonts model lives in
// editorial_map_pack_internal.hpp; the drawing wrappers were split out of
// editorial_map_pack.cpp.

#include "editorial_map_pack_internal.hpp"

#include <opencv2/imgproc.hpp>

#include <string>

namespace chrononplugin::maps::editorial_pack_detail {

void draw_text(Font& font, cv::Mat& frame, const std::string& text, int x,
               int baseline, int px, const cv::Scalar& color, double alpha,
               int tracking) {
    font.draw(frame, text, x, baseline, px, color, alpha, tracking);
}

void draw_text_centered(Font& font, cv::Mat& frame, const std::string& text,
                        int center_x, int baseline, int px,
                        const cv::Scalar& color, double alpha,
                        int tracking) {
    const double width = font.measure(text, px, tracking);
    font.draw(frame, text, static_cast<int>(std::lround(center_x - width * 0.5)),
              baseline, px, color, alpha, tracking);
}

void draw_text_outlined(Font& font, cv::Mat& frame, const std::string& text,
                        int x, int baseline, int px, const cv::Scalar& color,
                        double alpha, int tracking, const cv::Scalar& outline,
                        int radius) {
    static constexpr int kOffsets[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1},
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
    };
    for (const auto& offset : kOffsets) {
        font.draw(frame, text, x + offset[0] * radius, baseline + offset[1] * radius,
                  px, outline, alpha * 0.55, tracking);
    }
    font.draw(frame, text, x, baseline, px, color, alpha, tracking);
}
}  // namespace chrononplugin::maps::editorial_pack_detail
