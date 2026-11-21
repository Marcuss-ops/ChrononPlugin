// Editorial plates: textured canvases, supersampled composition and per-frame
// sampling. Split out of editorial_map_pack.cpp.

#include "editorial_map_pack_internal.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace chrononplugin::maps::editorial_pack_detail {

namespace {

[[nodiscard]] cv::Mat textured_canvas(int width, int height, unsigned int hex,
                                      unsigned int seed, double mottle,
                                      double fine) {
    cv::Mat canvas(height, width, CV_8UC3, rgb(hex));
    cv::RNG rng(seed);
    cv::Mat coarse(height / 6 + 2, width / 6 + 2, CV_8UC3);
    rng.fill(coarse, cv::RNG::NORMAL, 128.0, 26.0);
    cv::Mat coarse_full;
    cv::resize(coarse, coarse_full, cv::Size(width, height), 0.0, 0.0,
               cv::INTER_CUBIC);
    cv::addWeighted(canvas, 1.0, coarse_full, mottle, -128.0 * mottle, canvas);

    cv::Mat fine_noise(height / 3 + 2, width / 3 + 2, CV_8UC3);
    rng.fill(fine_noise, cv::RNG::NORMAL, 128.0, 18.0);
    cv::Mat fine_full;
    cv::resize(fine_noise, fine_full, cv::Size(width, height), 0.0, 0.0,
               cv::INTER_CUBIC);
    cv::addWeighted(canvas, 1.0, fine_full, fine, -128.0 * fine, canvas);
    return canvas;
}

[[nodiscard]] std::vector<std::vector<cv::Point>> scaled_rings(
    std::vector<std::vector<cv::Point>> rings, double scale, cv::Point2d offset) {
    double min_x = 1e18, min_y = 1e18, max_x = -1e18, max_y = -1e18;
    for (const auto& ring : rings) {
        for (const cv::Point& point : ring) {
            min_x = std::min(min_x, static_cast<double>(point.x));
            min_y = std::min(min_y, static_cast<double>(point.y));
            max_x = std::max(max_x, static_cast<double>(point.x));
            max_y = std::max(max_y, static_cast<double>(point.y));
        }
    }
    const cv::Point2d center((min_x + max_x) * 0.5, (min_y + max_y) * 0.5);
    for (auto& ring : rings) {
        for (cv::Point& point : ring) {
            const cv::Point2d p = center + (cv::Point2d(point.x, point.y) - center) * scale + offset;
            point = {static_cast<int>(std::lround(p.x)),
                     static_cast<int>(std::lround(p.y))};
        }
    }
    return rings;
}

[[nodiscard]] std::vector<std::vector<cv::Point>> offset_rings(
    std::vector<std::vector<cv::Point>> rings, cv::Point2d offset) {
    for (auto& ring : rings) {
        for (cv::Point& point : ring) {
            point.x += static_cast<int>(std::lround(offset.x));
            point.y += static_cast<int>(std::lround(offset.y));
        }
    }
    return rings;
}


}  // namespace

// The plates are composed at 2.12x (2x supersampling + 6% overscan) and then
// reduced once to 1.06x, which is exactly the largest frame the push-in can
// show. Per-frame sampling therefore only crops and performs a small resize
// instead of downscaling a 4K plate twice for every frame.
[[nodiscard]] Plates finalize_plates(const cv::Mat& base, const cv::Mat& hi,
                                     int width, int height) {
    const cv::Size preview_size(
        static_cast<int>(std::lround(width * 1.06)),
        static_cast<int>(std::lround(height * 1.06)));
    Plates plates;
    cv::resize(base, plates.base, preview_size, 0.0, 0.0, cv::INTER_AREA);
    if (!hi.empty()) {
        cv::resize(hi, plates.hi, preview_size, 0.0, 0.0, cv::INTER_AREA);
    }
    return plates;
}

[[nodiscard]] cv::Rect2d fraction_rect(int width, int height, double x, double y,
                                       double w, double h) {
    return {width * x, height * y, width * w, height * h};
}

