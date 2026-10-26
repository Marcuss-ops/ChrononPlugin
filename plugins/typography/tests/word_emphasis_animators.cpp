#include <doctest/doctest.h>

#include <chrononplugin/typography/word_emphasis_animators.hpp>

#include <array>

using namespace chronon3d;
using namespace chrononplugin::typography;

TEST_CASE("V1 semantic prefixes and compatibility aliases") {
    CHECK(parse_emphasis_prefix("base:hello").kind == WordEmphasisKind::Base);
    CHECK(parse_emphasis_prefix("phrase:hello").kind == WordEmphasisKind::Phrase);
    CHECK(parse_emphasis_prefix("name:Elon").kind == WordEmphasisKind::Name);
    CHECK(parse_emphasis_prefix("word:important").kind == WordEmphasisKind::Word);
    CHECK(parse_emphasis_prefix("title:heading").kind == WordEmphasisKind::Title);
    CHECK(parse_emphasis_prefix("emph:fast").kind == WordEmphasisKind::Emph);
    CHECK(parse_emphasis_prefix("plain").kind == WordEmphasisKind::None);
    CHECK(strip_emphasis_prefix("phrase:hello") == "hello");
    CHECK(strip_emphasis_prefix("UTF-8:é") == "UTF-8:é");
}

TEST_CASE("V1 profiles are span-costed, not character-costed") {
    const Color accent{0.9f, 0.2f, 0.1f, 1.0f};
    for (const auto kind : {WordEmphasisKind::Base, WordEmphasisKind::Phrase,
                            WordEmphasisKind::Name, WordEmphasisKind::Word}) {
        const auto specs = make_light_text_animators(kind, accent, Frame{10}, 1, 3);
        REQUIRE(specs.size() == 1);
        CHECK(specs.front().selectors.size() == 1);
        CHECK(specs.front().selectors.front().unit == TextSelectorUnit::Word);
        CHECK(specs.front().selectors.front().start.default_value() == doctest::Approx(33.3333f));
        CHECK(specs.front().selectors.front().end.default_value() == doctest::Approx(66.6667f));
        CHECK(specs.front().properties.size() <= 3);
        for (const auto& property : specs.front().properties) {
            std::visit([](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (requires { value.value.keyframes(); }) {
                    CHECK(value.value.keyframes().size() <= 5);
                }
            }, property);
        }
    }
    CHECK(make_light_text_animators(WordEmphasisKind::Word, std::nullopt, Frame{0}, 3, 3).empty());
}

TEST_CASE("V1 profiles use continuous canonical keyframes") {
    const auto base = make_light_text_animators(WordEmphasisKind::Base, std::nullopt, Frame{0});
    REQUIRE(base.size() == 1);
    REQUIRE(base.front().properties.size() == 2);
    CHECK(std::holds_alternative<PositionProperty>(base.front().properties[0]));
    CHECK(std::holds_alternative<OpacityProperty>(base.front().properties[1]));

    const auto phrase = make_light_text_animators(WordEmphasisKind::Phrase, std::nullopt, Frame{0});
    REQUIRE(phrase.size() == 1);
    REQUIRE(phrase.front().properties.size() == 2);
    CHECK(std::get<ScaleProperty>(phrase.front().properties[0]).value.keyframes().size() == 3);
    CHECK(std::get<OpacityProperty>(phrase.front().properties[1]).value.keyframes().size() == 2);
}

TEST_CASE("V1 semantic grouping coalesces adjacent marked words") {
    const std::array<std::string_view, 5> ids{
        "name:Elon", "name:Musk", "plain", "word:launch", "emph:now"};
    const auto specs = make_light_text_animators_for_semantics(ids, std::nullopt, Frame{0});
    REQUIRE(specs.size() == 2);
    CHECK(specs[0].id.find("text_v1_name_span_0") != std::string::npos);
    CHECK(specs[1].id.find("text_v1_word_span_1") != std::string::npos);
    CHECK(specs[0].selectors.size() == 1);
    CHECK(specs[1].selectors.size() == 1);
}
