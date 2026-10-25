#include "chrononplugin/maps/historical_timeline.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace chrononplugin::maps {

void HistoricalGeoTimeline::add_snapshot(HistoricalSnapshot snapshot) {
    const auto pos = std::lower_bound(
        m_snapshots.begin(), m_snapshots.end(), snapshot.year,
        [](const HistoricalSnapshot& s, int year) { return s.year < year; });
    if (pos != m_snapshots.end() && pos->year == snapshot.year) {
        throw std::invalid_argument("historical timeline: duplicate snapshot year");
    }
    m_snapshots.insert(pos, std::move(snapshot));
}

std::optional<HistoricalSnapshot> HistoricalGeoTimeline::nearest(int year) const {
    if (m_snapshots.empty()) {
        return std::nullopt;
    }
    const HistoricalSnapshot* best = &m_snapshots.front();
    for (const auto& s : m_snapshots) {
        // Strict '<' keeps the earlier snapshot on a tie, since snapshots are sorted.
        if (std::abs(s.year - year) < std::abs(best->year - year)) {
            best = &s;
        }
    }
    return *best;
}

}  // namespace chrononplugin::maps
