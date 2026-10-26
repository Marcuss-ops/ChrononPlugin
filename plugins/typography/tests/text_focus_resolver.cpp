// TextFocusResolver authority + text_depth_focus_v1 preset family contracts.
//
// Acceptance tests (per the focus-travel goal):
//   - TestFocusedWordHasZeroBlur
//   - TestBlurIncreasesWithDepthDistance
//   - TestFocusTravelContinuous        (no pop at word boundaries)
//   - TestNoBlurPopAtWordBoundary
//   - TestFinalFocusExact
//   - TestRandomAccessDeterministic
//   - TestWordAtFocusDistanceIsSharp   (true DOF path)
//   - TestFartherWordBlurGreater
//   - TestCameraPushUpdatesFocusCorrectly
//   - Bounds/validity: fail-closed on invalid options, static exactness,
//     depth-cascade z assignment, preset family census + canonical shape.

#include <doctest/doctest.h>

#include <chrononplugin/typography/text_focus_resolver.hpp>
#include <chronon3d/text/glyph_selector.hpp>

#include "support/glyph_selector_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace chronon3d;
using namespace chrononplugin::typography;

namespace {

constexpr float kEps = 1e-5f;

} // namespace

TEST_CASE("TextFocusProfile resolves Word, Span, and Line groups without layout authority") {
    TextFocusProfile profile;
    profile.basis = TextGroupBasis::Word;
    profile.focus_position = 1.5f;
    profile.blur_far = 12.0f;
    profile.blur_per_unit = 4.0f;
    profile.opacity_falloff = 0.1f;
    profile.scale_falloff = 0.02f;
    profile.curve = FocusCurve::Linear;
    const auto words = resolve_text_focus(profile, 4);
    REQUIRE(words.size() == 4);
    CHECK(words[1].blur_radius == doctest::Approx(words[2].blur_radius));
    CHECK(words[1].blur_radius == doctest::Approx(2.0f));
    CHECK(words[1].opacity == doctest::Approx(words[2].opacity));
    CHECK(words[1].scale == doctest::Approx(words[2].scale));
    CHECK(words[0].blur_radius > words[1].blur_radius);
    profile.curve = FocusCurve::SmoothStep;
    profile.focus_position = 1.75f;
    const auto smooth = resolve_text_focus(profile, 4);
    CHECK(smooth[1].blur_radius == doctest::Approx(2.755102f));
    CHECK(smooth[2].blur_radius == doctest::Approx(0.387755f));
    CHECK(smooth[0].blur_radius == doctest::Approx(7.0f));
    CHECK(smooth[1].blur_radius > smooth[2].blur_radius);

    profile.basis = TextGroupBasis::Span;
    profile.group_ids = {"title", "number", "suffix"};
    profile.focus_position = 1.0f;
    const auto spans = make_text_focus_animators(profile, 3);
    REQUIRE(spans.size() == 3);
    CHECK(spans[0].selectors.front().unit == TextSelectorUnit::Glyph);
    CHECK(spans[0].selectors.front().semantic_id == std::optional<std::string>{"title"});
    CHECK(spans[1].selectors.front().semantic_id == std::optional<std::string>{"number"});
    CHECK(spans[1].properties.size() == 3);
    CHECK(std::get<BlurProperty>(spans[1].properties[0]).radius.default_value() == doctest::Approx(0.0f));

    profile.basis = TextGroupBasis::Line;
    profile.group_ids.clear();
    const auto lines = make_text_focus_animators(profile, 2);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].selectors.front().unit == TextSelectorUnit::Line);
    CHECK(lines[1].selectors.front().unit == TextSelectorUnit::Line);
    CHECK(lines[0].selectors.front().start.default_value() == doctest::Approx(0.0f));
    CHECK(lines[0].selectors.front().end.default_value() == doctest::Approx(50.0f));
    CHECK(lines[1].selectors.front().start.default_value() == doctest::Approx(50.0f));
    CHECK(lines[1].selectors.front().end.default_value() == doctest::Approx(100.0f));
    CHECK(lines[0].selectors.front().order == TextSelectorOrder::Forward);
    CHECK(lines[0].selectors.front().index_step == 1U);
}

