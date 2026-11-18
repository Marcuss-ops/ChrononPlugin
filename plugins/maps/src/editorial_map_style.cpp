#include "chrononplugin/maps/editorial_map_pack.hpp"

namespace chrononplugin::maps {

const char* editorial_map_style_id(EditorialMapStyle style) noexcept {
    switch (style) {
        case EditorialMapStyle::kEditorialVox: return "01_editorial_vox";
        case EditorialMapStyle::kPaperCutout: return "02_paper_cutout";
        case EditorialMapStyle::kDarkRadar: return "03_dark_radar";
        case EditorialMapStyle::kBlueprint: return "04_blueprint";
        case EditorialMapStyle::kConnectionArc: return "05_connection_arc";
        case EditorialMapStyle::kFloatingIsland: return "06_floating_island";
    }
    return "";
}

std::vector<EditorialMapStyle> editorial_map_styles() {
    return {EditorialMapStyle::kEditorialVox, EditorialMapStyle::kPaperCutout,
            EditorialMapStyle::kDarkRadar, EditorialMapStyle::kBlueprint,
            EditorialMapStyle::kConnectionArc,
            EditorialMapStyle::kFloatingIsland};
}

}  // namespace chrononplugin::maps
