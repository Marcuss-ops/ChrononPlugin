#pragma once

#include <optional>
#include <string>
#include <vector>

#include "chrononplugin/maps/geo_feature.hpp"

namespace chrononplugin::maps {

/// One historical snapshot. Historical borders are approximations: the
/// provenance and confidence are kept with the data instead of implied exact.
struct HistoricalSnapshot {
    int year{0};
    GeoFeatureCollection collection;
    std::string source;
    std::string confidence;  // free-form tag, e.g. "high", "approximate"
    int valid_from{0};
    int valid_to{0};
};

/// Ordered set of snapshots for one entity. Selection returns the snapshot
/// whose year is nearest to the requested year (ties go to the earlier one).
class HistoricalGeoTimeline {
public:
    /// Adds a snapshot. Throws std::invalid_argument on a duplicate year.
    void add_snapshot(HistoricalSnapshot snapshot);

    [[nodiscard]] std::size_t size() const noexcept { return m_snapshots.size(); }

    /// Snapshots sorted by year.
    [[nodiscard]] const std::vector<HistoricalSnapshot>& snapshots() const noexcept {
        return m_snapshots;
    }

    /// Nearest snapshot to `year`, or nullopt when the timeline is empty.
    [[nodiscard]] std::optional<HistoricalSnapshot> nearest(int year) const;

private:
    std::vector<HistoricalSnapshot> m_snapshots;
};

}  // namespace chrononplugin::maps