TEST_CASE("TextFocusProfile normalizes falloffs against the farthest group from focus") {
    TextFocusProfile profile;
    profile.focus_position = 1.0f;
    profile.opacity_falloff = 0.2f;
    profile.scale_falloff = 0.1f;
    const auto samples = resolve_text_focus(profile, 5);
    CHECK(samples[0].opacity == doctest::Approx(0.933333f));
    CHECK(samples[4].opacity == doctest::Approx(0.8f));
    CHECK(samples[0].scale == doctest::Approx(0.966667f));
    CHECK(samples[4].scale == doctest::Approx(0.9f));

    profile.focus_position = 0.0f;
    profile.curve = FocusCurve::Quadratic;
    const auto quadratic = resolve_text_focus(profile, 5);
    CHECK(quadratic[1].opacity == doctest::Approx(0.9875f));
    CHECK(quadratic[4].opacity == doctest::Approx(0.8f));
}

TEST_CASE("TextFocusProfile respects disabled falloffs, custom curves, and validation") {
    TextFocusProfile profile;
    profile.focus_position = 1.0f;
    profile.blur_near = 2.0f;
    profile.blur_far = 8.0f;
    profile.blur_per_unit = 3.0f;
    profile.opacity_falloff = 0.0f;
    profile.scale_falloff = 0.0f;
    profile.curve = FocusCurve::Quadratic;
    const auto samples = resolve_text_focus(profile, 3);
    CHECK(samples[1].blur_radius == doctest::Approx(2.0f));
    CHECK(samples[0].blur_radius == doctest::Approx(5.0f));
    CHECK(samples[2].blur_radius == doctest::Approx(5.0f));
    for (const auto& sample : samples) {
        CHECK(sample.opacity == doctest::Approx(1.0f));
        CHECK(sample.scale == doctest::Approx(1.0f));
    }
    profile.basis = TextGroupBasis::Span;
    profile.group_ids = {"duplicate", "duplicate", "third"};
    CHECK_THROWS(resolve_text_focus(profile, 3));
    profile.group_ids = {"only-one"};
    CHECK_THROWS(make_text_focus_animators(profile, 3));
    profile.basis = TextGroupBasis::Word;
    profile.group_ids.clear();
    profile.focus_position = 3.0f;
    CHECK_THROWS(resolve_text_focus(profile, 3));
    profile.focus_position = 0.0f;
    profile.curve = static_cast<FocusCurve>(255);
    CHECK_THROWS(resolve_text_focus(profile, 3));
    profile.curve = FocusCurve::Linear;
    profile.basis = static_cast<TextGroupBasis>(255);
    CHECK_THROWS(resolve_text_focus(profile, 3));
    profile = {};
    profile.focus_position = 1.0f;
    CHECK_THROWS(resolve_text_focus(profile, 0));
    profile = {};
    profile.group_ids = {"not-used"};
    CHECK_THROWS(resolve_text_focus(profile, 2));
}

