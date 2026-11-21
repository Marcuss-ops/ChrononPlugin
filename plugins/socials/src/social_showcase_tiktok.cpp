// TikTok / vertical overlay composition. Split out of social_showcase.cpp.

#include "social_showcase_internal.hpp"

#include <string>

namespace chrononplugin::socials::social_detail {

void AddTikTokOverlay(Builder& builder, const TikTokSpec& spec) {
    builder.Backdrop(spec.background, spec.accent, spec.secondary);
    builder.Chrome("TIKTOK / VERTICAL OVERLAY", spec.accent);

    json& phone = builder.ShapeLayer("tk_phone", 680.0, 50.0, 560.0, 980.0,
                                     "#07080A", 56.0);
    WithStroke(phone, "#2B2D33", 2.0);
    builder.Intro(phone, 0, 34.0, 0.95);

    json& screen = builder.ShapeLayer("tk_screen", 692.0, 62.0, 536.0, 956.0,
                                      "#0E1519", 46.0);
    builder.FadeIn(screen, 4, 14);

    json& headline_a = builder.TextLayer("tk_headline_a", "SOCIAL", 730.0,
                                          236.0, 470.0, 82.0, 66.0, kInk,
                                          kBlack);
    builder.TextIntro(headline_a, 12, 16.0);
    json& headline_b = builder.TextLayer("tk_headline_b", "STRUCTURES", 730.0,
                                          306.0, 470.0, 82.0, 58.0, kInk,
                                          kBlack);
    builder.TextIntro(headline_b, 16, 16.0);

    json& foreground = builder.ShapeLayer("tk_foreground", 710.0, 700.0, 500.0,
                                           306.0, "#071013", 0.0, 0.82);
    builder.FadeIn(foreground, 18, 14);

    json& avatar = builder.EllipseLayer("tk_avatar", 1120.0, 548.0, 64.0, 64.0,
                                         spec.accent);
    builder.Intro(avatar, 26, 10.0, 0.7);

    json& heart = builder.TextLayer("tk_heart", "\xE2\x99\xA5", 1124.0, 644.0,
                                     60.0, 44.0, 38.0, kInk, kSymbol, "center");
    builder.TextIntro(heart, 34, 10.0);
    json& comment = builder.TextLayer("tk_comment", "\xE2\x97\x8B", 1128.0,
                                       734.0, 52.0, 40.0, 32.0, kInk, kSymbol,
                                       "center");
    builder.TextIntro(comment, 38, 10.0);
    json& share = builder.TextLayer("tk_share", "\xE2\x86\x97", 1128.0, 814.0,
                                     52.0, 40.0, 34.0, kInk, kSymbol, "center");
    builder.TextIntro(share, 42, 10.0);
    json& bookmark = builder.TextLayer("tk_bookmark", "\xE2\x96\xB1", 1130.0,
                                        894.0, 48.0, 36.0, 30.0, kInk, kSymbol,
                                        "center");
    builder.TextIntro(bookmark, 46, 10.0);

    json& disc = builder.EllipseLayer("tk_disc", 1146.0, 946.0, 76.0, 76.0,
                                       "#18181F");
    WithStroke(disc, "#3A3A3F", 2.0);
    builder.Intro(disc, 48, 8.0, 0.8);
    json& note = builder.TextLayer("tk_note", "\xE2\x99\xAB", 1160.0, 960.0,
                                    48.0, 48.0, 22.0, spec.secondary, kSymbol,
                                    "center");
    builder.Animate(note, json::array({
                               ScalarTrack("rotation_z", "linear",
                                           {{0, 0.0}, {builder.frames() - 1, 360.0}}),
                           }));

    json& handle = builder.TextLayer("tk_handle", spec.handle, 720.0, 766.0,
                                      380.0, 34.0, 26.0, kInk, kBold);
    builder.TextIntro(handle, 24);
    json& caption = builder.TextLayer("tk_caption", spec.caption, 720.0, 804.0,
                                       380.0, 62.0, 21.0, kInk);
    builder.TextIntro(caption, 28);

    json& burst = builder.TextLayer("tk_burst_heart", "\xE2\x99\xA5", 884.0,
                                     394.0, 152.0, 152.0, 140.0, "#FF4D6D",
                                     kSymbol, "center");
    builder.Animate(burst, json::array({
                                 ScalarTrack("opacity", "out_cubic",
                                             {{0, 0.0},
                                              {98, 0.0},
                                              {104, 1.0},
                                              {124, 1.0},
                                              {146, 0.0}}),
                                 VectorTrack("scale", "out_back",
                                             {{98, {0.20, 0.20}},
                                              {108, {1.35, 1.35}},
                                              {120, {1.05, 1.05}},
                                              {130, {0.90, 0.90}}}),
                                 ScalarTrack("rotation_z", "out_back",
                                             {{98, -14.0}, {112, 8.0}, {126, 0.0}}),
                             }));
}
}  // namespace chrononplugin::socials::social_detail
