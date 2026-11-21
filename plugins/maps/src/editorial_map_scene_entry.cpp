// EditorialMapScene entry point: catalog loading, option validation and
// serialization. The geometry/layout/style helpers live in the other
// editorial_map_scene_*.cpp translation units.

#include "chrononplugin/maps/editorial_map_scene.hpp"

#include "editorial_map_scene_internal.hpp"

#include <cmath>
#include <filesystem>
#include <string>

namespace chrononplugin::maps {

using namespace editorial_map_scene_detail;

const char* editorial_map_scene_schema_id() noexcept {
    return "chrononplugin.editorial-map-scene.v1";
}

EditorialMapSceneResult build_editorial_map_scene(EditorialMapStyle style,
                                                  const EditorialMapSceneOptions& options) {
    EditorialMapSceneResult result;
    auto fail = [&result](std::string message) {
        result.ok = false;
        result.error = std::move(message);
        return result;
    };

    if (options.width < 64 || options.width > 8192 || options.height < 64 ||
        options.height > 8192)
        return fail("canvas dimensions must be 64..8192 pixels");
    if (options.fps < 1 || options.fps > 240) return fail("fps must be in 1..240");
    if (options.frame_count < 2) return fail("frame_count must be at least 2");
    if (!std::isfinite(options.simplify_tolerance) ||
        options.simplify_tolerance < 0.1 || options.simplify_tolerance > 8.0)
        return fail("simplify_tolerance must be in [0.1,8]");

    try {
        const fs::path statesPath = resolveCatalogFile(
            options.states_geojson, "CHRONON_MAPS_CATALOG_DIR",
            "us_states_editorial.geojson", CHRONON_MAPS_EDITORIAL_STATES_DEFAULT);
        const fs::path countriesPath = resolveCatalogFile(
            options.countries_geojson, "CHRONON_MAPS_CATALOG_DIR",
            "ne_50m_admin_0_countries.geojson", CHRONON_MAPS_COUNTRIES_DEFAULT);

        const GeoFeatureCollection states = loadGeoJson(statesPath);
        GeoFeatureCollection countries;
        try {
            countries = loadGeoJson(countriesPath);
        } catch (const std::exception&) {
            // A continental treatment can run without country context, exactly
            // like the raster pack.
        }

        const GeoFeature* highlight = findFeature(states, "postal", options.highlight_state);
        if (highlight == nullptr) highlight = findFeature(states, "name", options.highlight_state);
        if (highlight == nullptr)
            return fail("highlight state not found in the state catalog: " +
                        options.highlight_state);

        Json scene = buildScene(style, options, states, countries, highlight);
        result.json = scene.dump(2);
        result.ok = true;
        return result;
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}
}  // namespace chrononplugin::maps