TEST_CASE("Word, semantic Span, and Line focus selectors target exactly one canonical group") {
    const auto time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});
    TextFocusProfile profile;

    const std::string word_text = "alpha beta gamma";
    const auto word_run = test_glyph_sel::make_run_for_source(word_text);
    const auto word_map = build_text_unit_map(word_run, word_text);
    profile.basis = TextGroupBasis::Word;
    const auto words = make_text_focus_animators(profile, word_map.word_count);
    REQUIRE(words.size() == 3);
    for (std::size_t group = 0; group < words.size(); ++group) {
        const auto& selector = words[group].selectors.front();
        CHECK(selector.unit == TextSelectorUnit::Word);
        CHECK(selector.order == TextSelectorOrder::Forward);
        for (u32 glyph = 0; glyph < word_run.glyphs.size(); ++glyph) {
            const bool expected = word_map.unit_index(glyph, TextSelectorUnit::Word) == group;
            CHECK(evaluate_selector(selector, word_map, glyph, word_text, time, &word_run) ==
                  doctest::Approx(expected ? 1.0f : 0.0f));
        }
    }

    profile.basis = TextGroupBasis::Span;
    profile.group_ids = {"alpha", "beta", "gamma"};
    auto span_map = word_map;
    span_map.glyph_semantic_id.resize(word_run.glyphs.size());
    for (std::size_t glyph = 0; glyph < word_run.glyphs.size(); ++glyph) {
        const auto word = word_map.glyph_to_word[glyph];
        if (word < profile.group_ids.size())
            span_map.glyph_semantic_id[glyph] = profile.group_ids[word];
    }
    const auto spans = make_text_focus_animators(profile, profile.group_ids.size());
    REQUIRE(spans.size() == 3);
    for (std::size_t group = 0; group < spans.size(); ++group) {
        const auto& selector = spans[group].selectors.front();
        CHECK(selector.semantic_id == profile.group_ids[group]);
        for (u32 glyph = 0; glyph < word_run.glyphs.size(); ++glyph) {
            const bool expected = word_map.glyph_to_word[glyph] == group;
            CHECK(evaluate_selector(selector, span_map, glyph, word_text, time, &word_run) ==
                  doctest::Approx(expected ? 1.0f : 0.0f));
        }
    }

    const std::string line_text = "abcdefghijkl";
    auto line_run = test_glyph_sel::make_run_for_source(line_text);
    for (std::size_t glyph = 0; glyph < line_run.glyphs.size(); ++glyph)
        line_run.glyphs[glyph].y = static_cast<float>(glyph / 4) * 16.0f;
    const auto line_map = build_text_unit_map(line_run, line_text);
    REQUIRE(line_map.line_count == 3);
    profile.basis = TextGroupBasis::Line;
    profile.group_ids.clear();
    const auto lines = make_text_focus_animators(profile, line_map.line_count);
    REQUIRE(lines.size() == 3);
    for (std::size_t group = 0; group < lines.size(); ++group) {
        const auto& selector = lines[group].selectors.front();
        CHECK(selector.unit == TextSelectorUnit::Line);
        CHECK(selector.order == TextSelectorOrder::Forward);
        CHECK_FALSE(selector.exclude_spaces);
        for (u32 glyph = 0; glyph < line_run.glyphs.size(); ++glyph) {
            const bool expected = line_map.unit_index(glyph, TextSelectorUnit::Line) == group;
            CHECK(evaluate_selector(selector, line_map, glyph, line_text, time, &line_run) ==
                  doctest::Approx(expected ? 1.0f : 0.0f));
        }
    }
}

TEST_CASE("TestFocusedWordHasZeroBlur — editorial mode") {
    TextFocusOptions options;
    const auto samples = resolve_word_focus(4, TextFocusState{2.0f, 0.0f}, options);
    REQUIRE(samples.size() == 4);
    CHECK(samples[2].blur_radius == doctest::Approx(0.0f).epsilon(kEps));
    // Neighbours carry the canonical 1-word × blur_scale radius.
    CHECK(samples[1].blur_radius == doctest::Approx(options.blur_scale).epsilon(kEps));
    CHECK(samples[3].blur_radius == doctest::Approx(options.blur_scale).epsilon(kEps));
    // The focused word stays fully opaque and at full scale.
    CHECK(samples[2].opacity == doctest::Approx(1.0f).epsilon(kEps));
    CHECK(samples[2].scale == doctest::Approx(1.0f).epsilon(kEps));
}

TEST_CASE("TestBlurIncreasesWithDepthDistance — editorial mode") {
    TextFocusOptions options;
    const auto samples = resolve_word_focus(6, TextFocusState{0.0f, 0.0f}, options);
    for (std::size_t i = 1; i < samples.size(); ++i) {
        INFO("word ", i);
        CHECK(samples[i].blur_radius > samples[i - 1].blur_radius);
    }
    // And the profile clamp holds.
    CHECK(samples.back().blur_radius <= options.profile.max_blur + kEps);
}