[[nodiscard]] View style_view(const std::string& slug, const Geometry& geometry,
                              int width, int height) {
    const LonLatBox& context_box = geometry.country_mode
        ? geometry.country_view_box : geometry.lower48;
    if (slug == "01_editorial_vox") {
        return fit_box(context_box,
                       fraction_rect(width, height, 0.035, 0.055, 0.93, 0.815));
    }
    if (slug == "02_paper_cutout") {
        return fit_box(context_box,
                       fraction_rect(width, height, 0.05, 0.15, 0.90, 0.78));
    }
    if (slug == "03_dark_radar") {
        return fit_box(context_box,
                       fraction_rect(width, height, 0.03, 0.07, 0.94, 0.84));
    }
    if (slug == "04_blueprint") {
        return fit_box(context_box,
                       fraction_rect(width, height, 0.05, 0.08, 0.90, 0.70));
    }
    if (slug == "05_connection_arc") {
        if (geometry.country_mode) {
            return fit_box(geometry.country_view_box,
                           fraction_rect(width, height, 0.03, 0.02, 0.94, 0.96));
        }
        LonLatBox box;
        box.add(-90.4, 36.6);
        box.add(-73.8, 43.0);
        return fit_box(box, fraction_rect(width, height, 0.03, 0.02, 0.94, 0.96));
    }
    if (slug == "06_floating_island") {
        return fit_box(geometry.target_box,
                       fraction_rect(width, height, 0.22, 0.15, 0.38, 0.68));
    }
    throw std::runtime_error("unknown map style: " + slug);
}

[[nodiscard]] Plates build_plates(const std::string& slug, const Geometry& geometry,
                                  int width, int height) {
    const int plate_width = static_cast<int>(std::lround(width * kPlateScale));
    const double pfx = static_cast<double>(plate_width) / width;
    const int plate_height = static_cast<int>(std::lround(height * pfx));
    const View screen = style_view(slug, geometry, width, height);
    const View plate = scaled_view(screen, pfx);
    const int border = std::max(1, static_cast<int>(std::lround(pfx)));
    Plates plates;

    if (slug == "01_editorial_vox") {
        const cv::Scalar sea = rgb(0xCBD5E1);
        plates.base = textured_canvas(plate_width, plate_height, 0xCBD5E1, 0xE011,
                                      0.055, 0.022);
        if (!geometry.country_mode) {
            draw_country_context(plates.base, geometry.countries, plate,
                                 rgb(0xE9E4D8), rgb(0xC9C0B2), border, sea);
        }
        draw_region_context(plates.base, geometry, plate, rgb(0xF4F1EA),
                            rgb(0xB8B2A6), border, sea);
        plates.hi = plates.base.clone();
        fill_feature(plates.hi, *geometry.highlighted, plate, rgb(0xD94E34), sea);
        stroke_feature(plates.hi, *geometry.highlighted, plate, rgb(0xB33A25),
                       border + 1);
        return finalize_plates(plates.base, plates.hi, width, height);
    }

    if (slug == "02_paper_cutout") {
        plates.base = textured_canvas(plate_width, plate_height, 0xE3DED3, 0xE022,
                                      0.065, 0.028);
        draw_region_context(plates.base, geometry, plate, rgb(0xD8D0C1),
                            rgb(0xB8AD9B), border, rgb(0xE3DED3));
        plates.hi = plates.base.clone();
        const auto rings = scaled_rings(project_rings(*geometry.highlighted, plate),
                                        1.035, {-8.0 * pfx, -8.0 * pfx});
        cv::Mat shadow_mask(plates.hi.size(), CV_8UC1, cv::Scalar(0));
        fill_rings(shadow_mask, offset_rings(rings, {18.0 * pfx, 26.0 * pfx}),
                   cv::Scalar(255));
        cv::GaussianBlur(shadow_mask, shadow_mask, cv::Size(0, 0), 26.0 * pfx);
        blend_mask(plates.hi, shadow_mask, rgb(0x2B241B), 0.32);
        fill_rings(plates.hi, offset_rings(rings, {2.0 * pfx, 3.0 * pfx}),
                   rgb(0xCFC4B1));
        fill_rings(plates.hi, rings, rgb(0xF8F3E7));
        stroke_rings(plates.hi, rings, rgb(0xB9AE9B), border + 1);
        return finalize_plates(plates.base, plates.hi, width, height);
    }

    if (slug == "03_dark_radar") {
        plates.base = textured_canvas(plate_width, plate_height, 0x0B0F17, 0xE033,
                                      0.045, 0.012);
        draw_graticule(plates.base, plate, rgb(0x1B2636), std::max(1, border / 2),
                       5.0, true);
        draw_region_context(plates.base, geometry, plate, rgb(0x131A26),
                            rgb(0x2D3B55), border, rgb(0x0B0F17));
        plates.hi = plates.base.clone();
        fill_feature(plates.hi, *geometry.highlighted, plate, rgb(0x1E3A5F),
                     rgb(0x0B0F17));
        stroke_feature(plates.hi, *geometry.highlighted, plate, rgb(0x38BDF8),
                       border + 1);
        return finalize_plates(plates.base, plates.hi, width, height);
    }

    if (slug == "04_blueprint") {
        plates.base = textured_canvas(plate_width, plate_height, 0x0E2A47, 0xE044,
                                      0.035, 0.018);
        draw_graticule(plates.base, plate, rgb(0x17395C), std::max(1, border / 2),
                       3.0, false);
        const int inset = static_cast<int>(std::lround(46.0 * pfx));
        cv::rectangle(plates.base, {inset, inset, plate_width - inset * 2,
                                    plate_height - inset * 2},
                      rgb(0x3D6C93), border, cv::LINE_AA);
        draw_region_context(plates.base, geometry, plate, rgb(0x0E2A47),
                            rgb(0x8FB8DA), border, rgb(0x0E2A47), true);
        plates.hi = plates.base.clone();
        draw_hatch(plates.hi, *geometry.highlighted, plate, rgb(0xE3C68E),
                   std::max(6, static_cast<int>(std::lround(7.0 * pfx))), border);
        stroke_feature(plates.hi, *geometry.highlighted, plate, rgb(0x061726),
                       border * 4);
        stroke_feature(plates.hi, *geometry.highlighted, plate, rgb(0xDCEBF8),
                       std::max(2, border * 2));
        return finalize_plates(plates.base, plates.hi, width, height);
    }

    if (slug == "05_connection_arc") {
        const cv::Scalar water = rgb(0xC7D5DE);
        plates.base = textured_canvas(plate_width, plate_height, 0xC7D5DE, 0xE055,
                                      0.055, 0.022);
        if (!geometry.country_mode) {
            draw_country_context(plates.base, geometry.countries, plate,
                                 rgb(0xDED7C9), rgb(0xC3B9A8), border, water);
        }
        draw_region_context(plates.base, geometry, plate, rgb(0xE7E1D4),
                            rgb(0xB7AE9E), border, water);
        plates.hi = plates.base.clone();
        fill_feature(plates.hi, *geometry.highlighted, plate, rgb(0xD94E34),
                     water);
        stroke_feature(plates.hi, *geometry.highlighted, plate, rgb(0xA43A28),
                       border + 1);
        return finalize_plates(plates.base, plates.hi, width, height);
    }

    if (slug == "06_floating_island") {
        plates.base = textured_canvas(plate_width, plate_height, 0xEDE8DE, 0xE066,
                                      0.055, 0.026);
        plates.hi = plates.base.clone();
        const auto rings = project_rings(*geometry.highlighted, plate);
        cv::Mat shadow_mask(plates.hi.size(), CV_8UC1, cv::Scalar(0));
        fill_rings(shadow_mask, offset_rings(rings, {18.0 * pfx, 28.0 * pfx}),
                   cv::Scalar(255));
        cv::GaussianBlur(shadow_mask, shadow_mask, cv::Size(0, 0), 30.0 * pfx);
        blend_mask(plates.hi, shadow_mask, rgb(0x3B2A20), 0.28);
        fill_rings(plates.hi, rings, rgb(0xD94E34));
        stroke_rings(plates.hi, rings, rgb(0x7E2E1F), border + 1);
        return finalize_plates(plates.base, plates.hi, width, height);
    }

    throw std::runtime_error("unknown map style: " + slug);
}

