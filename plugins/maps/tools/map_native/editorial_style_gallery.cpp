// CLI for EditorialMapPack: renders one or more of the six documentary map
// treatments. The rendering engine lives in the pack library; this file only
// parses arguments and selects styles.

#include "chrononplugin/maps/editorial_map_pack.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using chrononplugin::maps::EditorialMapRequest;
using chrononplugin::maps::EditorialMapStyle;

namespace {

void usage(const char* argv0) {
    std::fprintf(
        stderr,
        "usage: %s --out-dir DIR [--states FILE] [--countries FILE] "
        "[--fonts-dir DIR] [--only 01,03] [--width 1920] [--height 1080] "
        "[--fps 30] [--seconds 8] [--crf 18] [--threads N] "
        "[--ffmpeg ffmpeg]\n",
        argv0);
}

[[nodiscard]] bool selected(const std::string& id,
                            const std::vector<std::string>& tokens) {
    if (tokens.empty()) return true;
    for (const std::string& token : tokens) {
        if (id.rfind(token, 0) == 0) return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    std::string out_dir = "out/editorial-map-styles";
    std::string states_path;
    std::string countries_path;
    std::string fonts_dir;
    std::string only;
    std::string ffmpeg = "ffmpeg";
    int width = 1920;
    int height = 1080;
    int fps = 30;
    double seconds = 8.0;
    int crf = 18;
    int threads = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--out-dir") out_dir = next("--out-dir");
        else if (arg == "--states") states_path = next("--states");
        else if (arg == "--countries") countries_path = next("--countries");
        else if (arg == "--fonts-dir") fonts_dir = next("--fonts-dir");
        else if (arg == "--only") only = next("--only");
        else if (arg == "--width") width = std::stoi(next("--width"));
        else if (arg == "--height") height = std::stoi(next("--height"));
        else if (arg == "--fps") fps = std::stoi(next("--fps"));
        else if (arg == "--seconds") seconds = std::stod(next("--seconds"));
        else if (arg == "--crf") crf = std::stoi(next("--crf"));
        else if (arg == "--threads") threads = std::stoi(next("--threads"));
        else if (arg == "--ffmpeg") ffmpeg = next("--ffmpeg");
        else {
            usage(argv[0]);
            return 2;
        }
    }
    if (width <= 0 || height <= 0 || fps <= 0 || seconds <= 0.0 ||
        threads < 0) {
        std::fprintf(stderr,
                     "width, height, fps and seconds must be positive; "
                     "threads must be >= 0\n");
        return 2;
    }
    const int frame_count =
        std::max(2, static_cast<int>(std::lround(seconds * fps)));

    std::vector<std::string> requested;
    if (!only.empty()) {
        std::size_t start = 0;
        while (start <= only.size()) {
            const std::size_t comma = only.find(',', start);
            const std::string token = only.substr(
                start, comma == std::string::npos ? std::string::npos
                                                  : comma - start);
            if (!token.empty()) requested.push_back(token);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        for (const std::string& token : requested) {
            bool matched = false;
            for (const EditorialMapStyle style :
                 chrononplugin::maps::editorial_map_styles()) {
                if (std::string(chrononplugin::maps::editorial_map_style_id(style))
                        .rfind(token, 0) == 0) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                std::fprintf(stderr, "unknown editorial style selector: %s\n",
                             token.c_str());
                return 2;
            }
        }
    }

    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        std::fprintf(stderr, "cannot create output directory: %s\n",
                     out_dir.c_str());
        return 2;
    }

    int failures = 0;
    for (const EditorialMapStyle style :
         chrononplugin::maps::editorial_map_styles()) {
        const std::string id = chrononplugin::maps::editorial_map_style_id(style);
        if (!selected(id, requested)) continue;

        EditorialMapRequest request;
        request.style = style;
        request.output_video = (fs::path(out_dir) / (id + ".mp4")).string();
        request.poster_png =
            (fs::path(out_dir) / (id + "_poster.png")).string();
        request.output_width = width;
        request.output_height = height;
        request.fps = fps;
        request.frame_count = frame_count;
        request.crf = crf;
        request.threads = threads;
        request.ffmpeg = ffmpeg;
        request.states_geojson = states_path;
        request.countries_geojson = countries_path;
        request.fonts_dir = fonts_dir;

        const auto result = chrononplugin::maps::render_editorial_map(request);
        if (!result.ok) {
            std::fprintf(stderr, "[map-style] %s FAILED: %s\n", id.c_str(),
                         result.error.c_str());
            ++failures;
            continue;
        }
        std::printf(
            "[map-style] %s frames=%d %.2fs video=%s poster=%s wall=%.2fs\n",
            id.c_str(), result.frame_count, seconds,
            request.output_video.c_str(), request.poster_png.c_str(),
            result.render_seconds);
        std::fflush(stdout);
    }
    return failures == 0 ? 0 : 1;
}