TEST_CASE("TestNoBlurPopAtWordBoundary — travel is continuous in focus_position") {
    TextFocusOptions options;
    // Sample the plane of focus mid-travel between words 1 and 2 on a
    // 5-word phrase; no word may jump discontinuously for a small nudge.
    const float mid = 1.5f;
    const float nudged = mid + 0.01f;
    const auto before = resolve_word_focus(5, TextFocusState{mid, 0.5f}, options);
    const auto after = resolve_word_focus(5, TextFocusState{nudged, 0.5f}, options);
    for (std::size_t i = 0; i < before.size(); ++i) {
        INFO("word ", i);
        CHECK(std::fabs(after[i].blur_radius - before[i].blur_radius) <
              options.blur_scale * 0.05f);
        CHECK(std::fabs(after[i].opacity - before[i].opacity) < 0.01f);
    }
    // Symmetry: words 1 and 2 are equidistant from the plane at 1.5.
    CHECK(before[1].blur_radius == doctest::Approx(before[2].blur_radius).epsilon(kEps));
}

TEST_CASE("TestFinalFocusExact — every travel preset lands sharp on its terminal word") {
    FocusPresetOptions preset;
    preset.duration = Frame{48};
    preset.stops = 13;
    const std::size_t words = 4;
    for (const auto kind : {FocusTravelKind::WordTravel, FocusTravelKind::NearToFar,
                            FocusTravelKind::FarToNear, FocusTravelKind::DepthCascade}) {
        INFO("preset ", focus_travel_id(kind));
        const auto animators = make_focus_animators(kind, words, preset);
        const bool cascade = kind != FocusTravelKind::WordTravel;
        const std::size_t depth_animators = cascade ? words : 0u;
        REQUIRE(animators.size() == words + depth_animators);
        // Depth sweeps carry one depth-offset animator per word, followed by
        // the word-focus blur bands. Far-to-near terminates on word 0.
        const std::size_t band_start = depth_animators;
        const std::size_t terminal_word = kind == FocusTravelKind::FarToNear ? 0u : words - 1;
        const auto& terminal_band = animators[band_start + terminal_word];
        std::size_t blur_property = 0;
        for (std::size_t p = 0; p < terminal_band.properties.size(); ++p) {
            if (std::holds_alternative<BlurProperty>(terminal_band.properties[p]))
                blur_property = p;
        }
        const auto& blur = std::get<BlurProperty>(terminal_band.properties[blur_property]);
        REQUIRE(blur.radius.keyframes().size() == std::min(preset.stops, std::size_t{5}));
        CHECK(blur.radius.keyframes().back().value == doctest::Approx(0.0f).epsilon(kEps));
        const std::size_t opposite_word = terminal_word == 0 ? words - 1 : 0;
        const auto& opposite_band = animators[band_start + opposite_word];
        const auto& opposite_blur = std::get<BlurProperty>(opposite_band.properties.front());
        CHECK(opposite_blur.radius.keyframes().back().value > 0.0f);
    }
}

TEST_CASE("focus position and progress reject values outside their normalized domains") {
    CHECK_THROWS(resolve_word_focus(3, TextFocusState{-0.1f, 0.5f}));
    CHECK_THROWS(resolve_word_focus(3, TextFocusState{2.1f, 0.5f}));
    CHECK_THROWS(resolve_word_focus(3, TextFocusState{1.0f, -0.1f}));
    CHECK_THROWS(resolve_word_focus(3, TextFocusState{1.0f, 1.1f}));
}

TEST_CASE("short focus travel keeps every baked keyframe frame unique") {
    FocusPresetOptions preset;
    preset.duration = Frame{2};
    preset.stops = 5;
    const auto animators = make_focus_animators(FocusTravelKind::WordTravel, 3, preset);
    REQUIRE(animators.size() == 3);
    for (const auto& animator : animators) {
        const auto& blur = std::get<BlurProperty>(animator.properties.front());
        REQUIRE(blur.radius.keyframes().size() == 3);
        CHECK(blur.radius.keyframes()[0].frame == Frame{0});
        CHECK(blur.radius.keyframes()[1].frame == Frame{1});
        CHECK(blur.radius.keyframes()[2].frame == Frame{2});
    }
}

