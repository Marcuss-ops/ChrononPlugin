#pragma once

#include <chrononplugin/captions/subtitle_track_builder.hpp>
#include <chronon3d/text/prepared_text.hpp>

namespace chrononplugin::captions {

using namespace chronon3d;

using namespace chronon3d;

// Internal, fully-resolved subtitle snapshot. It deliberately contains no
// parent-layer timing: a layer owns its own lifetime, while this plan owns
// only the active cue's content and layout.
struct SubtitleCueTiming {
    TimeRange range{};

    [[nodiscard]] bool active_at(Frame frame) const noexcept {
        return range.contains(frame);
    }
};

struct SubtitleLayoutSpec {
    Vec2 box_size{};
    TextAlign align{TextAlign::Center};
    VerticalAlign vertical_align{VerticalAlign::Middle};
    TextAnchor anchor{TextAnchor::Center};
    TextPlacement placement{};
    float font_size{48.0f};
};

struct SubtitleWordBindings {
    std::vector<TimedWordBinding> bindings;
    std::vector<GlyphSelectorSpec> selectors;
};

struct SubtitleRenderPlan {
    std::size_t cue_index{0};
    SubtitleCueTiming timing;
    SubtitleLayoutSpec layout;
    SubtitleWordBindings words;
    PreparedText text;
};

} // namespace chrononplugin::captions
