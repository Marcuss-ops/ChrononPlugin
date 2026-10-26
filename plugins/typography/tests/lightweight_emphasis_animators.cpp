// Focused contract tests for the constant-cost semantic emphasis profiles.

#include <doctest/doctest.h>

#include <chrononplugin/typography/word_emphasis_animators.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

using namespace chronon3d;
using namespace chrononplugin::typography;

namespace {

GlyphSelectorSpec full_word_selector(std::string_view id = "semantic_span") {
    GlyphSelectorSpec selector;
    selector.id = std::string{id};
    selector.unit = TextSelectorUnit::Word;
    selector.shape = TextSelectorShape::Square;
    selector.order = TextSelectorOrder::Forward;
    selector.combine = SelectorCombineMode::Replace;
    selector.start = AnimatedValue<f32>(0.0f);
    selector.end = AnimatedValue<f32>(100.0f);
    selector.amount = AnimatedValue<f32>(100.0f);
    selector.exclude_spaces = true;
    return selector;
}

template <typename Property>
bool has_property(const TextAnimatorSpec& spec) {
    for (const auto& property : spec.properties) {
        if (std::holds_alternative<Property>(property)) {
            return true;
        }
    }
    return false;
}

std::size_t animated_keyframe_count(const TextAnimatorSpec& spec) {
    std::size_t count = 0;
    for (const auto& property : spec.properties) {
        std::visit(
            [&count](const auto& value) {
                using Property = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Property, PositionProperty> ||
                              std::is_same_v<Property, ScaleProperty> ||
                              std::is_same_v<Property, OpacityProperty>) {
                    count += value.value.keyframes().size();
                } else if constexpr (std::is_same_v<Property, BlurProperty>) {
                    count += value.radius.keyframes().size();
                } else if constexpr (std::is_same_v<Property, TrackingProperty>) {
                    count += value.pixels.keyframes().size();
                } else if constexpr (std::is_same_v<Property, RotationProperty>) {
                    count += value.degrees.keyframes().size();
                } else if constexpr (std::is_same_v<Property, FillColorProperty>) {
                    count += value.color.keyframes().size();
                }
            },
            property);
    }
    return count;
}

} // namespace

TEST_CASE("semantic emphasis parser supports production aliases") {
    const auto base = parse_emphasis_prefix("base:neutral");
    CHECK(base.kind == WordEmphasisKind::Base);
    CHECK(base.remainder == std::string_view{"neutral"});

    const auto phrase = parse_emphasis_prefix("phrase:very important sentence");
    CHECK(phrase.kind == WordEmphasisKind::Phrase);
    CHECK(phrase.remainder == std::string_view{"very important sentence"});

    const auto title = parse_emphasis_prefix("title:legacy heading");
    CHECK(title.kind == WordEmphasisKind::Title);
    CHECK(title.remainder == std::string_view{"legacy heading"});

    const auto word = parse_emphasis_prefix("word:critical");
    CHECK(word.kind == WordEmphasisKind::Word);
    CHECK(word.remainder == std::string_view{"critical"});

    const auto emph = parse_emphasis_prefix("emph:legacy-keyword");
    CHECK(emph.kind == WordEmphasisKind::Emph);
    CHECK(emph.remainder == std::string_view{"legacy-keyword"});

    CHECK(strip_emphasis_prefix("phrase:Alpha Beta") == "Alpha Beta");
    CHECK(strip_emphasis_prefix("word:Launch") == "Launch");
}

TEST_CASE("None does not allocate a lightweight animator") {
    const auto result = make_lightweight_emphasis_animator(
        WordEmphasisKind::None,
        std::nullopt,
        Frame{10},
        full_word_selector());
    CHECK_FALSE(result.has_value());
}

TEST_CASE("Base profile is one selector with fade and rise") {
    auto result = make_lightweight_emphasis_animator(
        WordEmphasisKind::Base,
        std::nullopt,
        Frame{20},
        full_word_selector(),
        "base_case");

    REQUIRE(result.has_value());
    CHECK(result->id == "light_emphasis_base_base_case");
    REQUIRE(result->selectors.size() == 1);
    REQUIRE(result->properties.size() == 2);
    CHECK(has_property<PositionProperty>(*result));
    CHECK(has_property<OpacityProperty>(*result));
    CHECK_FALSE(has_property<ScaleProperty>(*result));
    CHECK(animated_keyframe_count(*result) == 4);

    const auto& position = std::get<PositionProperty>(result->properties[0]);
    REQUIRE(position.value.keyframes().size() == 2);
    CHECK(position.value.keyframes()[0].value.y == doctest::Approx(10.0f));
    CHECK(position.value.keyframes()[1].value.y == doctest::Approx(0.0f));
}

