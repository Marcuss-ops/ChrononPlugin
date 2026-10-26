#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// presets/text/subtitle.hpp — Productive subtitle types and helpers
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/math/color.hpp>
#include <chronon3d/text/timed_text_document.hpp>

#include <optional>
#include <string_view>

namespace chrononplugin::typography {

using namespace chronon3d;

using SubtitleTrack = TimedTextDocument;
using SubtitleCue = TimedCue;
using TimedWord = ::chronon3d::TimedWord;

struct WordStyleState {
    std::optional<std::size_t> word_index;
    std::string semantic_id;
    float progress{0.0f};
    bool highlighted{false};
    /// TICKET-WORD-TIMING-QUALITY: provenance classification of the
    /// per-word timing that produced this state.  Routed through from
    /// `TimedCue::word_timing_quality` by `active_word_style_at()` so
    /// the renderer never has to walk the cue structure itself.
    /// Defaults to `None` so a default-constructed state advertises
    /// "no word-level data" — the conservative answer.
    /// Fully-qualified `::chronon3d::` prefix pins the type lookup to the
    /// parent namespace regardless of `using namespace chrono3d::presets::text;`
    /// at test-file scope.  `WordTimingQuality` lives in `chronon3d`, NOT in
    /// `presets::text`; the explicit qualification removes any ambiguity
    /// for downstream readers and IDE tooling.
    ::chronon3d::WordTimingQuality quality{::chronon3d::WordTimingQuality::None};
    std::optional<Color> color;
    std::optional<float> scale;
    std::optional<Color> background;
    [[nodiscard]] bool operator==(const WordStyleState&) const noexcept = default;
};


inline const SubtitleCue* active_cue_at(const SubtitleTrack& track, float time_s) {
    for (const auto& cue : track.cues) {
        if (time_s >= cue.start_s && time_s < cue.end_s) return &cue;
    }
    return nullptr;
}

inline const TimedWord* active_word_at(const SubtitleCue& cue, float time_s) {
    for (const auto& word : cue.words) {
        if (time_s >= word.start_s && time_s < word.end_s) return &word;
    }
    return nullptr;
}

inline WordStyleState active_word_style_at(
    const SubtitleTrack& track, float time_s,
    std::optional<Color> highlight_color = std::nullopt,
    std::optional<float> highlight_scale = std::nullopt,
    std::optional<Color> highlight_background = std::nullopt)
{
    WordStyleState state;
    const auto* cue = active_cue_at(track, time_s);
    if (!cue || cue->words.empty()) return state;
    const auto* word = active_word_at(*cue, time_s);
    if (!word) return state;
    state.highlighted = true;
    state.quality = cue->word_timing_quality;
    state.semantic_id = word->semantic_id;
    for (std::size_t i = 0; i < cue->words.size(); ++i) {
        if (cue->words[i].semantic_id == word->semantic_id) { state.word_index = i; break; }
    }
    const float duration = word->end_s - word->start_s;
    if (duration > 0.0f) state.progress = (time_s - word->start_s) / duration;
    state.color = highlight_color;
    state.scale = highlight_scale;
    state.background = highlight_background;
    return state;
}

} // namespace chrononplugin::typography
