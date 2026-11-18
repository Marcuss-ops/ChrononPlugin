// Emits the neutral EditorialMapScene DTO for the six documentary map styles.
//
// The tool loads the boundary catalogs, projects and simplifies them in
// ChrononPlugin and writes canvas-space JSON. ChrononTemplate consumes that
// document; Chronon3D never sees GeoJSON.
//
//   maps_editorial_scene_emit --out .tmp/editorial-scenes
//   maps_editorial_scene_emit --style blueprint --out .tmp/editorial-scenes
//
// `--style` accepts `all`, the public id (`04_blueprint`) or the short id
// (`blueprint`). One file is written per style: <out>/<short-id>.scene.json.

#include "chrononplugin/maps/editorial_map_scene.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using chrononplugin::maps::EditorialMapSceneOptions;
using chrononplugin::maps::EditorialMapSceneResult;
using chrononplugin::maps::EditorialMapStyle;

[[nodiscard]] std::string shortId(EditorialMapStyle style) {
    const std::string id = chrononplugin::maps::editorial_map_style_id(style);
    return id.size() > 3 ? id.substr(3) : id;
}

[[nodiscard]] bool matchesStyle(EditorialMapStyle style, const std::string& requested) {
    const std::string full = chrononplugin::maps::editorial_map_style_id(style);
    return requested == full || requested == shortId(style);
}

[[nodiscard]] bool parseNumber(const std::string& text, double& out) {
    try {
        std::size_t consumed = 0;
        out = std::stod(text, &consumed);
        return consumed == text.size();
    } catch (const std::exception&) {
        return false;
    }
}

void usage() {
    std::cout
        << "Usage: maps_editorial_scene_emit --out <dir> [options]\n"
        << "  --style <all|short-id|full-id>   default: all\n"
        << "  --catalog-dir <dir>              states + Natural Earth boundaries\n"
        << "  --width <px> --height <px>       default: 1920x1080\n"
        << "  --fps <n> --frames <n>           default: 30 / 240\n"
        << "  --highlight <name|code>          default: Indiana\n"
        << "  --title <text> --subtitle <text>\n"
        << "  --pin-label <text>\n"
        << "  --lat <deg> --lon <deg>          default: 39.7684 / -86.1581\n"
        << "  --tolerance <px>                 default: 0.9\n";
}

}  // namespace

int main(int argc, char** argv) {
    EditorialMapSceneOptions options;
    std::string outDir = ".tmp/editorial-scenes";
    std::string requestedStyle = "all";

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string flag = argv[i];
            auto value = [&](const char* name) -> std::string {
                if (i + 1 >= argc)
                    throw std::runtime_error(std::string("missing value for ") + name);
                return argv[++i];
            };
            if (flag == "--out") {
                outDir = value("--out");
            } else if (flag == "--style") {
                requestedStyle = value("--style");
            } else if (flag == "--catalog-dir") {
                const std::filesystem::path dir = value("--catalog-dir");
                options.states_geojson = (dir / "us_states_editorial.geojson").string();
                options.countries_geojson =
                    (dir / "ne_50m_admin_0_countries.geojson").string();
            } else if (flag == "--width") {
                options.width = std::stoi(value("--width"));
            } else if (flag == "--height") {
                options.height = std::stoi(value("--height"));
            } else if (flag == "--fps") {
                options.fps = std::stoi(value("--fps"));
            } else if (flag == "--frames") {
                options.frame_count = std::stoi(value("--frames"));
            } else if (flag == "--highlight") {
                options.highlight_state = value("--highlight");
            } else if (flag == "--title") {
                options.title = value("--title");
            } else if (flag == "--subtitle") {
                options.subtitle = value("--subtitle");
            } else if (flag == "--pin-label") {
                options.pin_label = value("--pin-label");
            } else if (flag == "--lat") {
                double parsed = 0.0;
                if (!parseNumber(value("--lat"), parsed))
                    throw std::runtime_error("--lat must be a number");
                options.pin_latitude = parsed;
            } else if (flag == "--lon") {
                double parsed = 0.0;
                if (!parseNumber(value("--lon"), parsed))
                    throw std::runtime_error("--lon must be a number");
                options.pin_longitude = parsed;
            } else if (flag == "--tolerance") {
                double parsed = 0.0;
                if (!parseNumber(value("--tolerance"), parsed))
                    throw std::runtime_error("--tolerance must be a number");
                options.simplify_tolerance = parsed;
            } else if (flag == "--help" || flag == "-h") {
                usage();
                return 0;
            } else {
                throw std::runtime_error("unknown argument: " + flag);
            }
        }

        std::vector<EditorialMapStyle> selected;
        for (EditorialMapStyle style : chrononplugin::maps::editorial_map_styles()) {
            if (requestedStyle == "all" || matchesStyle(style, requestedStyle))
                selected.push_back(style);
        }
        if (selected.empty())
            throw std::runtime_error("unknown style: " + requestedStyle);

        std::filesystem::create_directories(outDir);
        for (EditorialMapStyle style : selected) {
            const EditorialMapSceneResult result =
                chrononplugin::maps::build_editorial_map_scene(style, options);
            if (!result.ok) {
                std::cerr << "editorial scene emit error [" << shortId(style)
                          << "]: " << result.error << '\n';
                return 1;
            }
            const std::filesystem::path path =
                std::filesystem::path(outDir) / (shortId(style) + ".scene.json");
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out || !(out << result.json << '\n')) {
                std::cerr << "could not write " << path << '\n';
                return 1;
            }
            std::cout << "Wrote " << path << " (" << result.json.size() << " bytes)\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "editorial scene emit error: " << error.what() << '\n';
        usage();
        return 1;
    }
}
