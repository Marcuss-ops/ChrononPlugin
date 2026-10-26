#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// authoring/subtitle_track_builder.hpp — Fluent subtitle track authoring
//
// Consumes the canonical SubtitleTrack (TimedTextDocument) and produces the
// text-run active at the current frame on the parent LayerBuilder. Keeping a
// single active run avoids abusing layer timing for per-cue timing and keeps
// the render graph small and unambiguous.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/assets/asset_ref.hpp>
#include <chrononplugin/typography/subtitle.hpp>
#include <chrononplugin/typography/word_emphasis_animators.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/resolve_text_placement.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chrononplugin::captions {

using namespace chronon3d;

using chronon3d::CanvasInfo;
using chronon3d::Color;
using chronon3d::Frame;
using chronon3d::FrameRate;
using chronon3d::GlyphSelectorSpec;
using chronon3d::LayerBuilder;
using chronon3d::TextAlign;
using chronon3d::TextPlacement;
using chronon3d::TextPlacementKind;
using chronon3d::TextShadow;
using chronon3d::Vec2;
using chronon3d::VerticalAlign;

/// Binding between a `TimedWord` and the runtime `TextUnitMap::Word` selector.
///
/// This struct is the public, minimal surface for connecting subtitle word
/// timing to per-word text animators.  It carries the same semantic id as the
/// source `TimedWord` plus the byte range (UTF-8) that the runtime TextUnitMap
/// can resolve into a word index.
struct TimedWordBinding {
    std::string semantic_id;
    std::size_t word_index{0};
    std::size_t total_words{0};
    std::size_t byte_start{0};
    std::size_t byte_end{0};
    float start_s{0.0f};
    float end_s{0.0f};
};

/// Builds scheduled text-runs from a SubtitleTrack.
///
/// Usage:
///   layer.subtitles(track)
///        .preset("karaoke_fill")
///        .font(assets::FontRef{"fonts/Subtitle.ttf", "subtitle.bold"}, 48.0f)
///        .color(Color::White)
///        .box({1400.0f, 200.0f})
///        .align(TextAlign::Center)
///        .place(TextPlacementKind::SafeAreaBottom);
///
/// The builder declares NO implicit font asset: without an explicit
/// `.font(...)` the emitted run carries an empty font path and font
/// resolution is delegated to the canonical font-engine wiring.
class SubtitleTrackBuilder {
public:
    SubtitleTrackBuilder(LayerBuilder& builder,
                          const CanvasInfo& canvas,
                          const typography::SubtitleTrack& track,
                          Frame current_frame)
        : builder_(&builder), canvas_(&canvas), track_(&track),
          current_frame_(current_frame) {}

    /// Choose a text preset from the registry (default: minimal_white).
    SubtitleTrackBuilder& preset(std::string_view preset_id) {
        preset_id_ = std::string{preset_id};
        return *this;
    }

    /// Set the font (explicit canonical reference) and size for every cue.
    /// The reference is resolved through the per-runtime AssetResolver at
    /// materialization time; the core stores no repository-relative path.
    SubtitleTrackBuilder& font(assets::FontRef ref, float size) {
        font_ = std::move(ref);
        font_size_ = size;
        return *this;
    }

    /// Set the font path and size for every cue. Backward-compatible
    /// spelling: the raw path is wrapped into the canonical
    /// `assets::FontRef` so a single storage slot stays authoritative.
    SubtitleTrackBuilder& font(std::string_view path, float size) {
        return font(assets::FontRef{std::string{path}}, size);
    }

    /// Set the fill colour for every cue.
    SubtitleTrackBuilder& color(const Color& c) {
        color_ = c;
        return *this;
    }

    /// Set a drop shadow applied to every cue's text. The shadow replaces
    /// the cue's shadow stack entirely (one enabled TextShadow), mirroring
    /// how the text materializer lowers a plan `style.shadow` block.
    SubtitleTrackBuilder& shadow(const TextShadow& value) {
        shadow_ = value;
        return *this;
    }

    /// Set the layout box size for every cue.
    SubtitleTrackBuilder& box(const Vec2& size) {
        box_size_ = size;
        return *this;
    }

    /// Set horizontal alignment for every cue.
    SubtitleTrackBuilder& align(TextAlign a) {
        align_ = a;
        return *this;
    }

    /// Set placement kind for every cue.
    SubtitleTrackBuilder& place(TextPlacementKind kind) {
        placement_ = TextPlacement{kind};
        return *this;
    }

    /// Set the complete semantic placement, including an absolute/additive offset.
    SubtitleTrackBuilder& place(TextPlacement placement) {
        placement_ = placement;
        return *this;
    }

    /// Set vertical alignment within the cue layout box.
    SubtitleTrackBuilder& vertical_align(VerticalAlign align) {
        vertical_align_ = align;
        return *this;
    }

    /// Override the frame rate used to convert cue seconds to frames.
    /// If not called, the frame rate is read from the parent LayerBuilder.
    SubtitleTrackBuilder& frame_rate(FrameRate rate) {
        frame_rate_override_ = rate;
        return *this;
    }


    /// Allow `Estimated` per-word timing (SRT/VTT, JSON auto-fallback).
    /// When set, the builder will NOT fail on Estimated cues even for
    /// karaoke presets. This is an explicit opt-in for formats that do
    /// not provide real per-word timestamps.
    SubtitleTrackBuilder& allow_estimated_word_timing(bool value = true) {
        allow_estimated_ = value;
        return *this;
    }

    /// Commit the track: create the text-run active at the current frame.
    /// Cues are translated from seconds to frames using the parent
    /// LayerBuilder's frame rate, or the rate set via frame_rate().
    void build();

    /// Build the canonical binding records for each word in `cue`.
    /// Exposed for unit tests; production code uses build().
    [[nodiscard]] static std::vector<TimedWordBinding>
    build_word_bindings(const TimedCue& cue);

    /// Build the per-word GlyphSelectorSpec list for `cue`.
    /// Exposed for unit tests; production code uses build().
    [[nodiscard]] static std::vector<GlyphSelectorSpec>
    build_word_selectors(const TimedCue& cue, FrameRate frame_rate, Frame start_frame, std::size_t cue_index = 0);

private:
    LayerBuilder* builder_{nullptr};
    const CanvasInfo* canvas_{nullptr};
    const typography::SubtitleTrack* track_{nullptr};
    Frame current_frame_{0};

    std::string preset_id_{"minimal_white"};
    // No implicit engine default font. The old `"assets/fonts/Poppins-Bold.ttf"`
    // default was a repository-relative dependency and is removed; unset
    // means "no font asset declared" (canonical font wiring decides).
    std::optional<assets::FontRef> font_{std::nullopt};
    float font_size_{48.0f};
    Color color_{1.0f, 1.0f, 1.0f, 1.0f};
    std::optional<TextShadow> shadow_{std::nullopt};
    Vec2 box_size_{1400.0f, 200.0f};
    TextAlign align_{TextAlign::Center};
    TextPlacement placement_{TextPlacementKind::SafeAreaBottom};
    VerticalAlign vertical_align_{VerticalAlign::Middle};
    std::optional<FrameRate> frame_rate_override_{std::nullopt};
    bool require_authoritative_{false};
    bool allow_estimated_{false};

    [[nodiscard]] FrameRate active_frame_rate() const noexcept;

};

} // namespace chrononplugin::captions
