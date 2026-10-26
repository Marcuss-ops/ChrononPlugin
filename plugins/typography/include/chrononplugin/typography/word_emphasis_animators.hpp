#pragma once

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/text/animation/text_animator_spec.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace chrononplugin::typography {

using namespace chronon3d;

// Semantic profiles for the lightweight V1 text animation contract.  Title
// and Emph remain distinct enum values for source compatibility; their
// parser aliases map to the corresponding legacy profile names.
enum class WordEmphasisKind {
    None,
    Base,
    Phrase,
    Name,
    Word,
    Title,
    Emph
};

struct EmphasisParseResult {
    WordEmphasisKind kind{WordEmphasisKind::None};
    std::string_view remainder{};
};

[[nodiscard]] EmphasisParseResult parse_emphasis_prefix(std::string_view semantic_id);

[[nodiscard]] inline bool is_emphasis_kind(WordEmphasisKind kind) noexcept {
    return kind != WordEmphasisKind::None;
}

[[nodiscard]] inline std::string strip_emphasis_prefix(std::string_view semantic_id) {
    return std::string{parse_emphasis_prefix(semantic_id).remainder};
}

// Build exactly one canonical TextAnimatorSpec for one semantic span.  The
// caller invokes this once per span, so cost is O(spans), never O(characters).
// `span_index/span_count` select a Word range in the canonical TextUnitMap.
// Profiles obey the V1 budget: one selector, at most three properties, and
// at most five keyframes per property.
[[nodiscard]] std::vector<TextAnimatorSpec> make_light_text_animators(
    WordEmphasisKind kind,
    std::optional<Color> accent,
    Frame start_frame,
    std::size_t span_index = 0,
    std::size_t span_count = 1);

// Compatibility adapter for authored subtitle spans. It delegates to the
// same V1 profile implementation while preserving the caller's canonical
// word selector and diagnostic suffix.
[[nodiscard]] std::optional<TextAnimatorSpec> make_lightweight_emphasis_animator(
    WordEmphasisKind kind,
    std::optional<Color> accent,
    Frame start_frame,
    GlyphSelectorSpec selector,
    std::string_view id_suffix = {});

// Legacy name retained for source compatibility. It is intentionally
// constant-cost for every profile and never creates per-character animators.
[[nodiscard]] std::vector<TextAnimatorSpec> make_word_emphasis_animators(
    WordEmphasisKind kind,
    std::optional<Color> accent,
    Frame start_frame,
    std::size_t span_count);

// Resolve a sequence of TimedWord semantic IDs into one animator per
// contiguous marked span. Adjacent `name:`/`title:` or `word:`/`emph:` IDs
// are coalesced; unmarked words do not allocate an extra animator.
[[nodiscard]] std::vector<TextAnimatorSpec> make_light_text_animators_for_semantics(
    std::span<const std::string_view> semantic_ids,
    std::optional<Color> accent,
    Frame start_frame);

} // namespace chrononplugin::typography