TEST_CASE("true-depth preset focus minimizes blur at every baked focus-plane stop") {
    FocusPresetOptions preset;
    preset.stops = 5;
    const std::size_t words = 4;
    constexpr float depth_spacing = 80.0f;
    for (const auto kind : {FocusTravelKind::NearToFar, FocusTravelKind::FarToNear,
                            FocusTravelKind::DepthCascade}) {
        INFO("preset ", focus_travel_id(kind));
        const auto animators = make_focus_animators(kind, words, preset);
        REQUIRE(animators.size() == words * 2);
        const std::size_t band_start = words;
        for (std::size_t stop = 0; stop < 5; ++stop) {
            const float progress = static_cast<float>(stop) / 4.0f;
            const float focus_index = kind == FocusTravelKind::FarToNear
                ? static_cast<float>(words - 1) * (1.0f - progress)
                : static_cast<float>(words - 1) * progress;
            float minimum_blur = std::numeric_limits<float>::infinity();
            for (std::size_t word = 0; word < words; ++word) {
                const auto& band = animators[band_start + word];
                const auto& blur = std::get<BlurProperty>(band.properties.front());
                minimum_blur = std::min(minimum_blur, blur.radius.keyframes()[stop].value);
            }
            const float nearest_word_distance = std::min(
                focus_index - std::floor(focus_index),
                std::ceil(focus_index) - focus_index);
            const float expected_minimum_blur = nearest_word_distance * depth_spacing *
                preset.focus.profile.blur_per_depth;
            CHECK(minimum_blur == doctest::Approx(expected_minimum_blur).epsilon(kEps));
            // The sweep endpoints land exactly on words; intermediate focus
            // planes can sit between words, so neither word is fully sharp.
            if (stop == 0 || stop == 4)
                CHECK(minimum_blur == doctest::Approx(0.0f).epsilon(kEps));
        }
    }
}

TEST_CASE("depth focus cascade lowers a distinct selected Z offset per word") {
    FocusPresetOptions preset;
    preset.depth_cascade = true;
    preset.focus.profile.depth_step = 60.0f;
    preset.focus.profile.max_depth = 180.0f;
    const auto animators = make_focus_animators(FocusTravelKind::DepthCascade, 4, preset);
    REQUIRE(animators.size() == 8);
    for (std::size_t i = 0; i < 4; ++i) {
        INFO("depth animator ", i);
        const auto& depth = animators[i];
        CHECK(depth.id.find("_depth_word_" + std::to_string(i)) != std::string::npos);
        REQUIRE(depth.selectors.size() == 1);
        CHECK(depth.selectors.front().unit == TextSelectorUnit::Word);
        REQUIRE(depth.properties.size() == 1);
        REQUIRE(std::holds_alternative<PositionProperty>(depth.properties.front()));
        const auto& position = std::get<PositionProperty>(depth.properties.front());
        CHECK(position.value.default_value().z == doctest::Approx(-60.0f * static_cast<float>(i)));
        CHECK(depth.is_valid());
    }
    for (std::size_t i = 4; i < animators.size(); ++i)
        CHECK(animators[i].id.find("_band_") != std::string::npos);
}

TEST_CASE("camera and blur parameters fail closed when non-finite or unbounded") {
    FocusPresetOptions preset;
    preset.camera_world_z = std::numeric_limits<float>::infinity();
    CHECK_THROWS(make_focus_animators(FocusTravelKind::DepthCascade, 3, preset));
    preset.camera_world_z = 0.0f;
    preset.focus.profile.max_blur = 257.0f;
    CHECK_THROWS(make_focus_animators(FocusTravelKind::WordTravel, 3, preset));
}

