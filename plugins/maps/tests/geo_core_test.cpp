// Standalone test for the headless geo core (no test framework dependency).
#include "chrononplugin/maps/feature_query.hpp"
#include "chrononplugin/maps/geo_boolean.hpp"
#include "chrononplugin/maps/territory_change.hpp"
#include <cmath>
#include "chrononplugin/maps/geojson.hpp"
#include "chrononplugin/maps/historical_timeline.hpp"

#include <cstdio>
#include <stdexcept>
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

constexpr const char* kCollection = R"({
  "type": "FeatureCollection",
  "features": [
    {"type": "Feature", "id": "f1",
     "properties": {"name": "Ottoman Empire", "year": 1700, "active": true},
     "geometry": {"type": "Polygon",
       "coordinates": [[[0,0],[10,0],[10,10],[0,10],[0,0]]]}},
    {"type": "Feature", "properties": {"name": "Venice"},
     "geometry": {"type": "MultiPolygon",
       "coordinates": [
         [[[20,0],[21,0],[21,1],[20,0]]],
         [[[30,0],[31,0],[31,1],[30,0]], [[30.2,0.2],[30.4,0.2],[30.4,0.4],[30.2,0.2]]]
       ]}}
  ]
})";

void test_parse_collection() {
    const auto c = chrononplugin::maps::parse_geojson(kCollection);
    CHECK(c.features.size() == 2);
    CHECK(c.features[0].id == "f1");
    CHECK(c.features[0].properties.at("name") == "Ottoman Empire");
    CHECK(c.features[0].properties.at("year") == "1700");
    CHECK(c.features[0].properties.at("active") == "true");
    CHECK(c.features[0].polygons.size() == 1);
    CHECK(c.features[0].polygons[0].rings.size() == 1);
    CHECK(c.features[1].id.empty());
    CHECK(c.features[1].polygons.size() == 2);
    // The second polygon of the MultiPolygon has a hole.
    CHECK(c.features[1].polygons[1].rings.size() == 2);
    CHECK(c.features[1].polygons[1].rings[0][1][0] == 31.0);
}

void test_parse_single_feature_and_null_geometry() {
    const auto single = chrononplugin::maps::parse_geojson(
        R"({"type":"Feature","properties":{},"geometry":{"type":"Polygon",
           "coordinates":[[[0,0],[1,0],[1,1],[0,0]]]}})");
    CHECK(single.features.size() == 1);
    const auto empty = chrononplugin::maps::parse_geojson(
        R"({"type":"Feature","properties":{"n":"x"},"geometry":null})");
    CHECK(empty.features.size() == 1);
    CHECK(empty.features[0].polygons.empty());
}

bool throws(const char* text) {
    try {
        (void)chrononplugin::maps::parse_geojson(text);
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

void test_rejects_invalid_input() {
    CHECK(throws("{not json"));
    CHECK(throws(R"({"type":"Point","coordinates":[0,0]})"));
    CHECK(throws(R"({"type":"FeatureCollection","features":[{"type":"Feature",
        "properties":{},"geometry":{"type":"LineString","coordinates":[[0,0],[1,1]]}}]})"));
    // Open ring: first and last positions differ.
    CHECK(throws(R"({"type":"Feature","properties":{},"geometry":{"type":"Polygon",
        "coordinates":[[[0,0],[1,0],[1,1],[0,1]]]}})"));
    // Ring with fewer than 4 positions.
    CHECK(throws(R"({"type":"Feature","properties":{},"geometry":{"type":"Polygon",
        "coordinates":[[[0,0],[1,0],[0,0]]]}})"));
}

chrononplugin::maps::HistoricalSnapshot snap(int year) {
    chrononplugin::maps::HistoricalSnapshot s;
    s.year = year;
    s.source = "test";
    s.confidence = "approximate";
    s.valid_from = year;
    s.valid_to = year + 50;
    return s;
}

void test_timeline_nearest() {
    chrononplugin::maps::HistoricalGeoTimeline t;
    CHECK(!t.nearest(1700).has_value());
    t.add_snapshot(snap(1700));
    t.add_snapshot(snap(1453));
    t.add_snapshot(snap(1800));
    CHECK(t.size() == 3);
    CHECK(t.snapshots()[0].year == 1453);  // kept sorted
    CHECK(t.nearest(1700)->year == 1700);
    CHECK(t.nearest(1650)->year == 1700);  // 50 from 1700, 197 from 1453
    CHECK(t.nearest(1500)->year == 1453);
    CHECK(t.nearest(1600)->year == 1700);  // 100 from 1700, 147 from 1453
    CHECK(t.nearest(1000)->year == 1453);  // before the first snapshot
    CHECK(t.nearest(3000)->year == 1800);  // after the last snapshot
    CHECK(t.nearest(1700)->source == "test");
    CHECK(t.nearest(1700)->confidence == "approximate");
    CHECK(t.nearest(1700)->valid_to == 1750);
}

void test_timeline_tie_prefers_earlier() {
    chrononplugin::maps::HistoricalGeoTimeline t;
    t.add_snapshot(snap(1500));
    t.add_snapshot(snap(1600));
    CHECK(t.nearest(1550)->year == 1500);
}

void test_timeline_rejects_duplicate_year() {
    chrononplugin::maps::HistoricalGeoTimeline t;
    t.add_snapshot(snap(1700));
    bool threw = false;
    try {
        t.add_snapshot(snap(1700));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(t.size() == 1);
}

}  // namespace

