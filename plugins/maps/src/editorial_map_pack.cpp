// Native editorial map-style gallery — six documentary map treatments in C++.
//
// This tool renders the styles as full-frame 1920x1080 clips through the same
// C++/OpenCV map stack used by the native map renderer:
//
//   01 editorial_vox     warm paper, terracotta state, serif/sans editorial type
//   02 paper_cutout      raised state plate with a soft cast shadow
//   03 dark_radar        night-blue grid, mono type, concentric radar pulse
//   04 blueprint         cobalt technical sheet, 45-degree hatch on the state
//   05 connection_arc    tight corridor crop, two-point Bezier arc + ground shadow
//   06 floating_island   isolated state silhouette, 45-degree callout cartouche
//
// All geometry comes from Natural Earth: the simplified US-states asset built
// by build_editorial_map_assets.py plus the 50m countries catalog. Every clip
// gets the same cinematic grammar: an imperceptible 100% -> 106% push-in, a
// 0.35 s state ignition, a pin/label second beat, and typography that rises
// into place. Plates are composed at 2.12x (2x supersampling + 6% overscan) and
// resampled per frame with INTER_AREA, so state borders stay crisp under the
// push-in.
//
// Usage:
//   maps_editorial_style_gallery --out-dir out/editorial-map-styles
//     [--states asset.geojson] [--countries ne_50m...geojson]
//     [--fonts-dir Chronon3d/assets/fonts] [--only 01,03]
//     [--width 1920] [--height 1080] [--fps 30] [--seconds 8]
//
// The raster helpers live in editorial_map_pack_geo/text/plates/overlays.cpp;
// this translation unit owns the CLI vocabulary and the frame pipeline.

#include "chrononplugin/maps/editorial_map_pack.hpp"
#include "chrononplugin/maps/geojson.hpp"

#include "editorial_map_pack_internal.hpp"

#include <opencv2/core/utility.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef CHRONON_MAPS_EDITORIAL_STATES_DEFAULT
#define CHRONON_MAPS_EDITORIAL_STATES_DEFAULT "plugins/maps/assets/us_states_editorial.geojson"
#endif
#ifndef CHRONON_MAPS_COUNTRIES_DEFAULT
#define CHRONON_MAPS_COUNTRIES_DEFAULT "../ChrononTemplate/catalog/ne_50m_admin_0_countries.geojson"
#endif
#ifndef CHRONON_MAPS_FONTS_DIR_DEFAULT
#define CHRONON_MAPS_FONTS_DIR_DEFAULT "../Chronon3d/assets/fonts"
#endif
#ifndef CHRONON_MAPS_FONTS_DIR_SOURCE
#define CHRONON_MAPS_FONTS_DIR_SOURCE "../Chronon3d/assets/fonts"
#endif

