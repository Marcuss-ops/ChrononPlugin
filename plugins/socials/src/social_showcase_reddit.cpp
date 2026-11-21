// Reddit / thread cascade composition. Split out of social_showcase.cpp.

#include "social_showcase_internal.hpp"

#include <string>

namespace chrononplugin::socials::social_detail {

void AddRedditThread(Builder& builder, const RedditThreadSpec& spec) {
    builder.Backdrop(spec.background, spec.accent, "#FF8717");
    builder.Chrome("REDDIT / THREAD CASCADE", spec.accent);

    json& card = builder.ShapeLayer("rd_card", 260.0, 110.0, 1400.0, 860.0,
                                    spec.surface, 34.0);
    WithStroke(card, "#303031", 1.5);
    builder.Intro(card, 2, 36.0, 0.95, 1.003);

    json& sub_name = builder.TextLayer("rd_subreddit", spec.subreddit, 392.0,
                                       156.0, 500.0, 38.0, 29.0, kInk, kBold);
    builder.TextIntro(sub_name, 14);

    json& upvote = builder.TextLayer("rd_upvote", "\xE2\x96\xB2", 318.0, 262.0,
                                     64.0, 34.0, 26.0, spec.accent, kSymbol,
                                     "center");
    builder.Animate(upvote, json::array({
                                   VectorTrack("scale", "out_back",
                                               {{0, {1.0, 1.0}},
                                                {150, {1.0, 1.0}},
                                                {157, {1.35, 1.35}},
                                                {169, {1.0, 1.0}}}),
                               }));
    json& score = builder.TextLayer("rd_score", spec.score, 312.0, 298.0, 76.0,
                                     34.0, 23.0, kInk, kBold, "center");
    builder.TextIntro(score, 22);
    builder.TextLayer("rd_downvote", "\xE2\x96\xBC", 318.0, 338.0, 64.0, 34.0,
                      26.0, "#6B7075", kSymbol, "center");

    json& title = builder.TextLayer("rd_title", spec.title, 392.0, 250.0,
                                     1160.0, 112.0, 39.0, kInk, kBold);
    builder.TextIntro(title, 8, 18.0);

    json& spine = builder.ShapeLayer("rd_thread_spine", 420.0, 430.0, 3.0, 500.0,
                                     "#343536");
    builder.GrowY(spine, 500.0, 18, 70);

    json& reply_rail = builder.ShapeLayer("rd_reply_rail", 470.0, 492.0, 3.0,
                                          260.0, "#343536");
    builder.GrowY(reply_rail, 260.0, 34, 86);

    json& c1_avatar = builder.EllipseLayer("rd_c1_avatar", 444.0, 436.0, 52.0,
                                           52.0, "#3B82F6");
    builder.Intro(c1_avatar, 24, 12.0, 0.7);
    json& c1_author = builder.TextLayer("rd_c1_author", "u/motion_arch  -  3h",
                                         512.0, 432.0, 700.0, 28.0, 21.0,
                                         "#D7DADC", kBold);
    builder.TextIntro(c1_author, 28);
    json& c1_body = builder.TextLayer(
        "rd_c1_body",
        "Le thread line che crescono da zero sono il dettaglio che cercavo.",
        512.0, 468.0, 1040.0, 56.0, 24.0, "#D7DADC");
    builder.TextIntro(c1_body, 32);

    json& c2_author = builder.TextLayer("rd_c2_author", "u/pierone  -  2h  -  OP",
                                         590.0, 548.0, 560.0, 26.0, 20.0,
                                         "#D7DADC", kBold);
    builder.TextIntro(c2_author, 40);
    json& c2_body = builder.TextLayer(
        "rd_c2_body", "S\xC3\xAC: una scala su Y con l'origine fissata in alto, tutto keyframe.",
        590.0, 582.0, 940.0, 58.0, 23.0, "#D7DADC");
    builder.TextIntro(c2_body, 46);

    json& c3_author = builder.TextLayer("rd_c3_author", "u/render_fox  -  1h",
                                         680.0, 680.0, 700.0, 26.0, 20.0,
                                         "#D7DADC", kBold);
    builder.TextIntro(c3_author, 52);
    json& c3_body = builder.TextLayer(
        "rd_c3_body", "E il pulse dell'upvote? Sembra fisica vera.",
        680.0, 714.0, 830.0, 58.0, 23.0, "#D7DADC");
    builder.TextIntro(c3_body, 56);
}
}  // namespace chrononplugin::socials::social_detail