[[nodiscard]] cv::Mat sample_plates(const Plates& plates, double zoom,
                                    double highlight_alpha, int width, int height) {
    const bool full_plate = zoom <= 1.0000001;
    const int crop_width = full_plate
        ? plates.base.cols
        : std::max(2, static_cast<int>(std::lround(plates.base.cols / zoom)));
    const int crop_height = full_plate
        ? plates.base.rows
        : std::max(2, static_cast<int>(std::lround(plates.base.rows / zoom)));
    const cv::Point2d center(plates.base.cols * 0.5, plates.base.rows * 0.5);
    const cv::Size crop_size(crop_width, crop_height);

    auto extract = [&](const cv::Mat& plate) {
        cv::Mat out;
        if (full_plate) {
            out = plate.clone();
        } else {
            cv::getRectSubPix(plate, crop_size, center, out);
        }
        cv::resize(out, out, cv::Size(width, height), 0.0, 0.0, cv::INTER_AREA);
        return out;
    };

    if (!plates.hi.empty() && highlight_alpha >= 0.999) {
        return extract(plates.hi);
    }
    cv::Mat frame = extract(plates.base);
    if (!plates.hi.empty() && highlight_alpha > 0.001) {
        cv::Mat hi = extract(plates.hi);
        cv::addWeighted(frame, 1.0 - highlight_alpha, hi, highlight_alpha, 0.0,
                        frame);
    }
    return frame;
}
}  // namespace chrononplugin::maps::editorial_pack_detail