TEST_CASE("TestRandomAccessDeterministic — same inputs, same samples, any order") {
    TextFocusOptions options;
    const auto reference = resolve_word_focus(5, TextFocusState{2.5f, 0.5f}, options);
    for (int iteration = 0; iteration < 8; ++iteration) {
        const auto again = resolve_word_focus(5, TextFocusState{2.5f, 0.5f}, options);
        for (std::size_t i = 0; i < reference.size(); ++i) {
            CHECK(again[i].blur_radius == doctest::Approx(reference[i].blur_radius));
            CHECK(again[i].opacity == doctest::Approx(reference[i].opacity));
            CHECK(again[i].scale == doctest::Approx(reference[i].scale));
        }
    }
    // Preset baking is deterministic too.
    FocusPresetOptions preset;
    const auto first = make_focus_animators(FocusTravelKind::WordTravel, 4, preset);
    const auto second = make_focus_animators(FocusTravelKind::WordTravel, 4, preset);
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].id == second[i].id);
        CHECK(first[i].properties.size() == second[i].properties.size());
    }
}

TEST_CASE("TestWordAtFocusDistanceIsSharp — true depth mode") {
    const auto depths = resolve_word_depths(4, TextDepthProfile{80.0f, 640.0f});
    REQUIRE(depths.size() == 4);
    // Word 1 sits at z=-80; focus the camera exactly on it.
    const auto samples = resolve_word_depth_focus(depths, /*camera_world_z=*/0.0f,
                                                  /*focus_distance=*/80.0f,
                                                  /*aperture_scale=*/0.15f);
    REQUIRE(samples.size() == 4);
    CHECK(samples[1].blur_radius == doctest::Approx(0.0f).epsilon(kEps));
    // Word 0 sits exactly at the camera; its distance error is 80 → blurred.
    CHECK(samples[0].blur_radius > 0.0f);
}

TEST_CASE("TestFartherWordBlurGreater — true depth mode") {
    const auto depths = resolve_word_depths(5);
    const auto samples = resolve_word_depth_focus(depths, 0.0f, /*focus_distance=*/0.0f,
                                                  /*aperture_scale=*/0.2f);
    // Focus at the camera plane: every deeper word is blurrier than the one
    // before it until the profile clamp.
    for (std::size_t i = 1; i < samples.size(); ++i) {
        INFO("word ", i, " depth ", depths[i]);
        CHECK(samples[i].blur_radius >= samples[i - 1].blur_radius);
    }
    // Monotonicity is strict before the clamp engages.
    CHECK(samples[1].blur_radius > samples[0].blur_radius);
    // And every sample respects the max blur clamp.
    for (const auto& sample : samples)
        CHECK(sample.blur_radius <= 14.0f + kEps);
}

TEST_CASE("TestCameraPushUpdatesFocusCorrectly — true depth mode") {
    const auto depths = resolve_word_depths(4);
    const float aperture_scale = 0.15f;
    // Word i sits at z = -i * 80. Camera at z=0 with focus 240 focuses word 3
    // (|0-(-240)| = 240). Pushing the camera to z=-240 with the same focus
    // distance re-focuses word 0 (|−240−0| = 240) while word 3 goes soft:
    // | |−240−(−240)| − 240 | = 240.
    const auto before = resolve_word_depth_focus(depths, 0.0f, 240.0f, aperture_scale);
    const auto after = resolve_word_depth_focus(depths, -240.0f, 240.0f, aperture_scale);
    CHECK(before[3].blur_radius == doctest::Approx(0.0f).epsilon(kEps));
    CHECK(before[0].blur_radius > 0.0f);
    CHECK(after[0].blur_radius == doctest::Approx(0.0f).epsilon(kEps));
    CHECK(after[3].blur_radius > 0.0f);
    // Mid-push: camera at z=-120; the two mid words are both 40 units away,
    // so a 40-unit focus plane lands sharp on both.
    const auto mid = resolve_word_depth_focus(depths, -120.0f, 40.0f, aperture_scale);
    CHECK(mid[1].blur_radius == doctest::Approx(0.0f).epsilon(kEps));
    CHECK(mid[2].blur_radius == doctest::Approx(0.0f).epsilon(kEps));
    // A wrong focus distance blurs every word again.
    const auto missed = resolve_word_depth_focus(depths, -240.0f, 100.0f, aperture_scale);
    CHECK(missed[0].blur_radius > 0.0f);
    CHECK(missed[3].blur_radius > 0.0f);
}

