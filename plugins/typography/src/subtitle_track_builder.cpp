#include <chrononplugin/captions/subtitle_track_builder.hpp>
#include <chronon3d/core/types/time.hpp>

#include <chrononplugin/typography/word_emphasis_animators.hpp>
#include <chronon3d/text/glyph_selector_spec.hpp>
#include <chronon3d/text/prepared_text.hpp>
#include <chronon3d/text/text_definition.hpp>
#include "subtitle_render_plan.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace chrononplugin::captions {

using namespace chronon3d;

namespace {

// Return the 0-based word index of the whitespace-delimited word that starts
// at byte_start. This mirrors the segmentation used by the timed-text adapters.
[[nodiscard]] std::size_t word_index_for_byte_start(
    std::string_view text,
    std::size_t byte_start,
    std::size_t fallback) {
    std::size_t index = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() &&
               (text[i] == ' ' || text[i] == '\t' ||
                text[i] == '\n' || text[i] == '\r')) {
            ++i;
        }
        if (i >= text.size()) break;
        if (i == byte_start) return index;
        while (i < text.size() &&
               text[i] != ' ' && text[i] != '\t' &&
               text[i] != '\n' && text[i] != '\r') {
            ++i;
        }
        ++index;
    }
    return fallback;
}

struct SemanticWordEntry {
    std::size_t word_index{0};
    typography::WordEmphasisKind kind{
        typography::WordEmphasisKind::None};
    Frame start_frame{0};
};

// Convert tagged TimedWord entries into constant-cost semantic span animators.
// Adjacent words with the same semantic role are merged, so a multi-word name
// or important phrase costs one selector + one animator rather than one per
// word or character.
[[nodiscard]] std::vector<TextAnimatorSpec> build_semantic_emphasis_animators(
    const TimedCue& cue,
    FrameRate frame_rate,
    std::size_t cue_index) {
    std::vector<SemanticWordEntry> entries;
    entries.reserve(cue.words.size());

    for (std::size_t fallback_index = 0;
         fallback_index < cue.words.size();
         ++fallback_index) {
        const auto& word = cue.words[fallback_index];
        const auto parsed = typography::parse_emphasis_prefix(
            word.semantic_id);
        if (!typography::is_emphasis_kind(parsed.kind)) {
            continue;
        }

        entries.push_back(SemanticWordEntry{
            .word_index = word_index_for_byte_start(
                cue.text, word.byte_start, fallback_index),
            .kind = parsed.kind,
            .start_frame = resolve_frame_range(
                word.start_s, word.end_s, frame_rate,
                MinimumFrameDuration::AtLeastOneFrame).start,
        });
    }

    if (entries.empty()) {
        return {};
    }

    std::sort(entries.begin(), entries.end(),
              [](const SemanticWordEntry& a, const SemanticWordEntry& b) {
                  return a.word_index < b.word_index;
              });

    const f32 word_count = static_cast<f32>(cue.words.size());
    std::vector<TextAnimatorSpec> animators;
    animators.reserve(entries.size());

    std::size_t group_begin = 0;
    while (group_begin < entries.size()) {
        std::size_t group_end = group_begin;
        Frame group_start = entries[group_begin].start_frame;
        const auto kind = entries[group_begin].kind;

        while (group_end + 1 < entries.size() &&
               entries[group_end + 1].kind == kind &&
               entries[group_end + 1].word_index ==
                   entries[group_end].word_index + 1) {
            ++group_end;
            group_start = std::min(
                group_start, entries[group_end].start_frame);
        }

        const std::size_t first_word = entries[group_begin].word_index;
        const std::size_t last_word = entries[group_end].word_index;

        GlyphSelectorSpec selector;
        selector.id = "subtitle_semantic_cue_" + std::to_string(cue_index) +
                      "_words_" + std::to_string(first_word) + "_" +
                      std::to_string(last_word);
        selector.unit = TextSelectorUnit::Word;
        selector.shape = TextSelectorShape::Square;
        selector.order = TextSelectorOrder::Forward;
        selector.combine = SelectorCombineMode::Replace;
        selector.start = AnimatedValue<f32>(
            static_cast<f32>(first_word) * 100.0f / word_count);
        selector.end = AnimatedValue<f32>(
            static_cast<f32>(last_word + 1) * 100.0f / word_count);
        selector.amount = AnimatedValue<f32>(100.0f);
        selector.exclude_spaces = true;

        const std::string suffix =
            "cue_" + std::to_string(cue_index) + "_words_" +
            std::to_string(first_word) + "_" + std::to_string(last_word);

        auto animator = typography::make_lightweight_emphasis_animator(
            kind,
            std::nullopt,
            group_start,
            std::move(selector),
            suffix);
        if (animator.has_value()) {
            animators.push_back(std::move(*animator));
        }

        group_begin = group_end + 1;
    }

    return animators;
}

} // namespace

