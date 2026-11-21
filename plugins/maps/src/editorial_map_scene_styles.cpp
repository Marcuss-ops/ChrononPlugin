// Per-style editorial scene composition. Split out of editorial_map_scene.cpp.

#include "editorial_map_scene_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chrononplugin::maps::editorial_map_scene_detail {

[[nodiscard]] Json paletteFor(EditorialMapStyle style) {
    switch (style) {
        case EditorialMapStyle::kEditorialVox:
            return Json{{"background", "#CBD5E1"}, {"accent", "#D94E34"},
                        {"ink", "#1F2937"}, {"muted", "#556270"},
                        {"paper", "#F4F1EA"},
                        {"countries_fill", "#E9E4D8"},
                        {"countries_stroke", "#C9C0B2"},
                        {"states_fill", "#F4F1EA"}, {"states_stroke", "#B8B2A6"},
                        {"highlight_fill", "#D94E34"},
                        {"highlight_stroke", "#B33A25"}, {"grid", ""}, {"hatch", ""},
                        {"panel_fill", ""}, {"panel_stroke", ""}, {"shadow", ""}};
        case EditorialMapStyle::kPaperCutout:
            return Json{{"background", "#E3DED3"}, {"accent", "#C05A3E"},
                        {"ink", "#2A241C"}, {"muted", "#6B6255"},
                        {"paper", "#F8F3E7"},
                        {"countries_fill", ""}, {"countries_stroke", ""},
                        {"states_fill", "#D8D0C1"}, {"states_stroke", "#B8AD9B"},
                        {"highlight_fill", "#F8F3E7"},
                        {"highlight_stroke", "#B9AE9B"}, {"grid", ""}, {"hatch", ""},
                        {"panel_fill", ""}, {"panel_stroke", ""}, {"shadow", "#2B241B"}};
        case EditorialMapStyle::kDarkRadar:
            return Json{{"background", "#0B0F17"}, {"accent", "#38BDF8"},
                        {"ink", "#F1F5F9"}, {"muted", "#6F849E"},
                        {"paper", "#0B0F17"},
                        {"countries_fill", ""}, {"countries_stroke", ""},
                        {"states_fill", "#131A26"}, {"states_stroke", "#2D3B55"},
                        {"highlight_fill", ""}, {"highlight_stroke", ""},
                        {"grid", "#1B2636"}, {"hatch", ""},
                        {"panel_fill", ""}, {"panel_stroke", ""}, {"shadow", ""}};
        case EditorialMapStyle::kBlueprint:
            return Json{{"background", "#0E2A47"}, {"accent", "#E3C68E"},
                        {"ink", "#E8F3FC"}, {"muted", "#7FA5C6"},
                        {"paper", "#0E2A47"},
                        {"countries_fill", ""}, {"countries_stroke", ""},
                        {"states_fill", ""}, {"states_stroke", "#8FB8DA"},
                        {"highlight_fill", "#0E2A47"},
                        {"highlight_stroke", "#DCEBF8"}, {"grid", "#17395C"},
                        {"hatch", "#E3C68E"},
                        {"panel_fill", "#0A2136"}, {"panel_stroke", "#3D6C93"},
                        {"shadow", ""}};
        case EditorialMapStyle::kConnectionArc:
            return Json{{"background", "#C7D5DE"}, {"accent", "#D94E34"},
                        {"ink", "#1F2937"}, {"muted", "#5B6470"},
                        {"paper", "#F8F3E7"},
                        {"countries_fill", "#DED7C9"},
                        {"countries_stroke", "#C3B9A8"},
                        {"states_fill", "#E7E1D4"}, {"states_stroke", "#B7AE9E"},
                        {"highlight_fill", "#D94E34"},
                        {"highlight_stroke", "#7E2E1F"}, {"grid", ""}, {"hatch", ""},
                        {"panel_fill", ""}, {"panel_stroke", ""}, {"shadow", ""}};
        case EditorialMapStyle::kFloatingIsland:
            return Json{{"background", "#EDE8DE"}, {"accent", "#D94E34"},
                        {"ink", "#1F2937"}, {"muted", "#5B6470"},
                        {"paper", "#EDE8DE"},
                        {"countries_fill", ""}, {"countries_stroke", ""},
                        {"states_fill", ""}, {"states_stroke", ""},
                        {"highlight_fill", "#D94E34"},
                        {"highlight_stroke", "#7E2E1F"}, {"grid", ""}, {"hatch", ""},
                        {"panel_fill", ""}, {"panel_stroke", ""},
                        {"shadow", "#3B2A20"}};
    }
    return Json::object();
}

