// tests/text/test_subtitle_font_ref.cpp
// ═══════════════════════════════════════════════════════════════════════════
// Locks the explicit-font-reference contract of SubtitleTrackBuilder:
//
//   * The builder declares NO implicit engine default font. The legacy
//     `"assets/fonts/Poppins-Bold.ttf"` default was a repository-relative
//     dependency and is removed from the core.
//   * `font(assets::FontRef, size)` stores the canonical reference and the
//     emitted run carries `ref.path()` (resolved later through the
//     per-runtime AssetResolver).
//   * The string `font(path, size)` overload is backward-compatible and
//     wraps into the same canonical `assets::FontRef` slot.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/assets/asset_ref.hpp>
#include <chrononplugin/captions/subtitle_track_builder.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chrononplugin/typography/subtitle.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

#include "support/layer_builder_inspection.hpp"

using namespace chronon3d;
using namespace chrononplugin::typography;

namespace {

typography::SubtitleTrack make_track() {
    SubtitleTrack track;
    SubtitleCue cue;
    cue.start_s = 1.0f;
    cue.end_s = 3.0f;
    cue.text = "Hello";
    track.cues.push_back(cue);
    return track;
}

// Build at frame 45 (30 fps): the cue [1.0s, 3.0s] = frames [30, 90) is
// active, so build() commits a run.
LayerBuilder make_builder() {
    LayerBuilder lb{"subtitle_font_test",
                    SampleTime::from_frame_int(Frame{45}, FrameRate{30, 1})};
    lb.screen_dimensions(1920.0f, 1080.0f);
    return lb;
}

} // namespace

TEST_CASE("SubtitleTrackBuilder default declares no implicit font asset") {
    LayerBuilder lb = make_builder();
    const auto track = make_track();
    CanvasInfo canvas = CanvasInfo::with_safe_area(1920.0f, 1080.0f, SafeAreaPreset{});
    chrononplugin::captions::SubtitleTrackBuilder builder{lb, canvas, track, Frame{0}};

    CHECK_NOTHROW(builder.build());

    const auto runs =
        chronon3d::builders::testing::LayerBuilderInspector::pending_runs(lb);
    REQUIRE(runs.size() == 1);
    // No implicit engine default: the emitted run carries an empty font path
    // and resolution is delegated to the canonical font-engine wiring.
    CHECK(runs[0].text.style.font.font_path.empty());
}

TEST_CASE("SubtitleTrackBuilder font(FontRef, size) sets the explicit reference") {
    LayerBuilder lb = make_builder();
    const auto track = make_track();
    CanvasInfo canvas = CanvasInfo::with_safe_area(1920.0f, 1080.0f, SafeAreaPreset{});
    chrononplugin::captions::SubtitleTrackBuilder builder{lb, canvas, track, Frame{0}};

    CHECK_NOTHROW(builder
                      .font(assets::FontRef{"fonts/Subtitle.ttf", "subtitle.bold"},
                            42.0f)
                      .build());

    const auto runs =
        chronon3d::builders::testing::LayerBuilderInspector::pending_runs(lb);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].text.style.font.font_path == "fonts/Subtitle.ttf");
    CHECK(runs[0].text.style.font.font_size == doctest::Approx(42.0f));
}

TEST_CASE("SubtitleTrackBuilder string font() wraps into the canonical ref") {
    LayerBuilder lb = make_builder();
    const auto track = make_track();
    CanvasInfo canvas = CanvasInfo::with_safe_area(1920.0f, 1080.0f, SafeAreaPreset{});
    chrononplugin::captions::SubtitleTrackBuilder builder{lb, canvas, track, Frame{0}};

    CHECK_NOTHROW(builder.font("fonts/Other.ttf", 32.0f).build());

    const auto runs =
        chronon3d::builders::testing::LayerBuilderInspector::pending_runs(lb);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].text.style.font.font_path == "fonts/Other.ttf");
    CHECK(runs[0].text.style.font.font_size == doctest::Approx(32.0f));
}
