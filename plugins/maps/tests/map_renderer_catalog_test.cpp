// Pins the published map-renderer vocabulary to the renderer implementation.
//
// `plugins/maps/maps-renderer-catalog.v1.json` is the single machine-readable
// map catalog consumed by other repositories (RenderingGen embeds it). The
// renderer validates against the C++ sets exposed by map_plate_render.hpp, so
// this test fails whenever the catalog and those sets drift: a composition
// cannot be advertised but refused, nor accepted but hidden.
#include "chrononplugin/maps/map_plate_render.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <set>
#include <string>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

std::set<std::string> string_set(const nlohmann::json& document,
                                 const char* key) {
    std::set<std::string> values;
    const auto& array = document.at(key);
    for (const auto& value : array) {
        values.insert(value.get<std::string>());
    }
    return values;
}

}  // namespace

int main() {
    std::ifstream catalog_file(CHRONONPLUGIN_MAPS_RENDERER_CATALOG);
    CHECK(catalog_file.good());
    if (!catalog_file.good()) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }

    nlohmann::json catalog;
    try {
        catalog = nlohmann::json::parse(catalog_file);
    } catch (const std::exception& exc) {
        std::printf("FAIL: catalog is not valid JSON: %s\n", exc.what());
        return 1;
    }

    CHECK(catalog.at("schema") == "chrononplugin.maps-renderer-catalog.v1");
    CHECK(catalog.at("version") == 1);
    CHECK(catalog.at("source") == "ChrononPlugin");
    CHECK(catalog.at("renderer").at("binary") == "maps_dynamic_map_render");
    CHECK(catalog.at("renderer").at("telemetry_schema") ==
          "chronon.dynamic-map-telemetry.v1");

    auto catalog_animations = string_set(catalog, "map_animations");
    catalog_animations.insert(catalog.at("base_flyover").get<std::string>());
    CHECK(catalog_animations ==
          chrononplugin::maps::supported_map_animations());
    CHECK(string_set(catalog, "camera_animations") ==
          chrononplugin::maps::supported_map_camera_animations());
    CHECK(string_set(catalog, "label_animations") ==
          chrononplugin::maps::supported_map_label_animations());
    CHECK(string_set(catalog, "basemaps") ==
          chrononplugin::maps::supported_map_basemaps());

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("map_renderer_catalog: all checks passed\n");
    return 0;
}