TEST_CASE("depth cascade assigns distinct bounded world Z per word") {
    TextDepthProfile profile{60.0f, 180.0f};
    const auto depths = resolve_word_depths(5, profile);
    REQUIRE(depths.size() == 5);
    CHECK(depths[0] == doctest::Approx(0.0f));
    CHECK(depths[1] == doctest::Approx(-60.0f));
    CHECK(depths[2] == doctest::Approx(-120.0f));
    CHECK(depths[3] == doctest::Approx(-180.0f));
    // Clamped at max_depth instead of escaping the corridor.
    CHECK(depths[4] == doctest::Approx(-180.0f));
}

TEST_CASE("static focus is exact and band-structured") {
    const auto animators = make_static_focus_animators(3, /*focused_word=*/1);
    REQUIRE(animators.size() == 3);
    // The focused band's blur curve is zero everywhere.
    for (std::size_t i = 0; i < animators.size(); ++i) {
        std::optional<std::size_t> blur_index;
        for (std::size_t p = 0; p < animators[i].properties.size(); ++p)
            if (std::holds_alternative<BlurProperty>(animators[i].properties[p]))
                blur_index = p;
        REQUIRE(blur_index.has_value());
        const auto& blur = std::get<BlurProperty>(animators[i].properties[*blur_index]);
        const float expected = i == 1 ? 0.0f : 3.0f;
        for (const auto& keyframe : blur.radius.keyframes())
            CHECK(keyframe.value == doctest::Approx(expected).epsilon(kEps));
    }
}

TEST_CASE("preset family census — 8 stable serialized ids") {
    const std::vector<std::string_view> expected{
        "text_focus_word_static",  "text_focus_word_travel",
        "text_focus_near_to_far",  "text_focus_far_to_near",
        "text_focus_center_out",   "text_focus_edges_in",
        "text_focus_rack_duo",     "text_focus_depth_cascade"};
    for (std::size_t k = 0; k < expected.size(); ++k) {
        INFO("kind index ", k);
        CHECK(focus_travel_id(static_cast<FocusTravelKind>(k)) == expected[k]);
    }
}

TEST_CASE("preset bands keep the canonical V1 animator shape") {
    FocusPresetOptions preset;
    preset.stops = 6;
    const auto animators = make_focus_animators(FocusTravelKind::WordTravel, 4, preset);
    REQUIRE(animators.size() == 4);
    for (const auto& band : animators) {
        CHECK(band.is_valid());
        CHECK(band.selectors.size() == 1);
        CHECK(band.selectors.front().unit == TextSelectorUnit::Word);
        CHECK(band.properties.size() == 3);
        CHECK(std::holds_alternative<BlurProperty>(band.properties[0]));
        CHECK(std::holds_alternative<OpacityProperty>(band.properties[1]));
        CHECK(std::holds_alternative<ScaleProperty>(band.properties[2]));
        // V1 budget: at most five keyframes per property.
        for (const auto& property : band.properties) {
            std::visit([](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (requires { value.value.keyframes(); }) {
                    CHECK(value.value.keyframes().size() <= 5);
                } else if constexpr (requires { value.radius.keyframes(); }) {
                    CHECK(value.radius.keyframes().size() <= 5);
                }
            }, property);
        }
    }
}

TEST_CASE("rack duo alternates focus between the two anchor words") {
    FocusPresetOptions preset;
    preset.rack_a = 0;
    preset.rack_b = 3;
    preset.duration = Frame{60};
    preset.stops = 13;
    const auto animators = make_focus_animators(FocusTravelKind::RackDuo, 4, preset);
    REQUIRE(animators.size() == 4);
    auto blur_curve = [](const TextAnimatorSpec& band) -> const AnimatedValue<f32>& {
        for (const auto& property : band.properties)
            if (std::holds_alternative<BlurProperty>(property))
                return std::get<BlurProperty>(property).radius;
        throw std::logic_error("band has no blur property");
    };
    const auto& band0 = blur_curve(animators[0]);
    const auto& band3 = blur_curve(animators[3]);
    // Start: word A sharp, word B blurred. Middle: B sharp. End: back to A.
    CHECK(band0.keyframes().front().value == doctest::Approx(0.0f).epsilon(kEps));
    CHECK(band3.keyframes().front().value > 0.0f);
    const auto middle = band0.keyframes().size() / 2;
    CHECK(band0.keyframes()[middle].value > 0.0f);
    CHECK(band3.keyframes()[middle].value == doctest::Approx(0.0f).epsilon(kEps));
    CHECK(band0.keyframes().back().value == doctest::Approx(0.0f).epsilon(kEps));
    CHECK(band3.keyframes().back().value > 0.0f);
}