FrameRate SubtitleTrackBuilder::active_frame_rate() const noexcept {
    if (frame_rate_override_.has_value()) {
        return frame_rate_override_.value();
    }
    return builder_->frame_rate();
}

std::vector<TimedWordBinding>
SubtitleTrackBuilder::build_word_bindings(const TimedCue& cue) {
    std::vector<TimedWordBinding> bindings;
    bindings.reserve(cue.words.size());
    for (std::size_t w = 0; w < cue.words.size(); ++w) {
        const auto& word = cue.words[w];
        const std::size_t resolved_word_index =
            word_index_for_byte_start(cue.text, word.byte_start, w);
        bindings.push_back(TimedWordBinding{
            .semantic_id = word.semantic_id,
            .word_index = resolved_word_index,
            .total_words = cue.words.size(),
            .byte_start = word.byte_start,
            .byte_end = word.byte_end,
            .start_s = word.start_s,
            .end_s = word.end_s,
        });
    }
    return bindings;
}

std::vector<GlyphSelectorSpec>
SubtitleTrackBuilder::build_word_selectors(
    const TimedCue& cue,
    FrameRate frame_rate,
    Frame start_frame,
    std::size_t cue_index) {
    std::vector<GlyphSelectorSpec> selectors;
    if (cue.words.empty()) {
        return selectors;
    }

    const std::size_t word_count = cue.words.size();
    const f32 word_count_f = static_cast<f32>(word_count);

    for (std::size_t w = 0; w < word_count; ++w) {
        const auto& word = cue.words[w];
        const std::size_t word_index =
            word_index_for_byte_start(cue.text, word.byte_start, w);

        const f32 start_pct =
            static_cast<f32>(word_index) * 100.0f / word_count_f;
        const f32 end_pct =
            static_cast<f32>(word_index + 1) * 100.0f / word_count_f;

        const TimeRange word_range = resolve_frame_range(
            word.start_s, word.end_s, frame_rate,
            MinimumFrameDuration::AtLeastOneFrame);
        const Frame word_start_frame = word_range.start;
        const Frame word_end_frame = word_range.end;

        GlyphSelectorSpec word_selector;
        word_selector.unit = TextSelectorUnit::Word;
        word_selector.shape = TextSelectorShape::Square;
        word_selector.order = TextSelectorOrder::Forward;
        word_selector.start = start_pct;
        word_selector.end = end_pct;
        word_selector.id = "subtitle_cue_" + std::to_string(cue_index) +
                           "_word_" + word.semantic_id;

        AnimatedValue<f32> amount;
        if (start_frame < word_start_frame) {
            amount.add_keyframe(
                start_frame, 0.0f, EasingCurve{Easing::Hold});
        }
        amount.add_keyframe(
            word_start_frame, 100.0f, EasingCurve{Easing::Hold});
        amount.add_keyframe(
            word_end_frame, 0.0f, EasingCurve{Easing::Hold});
        word_selector.amount = std::move(amount);

        selectors.push_back(std::move(word_selector));
    }

    return selectors;
}