void test_query_by_property() {
    const auto c = chrononplugin::maps::parse_geojson(kCollection);
    const auto hit = chrononplugin::maps::query_by_property(c, "name", "Ottoman Empire");
    CHECK(hit.size() == 1);
    CHECK(hit[0] == &c.features[0]);
    CHECK(chrononplugin::maps::query_by_property(c, "name", "Venice").size() == 1);
    CHECK(chrononplugin::maps::query_by_property(c, "name", "Nowhere").empty());
    CHECK(chrononplugin::maps::query_by_property(c, "missing", "x").empty());
    CHECK(chrononplugin::maps::query_by_property(c, "year", "1700").size() == 1);
}

using chrononplugin::maps::GeoBooleanOp;
using chrononplugin::maps::GeoPolygon;

GeoPolygon square(double x0, double y0, double x1, double y1) {
    GeoPolygon p;
    p.rings.push_back({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, {x0, y0}});
    return p;
}

double ring_area(const std::vector<chrononplugin::maps::GeoPosition>& ring) {
    double s = 0.0;
    for (std::size_t i = 0; i + 1 < ring.size(); ++i) {
        s += ring[i][0] * ring[i + 1][1] - ring[i + 1][0] * ring[i][1];
    }
    return std::abs(s) * 0.5;
}

// Area with holes subtracted, summed over all polygons.
double area(const std::vector<GeoPolygon>& polys) {
    double total = 0.0;
    for (const auto& p : polys) {
        for (std::size_t r = 0; r < p.rings.size(); ++r) {
            total += (r == 0 ? 1.0 : -1.0) * ring_area(p.rings[r]);
        }
    }
    return total;
}

void test_boolean_ops() {
    const auto a = std::vector<GeoPolygon>{square(0, 0, 2, 2)};
    const auto b = std::vector<GeoPolygon>{square(1, 1, 3, 3)};
    CHECK(std::abs(area(chrononplugin::maps::geo_boolean(GeoBooleanOp::Union, a, b)) - 7.0) < 1e-3);
    CHECK(std::abs(area(chrononplugin::maps::geo_boolean(GeoBooleanOp::Intersection, a, b)) - 1.0) < 1e-3);
    CHECK(std::abs(area(chrononplugin::maps::geo_boolean(GeoBooleanOp::Xor, a, b)) - 6.0) < 1e-3);
    CHECK(std::abs(area(chrononplugin::maps::geo_boolean(GeoBooleanOp::Difference, a, b)) - 3.0) < 1e-3);
}

void test_difference_keeps_hole() {
    const auto big = std::vector<GeoPolygon>{square(0, 0, 4, 4)};
    const auto cut = std::vector<GeoPolygon>{square(1, 1, 3, 3)};
    const auto out = chrononplugin::maps::geo_boolean(GeoBooleanOp::Difference, big, cut);
    CHECK(out.size() == 1);
    CHECK(out[0].rings.size() == 2);  // exterior + one hole
    CHECK(std::abs(area(out) - 12.0) < 1e-3);
}

void test_dissolve_removes_shared_edges() {
    // Four unit squares forming a 2x2 block with shared internal edges.
    const std::vector<GeoPolygon> blocks{
        square(0, 0, 1, 1), square(1, 0, 2, 1), square(0, 1, 1, 2), square(1, 1, 2, 2)};
    const auto out = chrononplugin::maps::geo_dissolve(blocks);
    CHECK(out.size() == 1);
    CHECK(out[0].rings.size() == 1);  // no hole, no internal edges
    CHECK(std::abs(area(out) - 4.0) < 1e-3);
}

void test_dissolve_disjoint_stays_separate() {
    const std::vector<GeoPolygon> parts{square(0, 0, 1, 1), square(5, 5, 6, 6)};
    const auto out = chrononplugin::maps::geo_dissolve(parts);
    CHECK(out.size() == 2);
    CHECK(std::abs(area(out) - 2.0) < 1e-3);
}

void test_territory_change() {
    // before: [0,2]^2 ; after: [1,3]^2 -> shared 1, gained 3, lost 3.
    const auto before = std::vector<GeoPolygon>{square(0, 0, 2, 2)};
    const auto after = std::vector<GeoPolygon>{square(1, 1, 3, 3)};
    const auto c = chrononplugin::maps::compute_territory_change(before, after);
    CHECK(std::abs(area(c.shared) - 1.0) < 1e-3);
    CHECK(std::abs(area(c.gained) - 3.0) < 1e-3);
    CHECK(std::abs(area(c.lost) - 3.0) < 1e-3);
}

int main() {
    test_territory_change();
    test_boolean_ops();
    test_difference_keeps_hole();
    test_dissolve_removes_shared_edges();
    test_dissolve_disjoint_stays_separate();
    test_parse_collection();
    test_parse_single_feature_and_null_geometry();
    test_rejects_invalid_input();
    test_timeline_nearest();
    test_timeline_tie_prefers_earlier();
    test_timeline_rejects_duplicate_year();
    test_query_by_property();
    if (g_failures == 0) {
        std::printf("geo_core_test: PASS\n");
        return 0;
    }
    std::printf("geo_core_test: %d FAILURE(S)\n", g_failures);
    return 1;
}