TEST_CASE("center-out and edges-in animate their symmetric focus basins") {
    FocusPresetOptions preset;
    preset.duration = Frame{24};
    preset.stops = 5;
    const auto center = make_focus_animators(FocusTravelKind::CenterOut, 5, preset);
    REQUIRE(center.size() == 5);
    auto blur_curve = [](const TextAnimatorSpec& band) -> const AnimatedValue<f32>& {
        for (const auto& property : band.properties)
            if (std::holds_alternative<BlurProperty>(property))
                return std::get<BlurProperty>(property).radius;
        throw std::logic_error("band has no blur property");
    };
    // The middle word is sharp at every center-out stop; symmetric neighbors
    // become progressively sharp as the focus basin expands.
    const auto center_curve = blur_curve(center[2]).keyframes();
    REQUIRE(center_curve.size() == 5);
    for (const auto& keyframe : center_curve)
        CHECK(keyframe.value == doctest::Approx(0.0f).epsilon(kEps));
    CHECK(blur_curve(center[1]).keyframes().front().value > 0.0f);
    CHECK(blur_curve(center[1]).keyframes().back().value == doctest::Approx(0.0f).epsilon(kEps));
    CHECK(blur_curve(center[0]).keyframes().back().value == doctest::Approx(0.0f).epsilon(kEps));
    CHECK(blur_curve(center[0]).keyframes().front().value ==
          doctest::Approx(blur_curve(center[4]).keyframes().front().value).epsilon(kEps));

    // Two edge focus planes start sharp at both ends and converge to the
    // center. The center starts blurred and is the only sharp word at finish.
    const auto edges = make_focus_animators(FocusTravelKind::EdgesIn, 5, preset);
    REQUIRE(edges.size() == 5);
    CHECK(blur_curve(edges[0]).keyframes().front().value == doctest::Approx(0.0f).epsilon(kEps));
    CHECK(blur_curve(edges[4]).keyframes().front().value == doctest::Approx(0.0f).epsilon(kEps));
    CHECK(blur_curve(edges[0]).keyframes().back().value > 0.0f);
    CHECK(blur_curve(edges[4]).keyframes().back().value > 0.0f);
    CHECK(blur_curve(edges[2]).keyframes().front().value > 0.0f);
    CHECK(blur_curve(edges[2]).keyframes().back().value == doctest::Approx(0.0f).epsilon(kEps));
    for (std::size_t i = 0; i < 5; ++i)
        CHECK(blur_curve(edges[i]).keyframes().front().value ==
              doctest::Approx(blur_curve(edges[4 - i]).keyframes().front().value).epsilon(kEps));
}

TEST_CASE("invalid options fail closed") {
    TextFocusOptions options;
    options.blur_scale = -1.0f;
    CHECK_THROWS(resolve_word_focus(3, TextFocusState{}, options));
    CHECK_THROWS(resolve_word_depths(3, TextDepthProfile{-1.0f, 100.0f}));
    CHECK_THROWS(resolve_word_depth_focus({0.0f, -80.0f}, 0.0f, 80.0f, -0.1f));
    FocusPresetOptions bad_timing;
    bad_timing.duration = Frame{0};
    CHECK_THROWS(make_focus_animators(FocusTravelKind::WordTravel, 3, bad_timing));
    CHECK(make_focus_animators(FocusTravelKind::WordTravel, 0).empty());
}