void SubtitleTrackBuilder::build() {
    if (!track_ || track_->cues.empty()) {
        return;
    }

    // Resolve semantic text placement against the actual authoring canvas.
    // Without this viewport hand-off SafeAreaBottom keeps the historical
    // centred origin when the layer is materialized by LayerBuilder.
    builder_->screen_dimensions(canvas_->width, canvas_->height);

    const bool is_karaoke_preset =
        preset_id_ == "karaoke_fill" ||
        preset_id_ == "active_word_pop";
    const bool enforce_authoritative =
        (is_karaoke_preset || require_authoritative_) && !allow_estimated_;

    if (enforce_authoritative) {
        for (const auto& cue : track_->cues) {
            if (cue.word_timing_quality !=
                WordTimingQuality::Authoritative) {
                throw std::runtime_error(
                    "SubtitleTrackBuilder: preset '" + preset_id_ +
                    "' requires Authoritative per-word timing. Set "
                    ".allow_estimated_word_timing(true) to opt-in to "
                    "uniform-split word timing.");
            }
        }
    }

    for (std::size_t i = 0; i < track_->cues.size(); ++i) {
        const auto& cue = track_->cues[i];
        if (cue.text.empty()) {
            continue;
        }

        const TimeRange cue_range = resolve_frame_range(
            cue.start_s, cue.end_s, active_frame_rate(),
            MinimumFrameDuration::AtLeastOneFrame);
        const Frame current_frame = current_frame_;
        if (!cue_range.contains(current_frame)) {
            continue;
        }

        SubtitleRenderPlan plan;
        plan.cue_index = i;
        plan.timing.range = cue_range;
        plan.layout = SubtitleLayoutSpec{
            .box_size = box_size_,
            .align = align_,
            .vertical_align = vertical_align_,
            .anchor = vertical_align_ == VerticalAlign::Bottom
                ? TextAnchor::BottomCenter
                : (vertical_align_ == VerticalAlign::Top
                    ? TextAnchor::TopCenter
                    : TextAnchor::Center),
            .placement = placement_,
            .font_size = font_size_,
        };
        plan.words.bindings = build_word_bindings(cue);

        TextDefinition spec;
        spec.content.value = cue.text;
        // Explicit font reference only. No implicit engine default: when no
        // .font(...) was declared the run keeps an empty font path and the
        // canonical font wiring (font engine + per-runtime resolver) decides.
        if (font_) spec.style.font.font_path = font_->path();
        spec.style.font.font_size = plan.layout.font_size;
        spec.style.color = color_;
        // The plan style shadow replaces the cue's shadow stack, mirroring
        // the text materializer's single-enabled-TextShadow lowering.
        if (shadow_) spec.style.shadows = {*shadow_};
        spec.frame.size = plan.layout.box_size;
        spec.frame.align = plan.layout.align;
        spec.frame.vertical_align = plan.layout.vertical_align;
        spec.frame.anchor = plan.layout.anchor;
        spec.frame.placement = plan.layout.placement;

        plan.text = prepare_text(spec);

        // Existing timed-word selectors drive preset-specific active-word
        // effects such as karaoke fill and active-word pop.
        plan.words.selectors =
            build_word_selectors(cue, active_frame_rate(), cue_range.start, i);
        if (!plan.text.animation.animators.empty()) {
            auto& preset_animator = plan.text.animation.animators.front();
            for (auto& word_selector : plan.words.selectors) {
                preset_animator.selectors.push_back(
                    std::move(word_selector));
            }
        }

        // Semantic tags are independent of the selected subtitle preset.
        // Adjacent base:/phrase:/name:/word: entries are merged into one
        // constant-cost span animator and appended to the canonical stack.
        auto semantic_animators = build_semantic_emphasis_animators(
            cue, active_frame_rate(), i);
        for (auto& animator : semantic_animators) {
            plan.text.animation.animators.push_back(std::move(animator));
        }

        builder_->text_run(
            "subtitle_cue_" + std::to_string(plan.cue_index),
            std::move(plan.text)).commit();
        break;
    }
}

} // namespace chrononplugin::captions
