// Parity dump tool for the native vector tile/plate rasterizer.
//
// Renders the tiles and plates named in a JSON spec and writes each one as a
// raw 8-bit BGR blob, so tools/map_native/parity_raster_check.py can compare
// them byte-for-byte with the Python reference renderer. The spec is:
//
//   {
//     "countries_geojson":    "<path>",
//     "italy_admin2_geojson": "<path>",
//     "tiles":  [[z, x, y], ...],
//     "plates": [{"left": L, "top": T, "width": W, "height": H, "zoom": Z}, ...]
//   }
//
// Output files are named by list index: tile_<i>.bin and plate_<i>.bin, each
// `rows * cols * 3` bytes of BGR data. The tool never guesses: a missing
// source or malformed spec is an error, not a blank frame.

#include "chrononplugin/maps/map_plate_raster.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace {

int write_blob(const std::string& path, const cv::Mat& image) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        std::fprintf(stderr, "cannot open output: %s\n", path.c_str());
        return 4;
    }
    const std::size_t bytes = image.total() * image.elemSize();
    const std::size_t written = std::fwrite(image.data, 1, bytes, f);
    std::fclose(f);
    if (written != bytes) {
        std::fprintf(stderr, "short write: %s\n", path.c_str());
        return 5;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <spec.json> <out_dir>\n", argv[0]);
        return 2;
    }
    const std::string spec_path = argv[1];
    const std::string out_dir = argv[2];

    std::FILE* f = std::fopen(spec_path.c_str(), "rb");
    if (f == nullptr) {
        std::fprintf(stderr, "cannot open spec: %s\n", spec_path.c_str());
        return 3;
    }
    std::string data;
    char buffer[1 << 16];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
        data.append(buffer, got);
    }
    std::fclose(f);

    const nlohmann::json spec = nlohmann::json::parse(data);
    const std::string countries = spec.at("countries_geojson").get<std::string>();
    const std::string admin2 = spec.at("italy_admin2_geojson").get<std::string>();

    chrononplugin::maps::VectorPlateRaster raster({countries, admin2});

    std::size_t index = 0;
    for (const auto& tile : spec.at("tiles")) {
        const int z = tile.at(0).get<int>();
        const std::int64_t x = tile.at(1).get<std::int64_t>();
        const std::int64_t y = tile.at(2).get<std::int64_t>();
        const cv::Mat image = raster.render_tile(z, x, y);
        const std::string path = out_dir + "/tile_" + std::to_string(index) + ".bin";
        if (const int rc = write_blob(path, image); rc != 0) return rc;
        ++index;
    }

    index = 0;
    for (const auto& plate : spec.at("plates")) {
        const double left = plate.at("left").get<double>();
        const double top = plate.at("top").get<double>();
        const int width = plate.at("width").get<int>();
        const int height = plate.at("height").get<int>();
        const int zoom = plate.at("zoom").get<int>();
        const cv::Mat image = raster.compose_plate(left, top, width, height, zoom);
        const std::string path = out_dir + "/plate_" + std::to_string(index) + ".bin";
        if (const int rc = write_blob(path, image); rc != 0) return rc;
        ++index;
    }
    return 0;
}
