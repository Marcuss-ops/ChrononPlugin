// X / post card composition. Split out of social_showcase.cpp.

#include "social_showcase_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

namespace chrononplugin::socials::social_detail {

void AddXPost(Builder& builder, const XPostSpec& spec) {
    const int last = builder.frames() - 1;
    builder.Backdrop(spec.background, spec.accent, "#0E2A3D");
    builder.Chrome("X / POST CARD", spec.accent);

    json& card = builder.ShapeLayer("x_card", 250.0, 130.0, 1420.0, 820.0,
                                    spec.surface, 40.0);
    WithStroke(card, kHairline, 1.5);
    builder.Intro(card, 2, 38.0, 0.94, 1.004);

    json& avatar = builder.EllipseLayer("x_avatar", 380.0, 198.0, 104.0, 104.0,
                                        spec.author.avatar_color);
    builder.Intro(avatar, 12, 10.0, 0.70);
    json& avatar_initial = builder.TextLayer("x_avatar_initial", spec.author.avatar_initial,
                                             380.0, 198.0, 104.0, 104.0, 46.0,
                                             "#FFFFFF", kBold, "center");
    builder.Intro(avatar_initial, 12, 8.0, 0.80);

    json& name = builder.TextLayer("x_display_name", spec.author.display_name,
                                   520.0, 204.0, 320.0, 46.0, 36.0, kInk, kBold);
    builder.TextIntro(name, 16);

    if (spec.author.verified) {
        json& check = builder.TextLayer("x_verified_check", "\xE2\x9C\x93",
                                        728.0, 214.0, 30.0, 30.0, 20.0,
                                        spec.accent, kSymbol, "center");
        builder.FadeIn(check, 22, 8);
    }

    json& handle = builder.TextLayer(
        "x_handle", spec.author.handle + "  -  " + spec.timestamp, 520.0, 252.0,
        700.0, 34.0, 24.0, "#8B98A5", kRegular);
    builder.TextIntro(handle, 20);

    for (std::size_t i = 0; i < std::min<std::size_t>(2, spec.body_lines.size()); ++i) {
        json& line = builder.TextLayer(
            "x_body_" + std::to_string(i), spec.body_lines[i], 380.0,
            338.0 + static_cast<double>(i) * 58.0, 1160.0, 54.0, 38.0, kInk);
        builder.TextIntro(line, 22 + static_cast<int>(i) * 8);
    }

    json& media = builder.ShapeLayer("x_media", 380.0, 566.0, 1160.0, 216.0,
                                     "#0A1620", 22.0);
    WithStroke(media, "#1F3A4A", 1.5);
    builder.Intro(media, 36, 20.0, 0.97);

    json& headline = builder.TextLayer("x_media_headline", spec.media_headline,
                                       412.0, 622.0, 820.0, 56.0, 46.0, kInk,
                                       kBlack);
    builder.TextIntro(headline, 46);
    json& subline = builder.TextLayer("x_media_subline", spec.media_subline,
                                      412.0, 694.0, 760.0, 30.0, 22.0, "#8FA6B5");
    builder.TextIntro(subline, 50);

    json& play = builder.EllipseLayer("x_media_play", 1408.0, 632.0, 74.0, 74.0,
                                      spec.accent, 0.18);
    WithStroke(play, spec.accent, 1.5);
    builder.Intro(play, 48, 10.0, 0.7);
    json& play_glyph = builder.TextLayer("x_media_play_glyph", "\xE2\x96\xB6",
                                         1408.0, 632.0, 74.0, 74.0, 24.0,
                                         "#FFFFFF", kSymbol, "center");
    builder.FadeIn(play_glyph, 52, 8);

    json& media_progress = builder.ShapeLayer("x_media_progress", 412.0, 746.0,
                                              560.0, 4.0, spec.accent, 2.0);
    builder.GrowX(media_progress, 560.0, 58, last - 10, 0.02, 0.74);

    json& actions = builder.TextLayer(
        "x_actions",
        "\xE2\x97\x8B " + spec.replies + "      \xE2\x86\xBB " + spec.reposts +
            "      \xE2\x99\xA1 " + spec.likes + "      \xE2\x96\xB1 " + spec.views,
        380.0, 842.0, 1160.0, 36.0, 25.0, "#8B98A5", kSymbol);
    builder.TextIntro(actions, 60, 10.0);
}
}  // namespace chrononplugin::socials::social_detail