[[nodiscard]] Json buildScene(EditorialMapStyle style,
                              const EditorialMapSceneOptions& options,
                              const GeoFeatureCollection& states,
                              const GeoFeatureCollection& countries,
                              const GeoFeature* highlight) {
    const double width = options.width;
    const double height = options.height;
    const Design design{width / 1920.0, height / 1080.0,
                        std::min(width / 1920.0, height / 1080.0)};

    LonLatBox lower48;
    for (const GeoFeature& feature : states.features) {
        if (!isContiguousState(feature)) continue;
        const LonLatBox box = boxOf(feature);
        lower48.add(box.lonMin, box.latMin);
        lower48.add(box.lonMax, box.latMax);
    }
    if (!lower48.valid())
        throw std::runtime_error("the state catalog has no contiguous US geometry");

    const std::string id = editorial_map_style_id(style);
    View view;
    if (id == "01_editorial_vox") {
        view = fitBox(lower48, fractionRect(options.width, options.height,
                                            0.035, 0.055, 0.93, 0.815));
    } else if (id == "02_paper_cutout") {
        view = fitBox(lower48, fractionRect(options.width, options.height,
                                            0.05, 0.15, 0.90, 0.78));
    } else if (id == "03_dark_radar") {
        view = fitBox(lower48, fractionRect(options.width, options.height,
                                            0.03, 0.07, 0.94, 0.84));
    } else if (id == "04_blueprint") {
        view = fitBox(lower48, fractionRect(options.width, options.height,
                                            0.05, 0.08, 0.90, 0.70));
    } else if (id == "05_connection_arc") {
        LonLatBox east;
        east.add(-90.4, 36.6);
        east.add(-73.8, 43.0);
        view = fitBox(east, fractionRect(options.width, options.height,
                                         0.03, 0.02, 0.94, 0.96));
    } else if (id == "06_floating_island") {
        view = fitBox(boxOf(*highlight), fractionRect(options.width, options.height,
                                                      0.22, 0.15, 0.38, 0.68));
    } else {
        throw std::runtime_error("unknown map style: " + id);
    }

    const Bounds clipBounds{-kCanvasPadding, -kCanvasPadding,
                            width + kCanvasPadding, height + kCanvasPadding};
    const Bounds canvasBounds{0.0, 0.0, width, height};
    const Rect canvasRect{0.0, 0.0, width, height};
    const double tolerance = std::max(0.1, options.simplify_tolerance);
    const bool needsCountries = id == "01_editorial_vox" || id == "05_connection_arc";
    const bool needsStates = id != "06_floating_island";
    const bool needsHighlight = id == "01_editorial_vox" || id == "02_paper_cutout" ||
                                id == "04_blueprint" || id == "06_floating_island";

    Json highlightPolygons = Json::array();
    if (needsHighlight) {
        highlightPolygons = featureJson(*highlight, view, tolerance, clipBounds)["polygons"];
    }

    const Json palette = paletteFor(style);
    const Point indianapolis = view.project(options.pin_latitude, options.pin_longitude);
    const Point washington = view.project(38.9072, -77.0369);

    Json labels = Json::array();
    Json accents = Json::array();
    Json frames = Json::array();
    Json pins = Json::array();
    Json hatch = Json::array();
    Json graticule = Json::array();
    Json arc = nullptr;
    Json leader = nullptr;
    Json radar = nullptr;
    Json shadow = nullptr;

    if (id == "01_editorial_vox") {
        const std::string micro = "UNITED STATES / EDITORIAL PLATE";
        const double microWidth = estimateTextWidth(micro, design.f(17), 3, "mono");
        labels.push_back(labelJson(micro, design.x(1820) - microWidth,
                                   design.y(1018), "mono", design.f(17), "#33414F",
                                   3, "left", 0.72));
        accents.push_back(segmentJson({design.x(1820) - microWidth, design.y(1002)},
                                      {design.x(1820), design.y(1002)},
                                      "#D94E34", design.f(3)));
        labels.push_back(labelJson(options.title, design.x(98), design.y(946),
                                   "serif_bold", design.f(104), "#1F2937"));
        accents.push_back(segmentJson({design.x(102), design.y(962)},
                                      {design.x(196), design.y(962)},
                                      "#D94E34", design.f(4)));
        labels.push_back(labelJson(options.subtitle, design.x(102), design.y(1002),
                                   "sans_regular", design.f(21), "#556270", 5,
                                   "left", 0.95));
        labels.push_back(labelJson(options.pin_label,
                                   indianapolis.x + design.x(28),
                                   indianapolis.y - design.y(12), "sans_semibold",
                                   design.f(32), "#1F2937", 0, "left", 1.0,
                                   "#F4F1EA", design.f(2)));
        pins.push_back(pinJson(indianapolis.x, indianapolis.y, options.pin_label,
                               indianapolis.x + design.x(28),
                               indianapolis.y - design.y(12), design.f(13),
                               "#111827", "#F4F1EA", "dot"));
    } else if (id == "02_paper_cutout") {
        labels.push_back(labelJson("INDIANA", design.x(100), design.y(148),
                                   "serif_bold", design.f(92), "#2A241C"));
        accents.push_back(segmentJson({design.x(104), design.y(168)},
                                      {design.x(190), design.y(168)},
                                      "#C05A3E", design.f(3)));
        labels.push_back(labelJson("39.77 N / 86.16 W", design.x(104), design.y(204),
                                   "mono", design.f(18), "#6B6255", 2, "left", 0.9));
        labels.push_back(labelJson(options.subtitle, design.x(104), design.y(232),
                                   "sans_regular", design.f(18), "#7A7164", 4,
                                   "left", 0.85));
        labels.push_back(labelJson(options.pin_label,
                                   indianapolis.x + design.x(26),
                                   indianapolis.y - design.y(12), "sans_semibold",
                                   design.f(31), "#2A241C", 0, "left", 1.0,
                                   "#F8F3E7", design.f(2)));
        pins.push_back(pinJson(indianapolis.x, indianapolis.y, options.pin_label,
                               indianapolis.x + design.x(26),
                               indianapolis.y - design.y(12), design.f(13),
                               "#2A241C", "#F8F3E7", "dot"));
        shadow = Json{{"offset", Json::array({design.x(18), design.y(26)})},
                      {"blur", design.f(26)},
                      {"color", "#2B241B"},
                      {"opacity", 0.32},
                      {"scale", 1.035}};
    } else if (id == "03_dark_radar") {
        labels.push_back(labelJson("UNITED STATES // SIGNAL GRID", design.x(92),
                                   design.y(130), "mono", design.f(20), "#8FA3BF",
                                   3));
        labels.push_back(labelJson("38.90 N / 77.03 W", design.x(92), design.y(158),
                                   "mono", design.f(16), "#5E738D", 2, "left", 0.9));
        labels.push_back(labelJson("WASHINGTON, D.C.", design.x(92), design.y(976),
                                   "mono", design.f(24), "#F1F5F9", 4));
        labels.push_back(labelJson("FEDERAL DISTRICT / PRIORITY 01", design.x(92),
                                   design.y(1006), "mono", design.f(16), "#6F849E",
                                   2, "left", 0.9));
        labels.push_back(labelJson("GRID REF 18S UJ 234 083", design.x(92),
                                   design.y(1032), "mono", design.f(14), "#475B72",
                                   2, "left", 0.8));
        accents.push_back(segmentJson({design.x(92), design.y(92)},
                                      {design.x(172), design.y(92)},
                                      "#38BDF8", design.f(3)));
        radar = Json{{"x", round2(washington.x)}, {"y", round2(washington.y)},
                     {"color", "#38BDF8"}, {"max_radius", design.f(174)}};
        graticule = graticuleSegments(view, 5.0, true, canvasRect);
    } else if (id == "04_blueprint") {
        const int inset = static_cast<int>(std::lround(design.f(46)));
        frames.push_back(frameJson(inset, inset, width - inset * 2.0,
                                   height - inset * 2.0, "", "#3D6C93",
                                   std::max(1.0, design.f(1))));
        const Rect panel{width * 0.04, height * 0.745, width * 0.33, height * 0.205};
        frames.push_back(frameJson(panel.x, panel.y, panel.width, panel.height,
                                   "#0A2136", "#3D6C93", 1.0));
        const double textX = panel.x + width * 0.016;
        labels.push_back(labelJson("INDIANA", textX, panel.y + design.y(62),
                                   "sans_semibold", design.f(56), "#E8F3FC", 9));
        accents.push_back(segmentJson({textX, panel.y + design.y(80)},
                                      {panel.x + panel.width - design.x(26),
                                       panel.y + design.y(80)},
                                      "#3D6C93", 1.0));
        labels.push_back(labelJson("39.7684 N / 86.1581 W", textX,
                                   panel.y + design.y(112), "mono", design.f(18),
                                   "#A8C8E8", 1, "left", 0.95));
        labels.push_back(labelJson("ADMINISTRATIVE PLATE / 1:2,000,000", textX,
                                   panel.y + design.y(140), "mono", design.f(15),
                                   "#7FA5C6", 1, "left", 0.9));
        accents.push_back(segmentJson({textX, panel.y + design.y(176)},
                                      {textX + design.x(190), panel.y + design.y(176)},
                                      "#A8C8E8", design.f(2)));
        for (int tick = 0; tick <= 4; ++tick) {
            const double x = textX + tick * design.x(47);
            accents.push_back(segmentJson({x, panel.y + design.y(170)},
                                          {x, panel.y + design.y(184)},
                                          "#A8C8E8", 1.0));
        }
        labels.push_back(labelJson("0", textX, panel.y + design.y(200), "mono",
                                   design.f(13), "#7FA5C6", 0, "left", 0.9));
        labels.push_back(labelJson("100 KM", textX + design.x(146),
                                   panel.y + design.y(200), "mono", design.f(13),
                                   "#7FA5C6", 0, "left", 0.9));
        labels.push_back(labelJson("INDIANAPOLIS",
                                   indianapolis.x + design.x(30),
                                   indianapolis.y - design.y(14), "mono",
                                   design.f(17), "#E3C68E", 2));
        pins.push_back(pinJson(indianapolis.x, indianapolis.y, "INDIANAPOLIS",
                               indianapolis.x + design.x(30),
                               indianapolis.y - design.y(14), design.f(12),
                               "#E3C68E", "#E3C68E", "crosshair"));
        hatch = hatchSegments(highlightPolygons, std::max(6.0, design.f(7.0)),
                              canvasRect);
        graticule = graticuleSegments(view, 3.0, false, canvasRect);
    } else if (id == "05_connection_arc") {
        labels.push_back(labelJson("WASHINGTON - INDIANAPOLIS", design.x(98),
                                   design.y(112), "serif_bold", design.f(46),
                                   "#1F2937"));
        labels.push_back(labelJson("CONNECTION ARC / APPROX. 800 KM", design.x(100),
                                   design.y(146), "mono", design.f(17), "#5B6470",
                                   2, "left", 0.9));
        const double dcWidth = estimateTextWidth("WASHINGTON, D.C.", design.f(27), 0,
                                                 "sans_semibold");
        labels.push_back(labelJson("WASHINGTON, D.C.",
                                   washington.x - design.x(24) - dcWidth,
                                   washington.y + design.y(42), "sans_semibold",
                                   design.f(27), "#1F2937", 0, "left", 1.0,
                                   "#F8F3E7", design.f(2)));
        const std::string indyLabel = "INDIANAPOLIS";
        const double indyWidth = estimateTextWidth(indyLabel, design.f(27), 0,
                                                   "sans_semibold");
        labels.push_back(labelJson(indyLabel,
                                   indianapolis.x - design.x(24) - indyWidth,
                                   indianapolis.y - design.y(22), "sans_semibold",
                                   design.f(27), "#1F2937", 0, "left", 1.0,
                                   "#F8F3E7", design.f(2)));

        const double dx = indianapolis.x - washington.x;
        const double dy = indianapolis.y - washington.y;
        const double distance = std::hypot(dx, dy);
        const double lift = distance * 0.20;
        const Point c1{washington.x + dx * 0.33, washington.y + dy * 0.33 - lift};
        const Point c2{washington.x + dx * 0.66, washington.y + dy * 0.66 - lift};
        const Point midpoint{(washington.x + 3.0 * c1.x + 3.0 * c2.x + indianapolis.x) / 8.0,
                             (washington.y + 3.0 * c1.y + 3.0 * c2.y + indianapolis.y) / 8.0};
        labels.push_back(labelJson("800 KM", midpoint.x,
                                   midpoint.y - design.y(20), "mono", design.f(19),
                                   "#5B6470", 2, "center"));
        arc = Json{{"from", pointJson(washington)}, {"to", pointJson(indianapolis)},
                   {"control1", pointJson(c1)}, {"control2", pointJson(c2)},
                   {"distance_text", "800 KM"},
                   {"distance_x", round2(midpoint.x)},
                   {"distance_y", round2(midpoint.y - design.y(20))}};
        pins.push_back(pinJson(washington.x, washington.y, "WASHINGTON, D.C.",
                               washington.x - design.x(24) - dcWidth,
                               washington.y + design.y(42), design.f(12),
                               "#1F2937", "#F8F3E7", "dot"));
        pins.push_back(pinJson(indianapolis.x, indianapolis.y, "INDIANAPOLIS",
                               indianapolis.x - design.x(24) - indyWidth,
                               indianapolis.y - design.y(22), design.f(14),
                               "#D94E34", "#F8F3E7", "dot"));
    } else if (id == "06_floating_island") {
        const Point anchor = view.project(40.20, -84.82);
        const double targetY = design.y(150);
        const Point elbow{anchor.x + std::max(design.x(40), anchor.y - targetY),
                          targetY};
        const Point end{design.x(1285), targetY};
        leader = Json{{"points", Json::array({pointJson(anchor), pointJson(elbow),
                                              pointJson(end)})},
                      {"anchor_radius", design.f(4)}};
        const double cartoucheX = design.x(1315);
        const double cartoucheTop = design.y(160);
        accents.push_back(segmentJson({cartoucheX - design.x(22),
                                       cartoucheTop + design.y(54)},
                                      {cartoucheX - design.x(22),
                                       cartoucheTop + design.y(10)},
                                      "#D94E34", design.f(4)));
        labels.push_back(labelJson("INDIANA", cartoucheX,
                                   cartoucheTop + design.y(70), "serif_bold",
                                   design.f(74), "#1F2937"));
        accents.push_back(segmentJson({cartoucheX, cartoucheTop + design.y(88)},
                                      {cartoucheX + design.x(340),
                                       cartoucheTop + design.y(88)},
                                      "#B8B2A6", 1.0));
        labels.push_back(labelJson("39.77 N / 86.15 W", cartoucheX,
                                   cartoucheTop + design.y(120), "mono",
                                   design.f(19), "#5B6470", 2));
        labels.push_back(labelJson("THE HOOSIER STATE", cartoucheX,
                                   cartoucheTop + design.y(148), "sans_regular",
                                   design.f(17), "#6B7280", 4, "left", 0.9));
        shadow = Json{{"offset", Json::array({design.x(18), design.y(28)})},
                      {"blur", design.f(30)},
                      {"color", "#3B2A20"},
                      {"opacity", 0.28},
                      {"scale", 1.0}};
    }

    // Roles let the template pack own choreography without guessing from
    // label text or list order.
    auto assignRole = [&labels](std::size_t index, const char* role) {
        if (index < labels.size()) labels[index]["role"] = role;
    };
    if (id == "01_editorial_vox") {
        assignRole(0, "micro");
        assignRole(1, "title");
        assignRole(2, "subtitle");
        assignRole(3, "pin");
    } else if (id == "02_paper_cutout") {
        assignRole(0, "title");
        assignRole(1, "data");
        assignRole(2, "subtitle");
        assignRole(3, "pin");
    } else if (id == "03_dark_radar") {
        assignRole(0, "hud");
        assignRole(1, "data");
        assignRole(2, "hud");
        assignRole(3, "data");
        assignRole(4, "data");
    } else if (id == "04_blueprint") {
        assignRole(0, "title");
        assignRole(1, "data");
        assignRole(2, "data");
        assignRole(3, "data");
        assignRole(4, "data");
        assignRole(5, "pin");
    } else if (id == "05_connection_arc") {
        assignRole(0, "title");
        assignRole(1, "subtitle");
        assignRole(2, "pin");
        assignRole(3, "pin");
        assignRole(4, "data");
    } else if (id == "06_floating_island") {
        assignRole(0, "title");
        assignRole(1, "data");
        assignRole(2, "subtitle");
    }

    Json scene;
    scene["schema"] = editorial_map_scene_schema_id();
    scene["style"] = shortStyleId(style);
    scene["style_id"] = editorial_map_style_id(style);
    scene["canvas"] = Json::array({options.width, options.height});
    scene["fps"] = options.fps;
    scene["frame_count"] = options.frame_count;
    scene["title"] = options.title;
    scene["subtitle"] = options.subtitle;
    scene["palette"] = palette;
    // Keep only contiguous states in every continental composition.
    Json stateFeatures = Json::array();
    if (needsStates) {
        for (const GeoFeature& state : states.features) {
            if (!isContiguousState(state)) continue;
            stateFeatures.push_back(featureJson(state, view, tolerance * 1.2,
                                                clipBounds));
        }
    }
    scene["states"] = std::move(stateFeatures);
    Json countryFeatures = Json::array();
    if (needsCountries) {
        for (const GeoFeature& country : countries.features) {
            const auto admin = country.properties.find("ADMIN");
            if (admin != country.properties.end() &&
                admin->second == "United States of America")
                continue;
            countryFeatures.push_back(featureJson(country, view, tolerance * 1.2,
                                                  clipBounds));
        }
    }
    scene["countries"] = std::move(countryFeatures);
    scene["highlight"] = Json{{"polygons", std::move(highlightPolygons)}};
    scene["hatch_segments"] = std::move(hatch);
    scene["graticule_segments"] = std::move(graticule);
    scene["accent_segments"] = std::move(accents);
    scene["frames"] = std::move(frames);
    scene["pins"] = std::move(pins);
    scene["labels"] = std::move(labels);
    scene["arc"] = std::move(arc);
    scene["leader"] = std::move(leader);
    scene["radar"] = std::move(radar);
    scene["shadow"] = std::move(shadow);
    return scene;
}
}  // namespace chrononplugin::maps::editorial_map_scene_detail
