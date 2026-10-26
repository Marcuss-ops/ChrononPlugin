#include <doctest/doctest.h>

#include <chrononplugin/typography/text_motion.hpp>

using namespace chronon3d;

TEST_CASE("text motion preset catalog builds reusable entrance and exit animators") {
    typography::MotionPresetOptions options;
    options.start = Frame{12};
    options.duration = Frame{24};

    const auto fade = typography::make_motion_preset(typography::MotionPreset::Fade, options);
    REQUIRE(fade.selectors.size() == 1);
    REQUIRE(fade.properties.size() == 1);
    CHECK(fade.is_valid());
    CHECK(fade.id == "text.fade_in");
    CHECK(fade.selectors[0].amount.evaluate(12.0) == doctest::Approx(100.0f));
    CHECK(fade.selectors[0].amount.evaluate(36.0) == doctest::Approx(0.0f));
    CHECK(std::holds_alternative<OpacityProperty>(fade.properties[0]));
    auto exit_options = options;
    exit_options.entering = false;
    const auto fade_out = typography::make_motion_preset(
        typography::MotionPreset::Fade, exit_options);
    CHECK(fade_out.selectors[0].amount.evaluate(12.0) == doctest::Approx(0.0f));
    CHECK(fade_out.selectors[0].amount.evaluate(36.0) == doctest::Approx(100.0f));

    const auto rise = typography::make_motion_preset(typography::MotionPreset::Rise, options);
    REQUIRE(std::holds_alternative<PositionProperty>(rise.properties[0]));
    CHECK(std::get<PositionProperty>(rise.properties[0]).value.evaluate(12.0).y ==
          doctest::Approx(-48.0f));

    const auto drop = typography::make_motion_preset(typography::MotionPreset::Drop, options);
    CHECK(std::get<PositionProperty>(drop.properties[0]).value.evaluate(12.0).y ==
          doctest::Approx(48.0f));
    const auto slide_left = typography::make_motion_preset(
        typography::MotionPreset::SlideLeft, options);
    CHECK(std::get<PositionProperty>(slide_left.properties[0]).value.evaluate(12.0).x ==
          doctest::Approx(-48.0f));
    const auto slide_right = typography::make_motion_preset(
        typography::MotionPreset::SlideRight, options);
    CHECK(std::get<PositionProperty>(slide_right.properties[0]).value.evaluate(12.0).x ==
          doctest::Approx(48.0f));

    const auto pop = typography::make_motion_preset(typography::MotionPreset::Pop, options);
    CHECK(std::get<ScaleProperty>(pop.properties[0]).value.evaluate(12.0).x ==
          doctest::Approx(0.72f));

    const auto blur = typography::make_motion_preset(typography::MotionPreset::Blur, options);
    CHECK(std::get<BlurProperty>(blur.properties[0]).radius.evaluate(12.0) ==
          doctest::Approx(12.0f));
    const auto rotate = typography::make_motion_preset(typography::MotionPreset::Rotate, options);
    CHECK(std::get<RotationProperty>(rotate.properties[0]).degrees.evaluate(12.0).z ==
          doctest::Approx(-90.0f));
    const auto tracking = typography::make_motion_preset(
        typography::MotionPreset::Tracking, options);
    CHECK(std::get<TrackingProperty>(tracking.properties[0]).pixels.evaluate(12.0) ==
          doctest::Approx(24.0f));

    const auto typewriter_in = typography::make_motion_preset(
        typography::MotionPreset::Typewriter, options);
    CHECK(typewriter_in.selectors[0].start.evaluate(12.0) == doctest::Approx(0.0f));
    CHECK(typewriter_in.selectors[0].start.evaluate(36.0) == doctest::Approx(100.0f));

    options.entering = false;
    const auto typewriter = typography::make_motion_preset(
        typography::MotionPreset::Typewriter, options);
    CHECK(typewriter.selectors[0].unit == TextSelectorUnit::Grapheme);
    CHECK(typewriter.selectors[0].start.evaluate(12.0) == doctest::Approx(100.0f));
    CHECK(typewriter.selectors[0].start.evaluate(36.0) == doctest::Approx(0.0f));

    const auto word = typography::make_motion_preset(
        typography::MotionPreset::WordReveal, options);
    CHECK(typewriter.is_valid());
    CHECK(word.is_valid());
    CHECK(typewriter.selectors[0].start.evaluate(12.0) == doctest::Approx(100.0f));
    CHECK(typewriter.selectors[0].start.evaluate(36.0) == doctest::Approx(0.0f));
    CHECK(word.selectors[0].unit == TextSelectorUnit::Word);
    CHECK(word.id == "text.word_reveal_out");
}

TEST_CASE("text motion preset catalog rejects unbounded authoring inputs") {
    typography::MotionPresetOptions options;
    options.duration = Frame{0};
    CHECK_THROWS_AS(typography::make_motion_preset(
        typography::MotionPreset::Fade, options), std::invalid_argument);

    options.duration = Frame{24};
    options.blur_radius = 300.0f;
    CHECK_THROWS_AS(typography::make_motion_preset(
        typography::MotionPreset::Blur, options), std::invalid_argument);

    options.blur_radius = 12.0f;
    options.rotation_degrees = 4000.0f;
    CHECK_THROWS_AS(typography::make_motion_preset(
        typography::MotionPreset::Rotate, options), std::invalid_argument);
}
