// YouTube / comment highlight composition. Split out of social_showcase.cpp.

#include "social_showcase_internal.hpp"

#include <string>

namespace chrononplugin::socials::social_detail {

void AddYouTubeComment(Builder& builder, const YouTubeSpec& spec) {
    const int last = builder.frames() - 1;
    builder.Backdrop(spec.background, spec.accent, "#3B0A0A");
    builder.Chrome("YOUTUBE / COMMENT HIGHLIGHT", spec.accent);

    json& thumb = builder.ShapeLayer("yt_thumbnail", 220.0, 120.0, 1200.0,
                                     620.0, "#15171B", 24.0);
    WithStroke(thumb, "#2C2F35", 1.5);
    builder.Intro(thumb, 0, 32.0, 0.96);

    json& eyebrow = builder.TextLayer("yt_thumb_eyebrow", spec.thumbnail_eyebrow,
                                       270.0, 168.0, 760.0, 92.0, 76.0, kInk,
                                       kBlack);
    builder.TextIntro(eyebrow, 16, 18.0);

    json& play = builder.EllipseLayer("yt_play", 780.0, 390.0, 100.0, 100.0,
                                      "#000000", 0.55);
    WithStroke(play, "#FFFFFF", 2.0);
    builder.Intro(play, 26, 10.0, 0.75);
    json& play_glyph = builder.TextLayer("yt_play_glyph", "\xE2\x96\xB6",
                                          780.0, 390.0, 100.0, 100.0, 34.0,
                                          "#FFFFFF", kSymbol, "center");
    builder.FadeIn(play_glyph, 30, 8);

    json& progress = builder.ShapeLayer("yt_progress_fill", 240.0, 760.0, 1160.0,
                                        6.0, spec.accent, 3.0);
    builder.GrowX(progress, 1160.0, 12, last - 2, 0.03, 0.86);

    json& channel_avatar = builder.EllipseLayer("yt_channel_avatar", 240.0, 790.0,
                                                 64.0, 64.0, "#7C3AED");
    builder.Intro(channel_avatar, 18, 10.0, 0.7);

    json& channel = builder.TextLayer("yt_channel", spec.channel + "  \xE2\x9C\x93",
                                       322.0, 786.0, 620.0, 32.0, 25.0, kInk,
                                       kSymbol);
    builder.TextIntro(channel, 20);
    json& meta = builder.TextLayer("yt_meta", spec.meta, 322.0, 822.0, 760.0,
                                    26.0, 19.0, "#AAAAAA");
    builder.TextIntro(meta, 24);
    json& title = builder.TextLayer("yt_title", spec.title, 240.0, 860.0,
                                     1040.0, 92.0, 33.0, kInk, kBold);
    builder.TextIntro(title, 28);

    json& dim = builder.ColorLayer("yt_dim", "#000000");
    builder.Animate(dim, json::array({
                              ScalarTrack("opacity", "in_cubic",
                                          {{0, 0.0}, {104, 0.0}, {128, 0.62}}),
                          }));

    json& comment_eyebrow = builder.TextLayer("yt_comment_eyebrow",
                                               "COMMENT HIGHLIGHT", 372.0,
                                               392.0, 500.0, 26.0, 18.0,
                                               spec.accent, kBold);
    builder.TextIntro(comment_eyebrow, 108, 12.0);

    json& comment_card = builder.ShapeLayer("yt_comment_card", 360.0, 436.0,
                                             1220.0, 292.0, "#0F0F0F", 24.0);
    WithStroke(comment_card, "#3A3A3A", 1.5);
    builder.Intro(comment_card, 112, 40.0, 0.93);

    json& comment_author = builder.TextLayer(
        "yt_comment_author", spec.comment.author + "  -  " + spec.comment.age,
        400.0, 468.0, 700.0, 34.0, 27.0, kInk, kBold);
    builder.TextIntro(comment_author, 118);
    json& comment_body = builder.TextLayer("yt_comment_body", spec.comment.body,
                                            400.0, 550.0, 1040.0, 78.0, 27.0,
                                            kInk);
    builder.TextIntro(comment_body, 122);
    json& comment_actions = builder.TextLayer(
        "yt_comment_actions", "\xE2\x99\xA5 " + spec.comment.likes + "    Rispondi",
        400.0, 652.0, 600.0, 28.0, 21.0, "#AAAAAA", kSymbol);
    builder.TextIntro(comment_actions, 126);
}
}  // namespace chrononplugin::socials::social_detail