namespace chrononplugin::maps {

namespace {
namespace fs = std::filesystem;

struct Style {
    std::string slug;
    double zoom = 0.05;
    double grain = 0.03;
    double vignette = 0.18;
};

[[nodiscard]] std::vector<Style> styles() {
    return {
        {"01_editorial_vox", 0.060, 0.045, 0.20},
        {"02_paper_cutout", 0.050, 0.035, 0.16},
        {"03_dark_radar", 0.055, 0.022, 0.34},
        {"04_blueprint", 0.050, 0.022, 0.18},
        {"05_connection_arc", 0.070, 0.035, 0.20},
        {"06_floating_island", 0.040, 0.040, 0.14},
    };
}

[[nodiscard]] std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (const char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

[[nodiscard]] bool style_for_enum(EditorialMapStyle enum_style, Style& out) {
    const std::string id = editorial_map_style_id(enum_style);
    for (const Style& candidate : styles()) {
        if (candidate.slug == id) {
            out = candidate;
            return true;
        }
    }
    return false;
}

}  // namespace

using namespace editorial_pack_detail;

EditorialMapResult render_editorial_map(const EditorialMapRequest& request) {
    EditorialMapResult result;
    auto fail = [&result](std::string message) {
        result.ok = false;
        result.error = std::move(message);
        return result;
    };

    if (request.output_video.empty()) return fail("output_video is required");
    if (request.output_width <= 0 || request.output_height <= 0) {
        return fail("output dimensions must be positive");
    }
    if (request.fps <= 0) return fail("fps must be positive");
    if (request.frame_count < 2) return fail("frame_count must be at least 2");

    Style style;
    if (!style_for_enum(request.style, style)) {
        return fail("unknown editorial map style");
    }

    const fs::path states_path = resolve_catalog_file(
        request.states_geojson, "CHRONON_MAPS_CATALOG_DIR",
        "us_states_editorial.geojson", CHRONON_MAPS_EDITORIAL_STATES_DEFAULT);
    const fs::path countries_path = resolve_catalog_file(
        request.countries_geojson, "CHRONON_MAPS_CATALOG_DIR",
        "ne_50m_admin_0_countries.geojson", CHRONON_MAPS_COUNTRIES_DEFAULT);
    std::string fonts_dir = request.fonts_dir;
    if (fonts_dir.empty()) {
        const char* env_fonts = std::getenv("CHRONON_MAPS_FONTS_DIR");
        fonts_dir = env_fonts != nullptr && *env_fonts != '\0'
            ? env_fonts : CHRONON_MAPS_FONTS_DIR_DEFAULT;
        if (!fs::is_directory(fonts_dir) &&
            (env_fonts == nullptr || *env_fonts == '\0')) {
            fonts_dir = CHRONON_MAPS_FONTS_DIR_SOURCE;
        }
    }

    try {
        const auto started = std::chrono::steady_clock::now();
        const Geometry geometry =
            load_geometry(states_path, countries_path, request.highlight_state,
                          request.highlight_country);
        // Overlay copy follows the resolved target: a region render must not
        // keep the country title or its capital pin.
        EditorialMapRequest effective = request;
        if (geometry.region_mode) {
            const std::string title = upper_copy(geometry.place_name);
            effective.title = title;
            effective.subtitle = "REGIONE ITALIANA";
            effective.pin_label = title;
            effective.pin_latitude = geometry.place_pin_lat;
            effective.pin_longitude = geometry.place_pin_lon;
        }

        const fs::path fonts_path(fonts_dir);
        {
            // Fail before opening the encoder if the font set is incomplete.
            Fonts probe(fonts_path);
            (void)probe;
        }

        unsigned int hardware = std::thread::hardware_concurrency();
        if (hardware == 0) hardware = 4;
        // Past twelve workers the per-frame ROI work stops scaling: the
        // threaded resize/blur path saturates memory bandwidth and x264 also
        // wants cores. An explicit request can still override this.
        const int workers = request.threads > 0
            ? request.threads
            : static_cast<int>(std::min(hardware, 12U));
        cv::setNumThreads(workers);

        const Plates plates =
            build_plates(style.slug, geometry, request.output_width,
                         request.output_height);
        const View screen = style_view(style.slug, geometry,
                                       request.output_width,
                                       request.output_height);
        const Finish finish = make_finish(
            request.output_width, request.output_height,
            0x5EED0000U + static_cast<unsigned int>(
                std::hash<std::string>{}(style.slug) & 0xFFFFU),
            style.grain, style.vignette);

        const fs::path video_path(request.output_video);
        if (!video_path.parent_path().empty()) {
            fs::create_directories(video_path.parent_path());
        }
        const fs::path poster_path(request.poster_png);
        if (!poster_path.empty() && !poster_path.parent_path().empty()) {
            fs::create_directories(poster_path.parent_path());
        }

        const std::string ffmpeg = request.ffmpeg.empty() ? "ffmpeg" : request.ffmpeg;
        const std::string rate =
            request.fps_num > 0 && request.fps_den > 0
                ? std::to_string(request.fps_num) + "/" +
                      std::to_string(request.fps_den)
                : std::to_string(request.fps);
        const std::string encoder_args = request.gpu_encoder
            ? " -c:v h264_nvenc -gpu " + std::to_string(request.gpu_index) +
                  " -preset p6 -tune hq -rc vbr -cq " +
                  std::to_string(request.gpu_quality) + " -b:v 0"
            : " -c:v libx264 -preset veryfast -crf " +
                  std::to_string(request.crf);
        const std::string command =
            ffmpeg + " -hide_banner -loglevel error -y -f rawvideo -pix_fmt bgr24 -s " +
            std::to_string(request.output_width) + "x" +
            std::to_string(request.output_height) + " -r " + rate +
            " -i -" + encoder_args + " -pix_fmt yuv420p -movflags +faststart " +
            shell_quote(video_path.string());

        std::FILE* encoder = ::popen(command.c_str(), "w");
        if (encoder == nullptr) {
            return fail("cannot launch ffmpeg");
        }

        const int poster_frame = std::min(
            request.frame_count - 1,
            static_cast<int>(std::lround(request.frame_count * 0.80)));
        std::atomic<bool> failed{false};
        std::mutex error_mutex;
        std::string error_text;

        const auto render_one = [&](int frame_index, Fonts& fonts) {
            const double t = static_cast<double>(frame_index) /
                             static_cast<double>(request.frame_count - 1);
            const double zoom = 1.0 + style.zoom * t;
            const double highlight_alpha = plates.hi.empty()
                ? 0.0 : smooth01(0.015, 0.090, t);
            cv::Mat frame = sample_plates(plates, zoom, highlight_alpha,
                                          request.output_width,
                                          request.output_height);
            View zoomed = screen;
            zoomed.scale *= zoom;
            draw_overlay(style.slug, frame, geometry, fonts, zoomed, t,
                         effective.output_width, effective.output_height,
                         effective);
            finish.apply(frame);
            return frame;
        };

        const int batch_size = std::clamp(workers * 2, 4, 16);

        bool write_failed = false;
        for (int start = 0; start < request.frame_count && !write_failed;
             start += batch_size) {
            const int count = std::min(batch_size, request.frame_count - start);
            std::vector<cv::Mat> frames(static_cast<std::size_t>(count));

            cv::parallel_for_(cv::Range(0, count), [&](const cv::Range& range) {
                thread_local std::string tl_fonts_dir;
                thread_local std::unique_ptr<Fonts> tl_fonts;
                try {
                    if (!tl_fonts || tl_fonts_dir != fonts_dir) {
                        tl_fonts = std::make_unique<Fonts>(fonts_path);
                        tl_fonts_dir = fonts_dir;
                    }
                    for (int i = range.start; i < range.end; ++i) {
                        if (failed.load(std::memory_order_relaxed)) return;
                        frames[static_cast<std::size_t>(i)] =
                            render_one(start + i, *tl_fonts);
                    }
                } catch (const std::exception& exc) {
                    if (!failed.exchange(true)) {
                        std::lock_guard<std::mutex> lock(error_mutex);
                        error_text = exc.what();
                    }
                } catch (...) {
                    if (!failed.exchange(true)) {
                        std::lock_guard<std::mutex> lock(error_mutex);
                        error_text = "unknown frame rendering failure";
                    }
                }
            });

            if (failed.load()) break;
            for (int i = 0; i < count; ++i) {
                cv::Mat& frame = frames[static_cast<std::size_t>(i)];
                if (frame.empty() || frame.cols != request.output_width ||
                    frame.rows != request.output_height) {
                    write_failed = true;
                    error_text = "frame renderer returned an invalid frame";
                    break;
                }
                if (!poster_path.empty() && start + i == poster_frame &&
                    !cv::imwrite(poster_path.string(), frame)) {
                    write_failed = true;
                    error_text = "cannot write the poster frame";
                    break;
                }
                const std::size_t bytes = static_cast<std::size_t>(
                    request.output_width) * request.output_height * 3U;
                if (std::fwrite(frame.data, 1, bytes, encoder) != bytes) {
                    write_failed = true;
                    error_text = "short write to the video encoder";
                    break;
                }
            }
        }

        const int encoder_status = ::pclose(encoder);
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        if (write_failed || failed.load() || encoder_status != 0) {
            std::remove(video_path.string().c_str());
            if (error_text.empty()) {
                error_text = "ffmpeg failed with status " +
                             std::to_string(encoder_status);
            }
            return fail(error_text);
        }

        result.ok = true;
        result.frame_count = request.frame_count;
        result.render_seconds = elapsed;
        return result;
    } catch (const std::exception& exc) {
        return fail(exc.what());
    } catch (...) {
        return fail("unknown editorial map rendering failure");
    }
}
}  // namespace chrononplugin::maps