TEST_CASE("Important phrase profile is a soft span reveal") {
    auto result = make_lightweight_emphasis_animator(
        WordEmphasisKind::Title,
        std::nullopt,
        Frame{30},
        full_word_selector(),
        "phrase_case");

    REQUIRE(result.has_value());
    REQUIRE(result->selectors.size() == 1);
    REQUIRE(result->properties.size() == 2);
    CHECK(has_property<ScaleProperty>(*result));
    CHECK(has_property<OpacityProperty>(*result));
    CHECK(animated_keyframe_count(*result) == 5);

    const auto& scale = std::get<ScaleProperty>(result->properties[0]);
    REQUIRE(scale.value.keyframes().size() == 3);
    CHECK(scale.value.keyframes()[0].value.x == doctest::Approx(0.96f));
    CHECK(scale.value.keyframes()[1].value.x == doctest::Approx(1.02f));
    CHECK(scale.value.keyframes()[2].value.x == doctest::Approx(1.0f));
}

TEST_CASE("Special name profile supports one static accent") {
    const Color accent{0.20f, 0.75f, 1.0f, 1.0f};
    auto result = make_lightweight_emphasis_animator(
        WordEmphasisKind::Name,
        accent,
        Frame{40},
        full_word_selector(),
        "name_case");

    REQUIRE(result.has_value());
    REQUIRE(result->selectors.size() == 1);
    REQUIRE(result->properties.size() == 3);
    CHECK(has_property<ScaleProperty>(*result));
    CHECK(has_property<OpacityProperty>(*result));
    CHECK(has_property<FillColorProperty>(*result));
    CHECK(animated_keyframe_count(*result) == 5);

    const auto& scale = std::get<ScaleProperty>(result->properties[0]);
    CHECK(scale.value.keyframes()[0].value.x == doctest::Approx(0.94f));
    const auto& fill = std::get<FillColorProperty>(result->properties[2]);
    CHECK(fill.color.evaluate(0.0) == accent);
}

TEST_CASE("Important word profile is the fastest punch") {
    const Color accent{1.0f, 0.78f, 0.15f, 1.0f};
    auto result = make_lightweight_emphasis_animator(
        WordEmphasisKind::Emph,
        accent,
        Frame{50},
        full_word_selector(),
        "word_case");

    REQUIRE(result.has_value());
    REQUIRE(result->selectors.size() == 1);
    REQUIRE(result->properties.size() == 3);
    CHECK(animated_keyframe_count(*result) == 5);

    const auto& scale = std::get<ScaleProperty>(result->properties[0]);
    const auto& opacity = std::get<OpacityProperty>(result->properties[1]);
    REQUIRE(scale.value.keyframes().size() == 3);
    REQUIRE(opacity.value.keyframes().size() == 2);
    CHECK(scale.value.keyframes()[0].value.x == doctest::Approx(0.90f));
    CHECK(scale.value.keyframes()[1].value.x == doctest::Approx(1.08f));
    CHECK(opacity.value.keyframes()[1].frame.integral() -
          opacity.value.keyframes()[0].frame.integral() == 2);
}

TEST_CASE("lightweight profiles stay inside the constant-cost budget") {
    constexpr std::array kinds{
        WordEmphasisKind::Base,
        WordEmphasisKind::Title,
        WordEmphasisKind::Name,
        WordEmphasisKind::Emph,
    };

    for (const auto kind : kinds) {
        auto result = make_lightweight_emphasis_animator(
            kind,
            Color{1.0f, 0.8f, 0.2f, 1.0f},
            Frame{0},
            full_word_selector());

        REQUIRE(result.has_value());
        CHECK(result->selectors.size() == 1);
        CHECK(result->properties.size() <= 3);
        CHECK(animated_keyframe_count(*result) <= 5);

        auto compiled = *result;
        auto& compiled_ref = compiled.compile();
        CHECK(&compiled_ref == &compiled);
        CHECK(compiled.is_valid());
    }
}

TEST_CASE("Base convenience path is O(1) for very long text") {
    const auto short_text = make_word_emphasis_animators(
        WordEmphasisKind::Base, std::nullopt, Frame{0}, 1);
    const auto long_text = make_word_emphasis_animators(
        WordEmphasisKind::Base, std::nullopt, Frame{0}, 10000);

    REQUIRE(short_text.size() == 1);
    REQUIRE(long_text.size() == 1);
    CHECK(short_text.front().selectors.size() == 1);
    CHECK(long_text.front().selectors.size() == 1);
    CHECK(short_text.front().properties.size() == long_text.front().properties.size());
    CHECK(animated_keyframe_count(short_text.front()) ==
          animated_keyframe_count(long_text.front()));
}
