#include "chrononplugin/socials/social_showcase.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chrononplugin::socials {
namespace {

using json = nlohmann::ordered_json;

constexpr int kWidth = 1920;
constexpr int kHeight = 1080;
constexpr int kFps = 30;
constexpr int kMaxFrames = 300;
constexpr std::size_t kMaxLayers = 18;

constexpr const char* kRegular = "Inter-Regular.ttf";
constexpr const char* kBold = "Inter-Bold.ttf";
constexpr const char* kSemiBold = "Inter-SemiBold.ttf";
constexpr const char* kBlack = "Inter-Black.ttf";
constexpr const char* kSymbol = "DejaVuSans.ttf";

constexpr const char* kMuted = "#8B949E";
constexpr const char* kSoft = "#C9D1D9";
constexpr const char* kInk = "#F7F9F9";
constexpr const char* kHairline = "#2F3336";

json Rgba(const std::string& hex, double alpha = 1.0) {
    if (hex.size() != 7 || hex.front() != '#') {
        throw std::invalid_argument("socials: colors must use #RRGGBB");
    }
    const auto channel = [&hex](std::size_t offset) {
        return std::stoi(hex.substr(offset, 2), nullptr, 16) / 255.0;
    };
    return json::array({channel(1), channel(3), channel(5), alpha});
}

json Keyframe(int frame, const json& value) {
    return json{{"frame", frame}, {"value", value}};
}

json ScalarTrack(const std::string& property, const std::string& easing,
                 std::initializer_list<std::pair<int, double>> keys) {
    json frames = json::array();
    for (const auto& [frame, value] : keys) frames.push_back(Keyframe(frame, value));
    return json{{"property", property}, {"easing", easing}, {"keyframes", frames}};
}

json VectorTrack(const std::string& property, const std::string& easing,
                 std::initializer_list<std::pair<int, std::pair<double, double>>> keys) {
    json frames = json::array();
    for (const auto& [frame, value] : keys) {
        frames.push_back(Keyframe(frame, json::array({value.first, value.second})));
    }
    return json{{"property", property}, {"easing", easing}, {"keyframes", frames}};
}

json OpacityIn(int delay, int duration, double peak = 1.0) {
    json keys = json::array();
    if (delay > 0) keys.push_back(Keyframe(0, 0.0));
    keys.push_back(Keyframe(delay, 0.0));
    keys.push_back(Keyframe(delay + duration, peak));
    return json{{"property", "opacity"}, {"easing", "out_cubic"}, {"keyframes", keys}};
}

json WithStroke(json& layer, const std::string& color, double width) {
    layer.at("shape")["stroke"] = json{{"color", color}, {"width", width}};
    return layer;
}

class Builder {
public:
    explicit Builder(PlanOptions options) : options_(options) {
        if (options_.width != kWidth || options_.height != kHeight) {
            throw std::invalid_argument("socials: showcase canvas must be 1920x1080");
        }
        if (options_.fps != kFps) {
            throw std::invalid_argument("socials: showcase canvas must run at 30 fps");
        }
        if (options_.duration_frames < 60 || options_.duration_frames > kMaxFrames) {
            throw std::invalid_argument("socials: duration must be within 2..10 seconds");
        }
    }

    [[nodiscard]] int frames() const { return options_.duration_frames; }

    json& Add(json layer) {
        layers_.push_back(std::move(layer));
        return layers_.back();
    }

    void Animate(json& layer, json tracks) const {
        layer["animation"] = json{{"tracks", std::move(tracks)}};
    }

    json& ColorLayer(const std::string& id, const std::string& fill,
                     double alpha = 1.0) {
        json layer = {
            {"id", id},
            {"type", "color"},
            {"color", Rgba(fill, alpha)},
            {"size", json::array({options_.width, options_.height})},
            {"position", json::array({0.0, 0.0})},
            {"start_frame", 0},
            {"duration_frames", frames()},
        };
        return Add(std::move(layer));
    }

    json& ShapeLayer(const std::string& id, double left, double top, double width,
                     double height, const std::string& fill, double radius = 0.0,
                     double alpha = 1.0) {
        json shape = {
            {"type", radius > 0.0 ? "rounded_rect" : "rect"},
            {"fill", Rgba(fill, alpha)},
        };
        if (radius > 0.0) shape["radius"] = radius;
        json layer = {
            {"id", id},
            {"type", "shape"},
            {"shape", std::move(shape)},
            {"size", json::array({width, height})},
            // Procedural shapes are world-space layers: [0,0] is the canvas
            // centre and one world unit maps to one pixel. Text layers are
            // screen-space, so only shapes need the centre offset.
            {"position", json::array({left + width / 2.0 - kWidth / 2.0,
                                      top + height / 2.0 - kHeight / 2.0})},
            {"start_frame", 0},
            {"duration_frames", frames()},
        };
        return Add(std::move(layer));
    }

    json& EllipseLayer(const std::string& id, double left, double top, double width,
                       double height, const std::string& fill, double alpha = 1.0) {
        json layer = {
            {"id", id},
            {"type", "shape"},
            {"shape", json{{"type", "ellipse"}, {"fill", Rgba(fill, alpha)}}},
            {"size", json::array({width, height})},
            {"position", json::array({left + width / 2.0 - kWidth / 2.0,
                                      top + height / 2.0 - kHeight / 2.0})},
            {"start_frame", 0},
            {"duration_frames", frames()},
        };
        return Add(std::move(layer));
    }

    json& TextLayer(const std::string& id, const std::string& value, double left,
                    double top, double width, double height, double font_size,
                    const std::string& fill, const std::string& font = kRegular,
                    const std::string& align = "left") {
        json layer = {
            {"id", id},
            {"type", "text"},
            {"text", value},
            {"size", json::array({width, height})},
            {"position", json::array({left + width / 2.0, top + height / 2.0})},
            {"start_frame", 0},
            {"duration_frames", frames()},
            {"style", json{
                          {"font", std::string("assets/fonts/") + font},
                          {"font_size", font_size},
                          {"fill", fill},
                          {"text_align", align},
                      }},
        };
        return Add(std::move(layer));
    }

    void Intro(json& layer, int delay, double slide = 24.0,
               double from_scale = 0.96, double end_scale = 1.0) const {
        json scale_keys = json::array();
        scale_keys.push_back(Keyframe(0, json::array({from_scale, from_scale})));
        scale_keys.push_back(Keyframe(delay + 20, json::array({1.0, 1.0})));
        if (std::abs(end_scale - 1.0) > 1e-6 && frames() - 1 > delay + 22) {
            scale_keys.push_back(Keyframe(frames() - 1, json::array({end_scale, end_scale})));
        }
        Animate(layer, json::array({
                            OpacityIn(delay, 12),
                            ScalarTrack("position_y", "out_back",
                                        {{0, slide}, {delay + 18, 0.0}}),
                            json{{"property", "scale"},
                                 {"easing", "out_back"},
                                 {"keyframes", scale_keys}},
                        }));
    }

    void TextIntro(json& layer, int delay, double slide = 14.0) const {
        Animate(layer, json::array({
                            OpacityIn(delay, 10),
                            ScalarTrack("position_y", "out_cubic",
                                        {{0, slide}, {delay + 12, 0.0}}),
                        }));
    }

    void FadeIn(json& layer, int delay, int duration = 12,
                double peak = 1.0) const {
        Animate(layer, json::array({OpacityIn(delay, duration, peak)}));
    }

    void GrowY(json& layer, double height, int start, int end) const {
        Animate(layer, json::array({
                            ScalarTrack("scale_y", "out_expo", {{start, 0.02}, {end, 1.0}}),
                            ScalarTrack("position_y", "out_expo",
                                        {{start, -height / 2.0 * 0.98}, {end, 0.0}}),
                        }));
    }

    void GrowX(json& layer, double width, int start, int end, double from,
               double to, const std::string& easing = "linear") const {
        Animate(layer, json::array({
                            ScalarTrack("scale_x", easing, {{start, from}, {end, to}}),
                            ScalarTrack("position_x", easing,
                                        {{start, -width / 2.0 * (1.0 - from)},
                                         {end, -width / 2.0 * (1.0 - to)}}),
                        }));
    }

    void Backdrop(const std::string& background, const std::string& accent,
                  const std::string& secondary) {
        (void)accent;
        (void)secondary;
        json& base = ColorLayer("backdrop", background);
        base["effects"] = json::array({
            json{{"type", "vignette"},
                 {"radius", 0.72},
                 {"softness", 0.78},
                 {"amount", 0.44},
                 {"color", Rgba("#000000")}},
        });
    }

    void Chrome(const std::string& section, const std::string& accent) {
        (void)accent;
        TextLayer("chrome_section", "CHRONONPLUGIN / SOCIALS  -  " + section,
                  80.0, 36.0, 1000.0, 28.0, 19.0, kMuted, kBold);
    }

    void FinalFade() {
        json& fade = ColorLayer("end_fade", "#000000");
        Animate(fade, json::array({
                            ScalarTrack("opacity", "in_cubic",
                                        {{0, 0.0},
                                         {frames() - 14, 0.0},
                                         {frames() - 1, 0.92}}),
                        }));
    }

    [[nodiscard]] json Finish(const std::string& job_id,
                              const std::string& output) {
        json plan = {
            {"schema", "chronon.render-plan.v3"},
            {"version", 3},
            {"job_id", job_id},
            {"canvas",
             json{{"width", options_.width},
                  {"height", options_.height},
                  {"fps_num", options_.fps},
                  {"fps_den", 1},
                  {"duration_frames", frames()}}},
            {"layers", std::move(layers_)},
            {"output", json{{"path", output}, {"format", "mp4"}, {"codec", "h264"}}},
        };
        return plan;
    }

private:
    PlanOptions options_;
    json layers_ = json::array();
};

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

void ValidatePlan(const json& plan, const std::string& id) {
    if (plan.at("schema") != "chronon.render-plan.v3" ||
        plan.at("version") != 3) {
        throw std::runtime_error("socials: " + id + " is not a RenderPlan V3");
    }
    const json& canvas = plan.at("canvas");
    if (canvas.at("width") != kWidth || canvas.at("height") != kHeight ||
        canvas.at("fps_num") != kFps) {
        throw std::runtime_error("socials: " + id + " has an invalid canvas");
    }
    if (canvas.at("duration_frames") < 60 ||
        canvas.at("duration_frames") > kMaxFrames) {
        throw std::runtime_error("socials: " + id + " exceeds 10 seconds");
    }
    if (plan.at("layers").size() > kMaxLayers) {
        throw std::runtime_error("socials: " + id +
                                 " exceeds the 18-layer native surface budget");
    }
    if (plan.at("layers").empty()) {
        throw std::runtime_error("socials: " + id + " has no layers");
    }
    for (const json& layer : plan.at("layers")) {
        if (!layer.contains("id") || !layer.contains("type")) {
            throw std::runtime_error("socials: " + id + " has an incomplete layer");
        }
        if (layer.at("type") == "text") {
            const std::string font = layer.at("style").at("font");
            if (font.rfind("assets/fonts/", 0) != 0) {
                throw std::runtime_error("socials: " + id +
                                         " references a non-portable font path");
            }
        }
    }
}

std::string Seconds(const PlanOptions& options) {
    return std::to_string(
        static_cast<int>(std::lround(options.duration_frames /
                                     static_cast<double>(options.fps))));
}

std::string OutputName(int index, const std::string& slug,
                       const PlanOptions& options) {
    const std::string prefix =
        index < 10 ? "0" + std::to_string(index) : std::to_string(index);
    return "mp4/Socials_" + prefix + "_" + slug + "_1920x1080_" +
           Seconds(options) + "s.mp4";
}

std::string FileName(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

ShowcasePlan MakeShowcasePlan(std::string id, std::string platform,
                              std::string label, std::string plan_file,
                              std::string mp4_file, std::string drive_name,
                              const PlanOptions& options, json plan) {
    ValidatePlan(plan, id);
    ShowcasePlan entry;
    entry.id = std::move(id);
    entry.platform = std::move(platform);
    entry.label = std::move(label);
    entry.plan_file = std::move(plan_file);
    entry.mp4_file = std::move(mp4_file);
    entry.drive_name = std::move(drive_name);
    entry.frames = options.duration_frames;
    entry.plan = std::move(plan);
    return entry;
}

}  // namespace

SocialShowcaseContent DefaultSocialShowcaseContent() {
    SocialShowcaseContent content;

    content.x.author = {"Chronon3D", "@chronon3d", "C", "#1D9BF0", true};
    content.x.timestamp = "2h";
    content.x.body_lines = {
        "Le card social ora vivono dentro Chronon3d.",
        "Niente browser, niente screenshot:",
        "ogni pixel \xC3\xA8 un layer animabile.",
    };
    content.x.media_eyebrow = "LIVE PREVIEW";
    content.x.media_headline = "SDF - CURVES - SPRINGS";
    content.x.media_subline = "un RenderPlan, zero browser";
    content.x.replies = "128";
    content.x.reposts = "430";
    content.x.likes = "2.7K";
    content.x.views = "48.2K";

    content.reddit.subreddit = "r/Chronon3D";
    content.reddit.author = "u/pierone";
    content.reddit.age = "4h";
    content.reddit.title =
        "Ho ricostruito una thread Reddit\ncon layer e curve native";
    content.reddit.meta = "128 commenti  -  96% upvoted";
    content.reddit.score = "2.4K";
    content.reddit.comments = {
        {"u/motion_arch", "3h",
         "Le thread line che crescono da zero sono il dettaglio che cercavo.",
         "842", false},
        {"u/pierone", "2h",
         "S\xC3\xAC: una scala su Y con l'origine fissata in alto, tutto keyframe.",
         "311", true},
        {"u/render_fox", "1h",
         "E il pulse dell'upvote? Sembra fisica vera.", "97", false},
    };

    content.youtube.channel = "Chronon Motion Lab";
    content.youtube.title =
        "Social structures: animare le card\ncon RenderPlan V3";
    content.youtube.meta = "18K visualizzazioni  -  2 giorni fa";
    content.youtube.thumbnail_eyebrow = "SOCIALS";
    content.youtube.thumbnail_label = "STRUCTURES / 01";
    content.youtube.duration = "8:24";
    content.youtube.comment = {
        "@frame_by_frame", "2 mesi fa",
        "Questa \xC3\xA8 la prima volta che una card social\nsembra motion design, non un widget.",
        "1,4K", "F", "#7C3AED"};

    content.tiktok.creator = "chronon.lab";
    content.tiktok.handle = "@chronon.lab";
    content.tiktok.caption =
        "POV: animi una card social senza browser\n#chronon3d #motiondesign";
    content.tiktok.sound = "Original sound - chronon.lab";
    content.tiktok.likes = "24.8K";
    content.tiktok.comments = "1.2K";
    content.tiktok.shares = "3.4K";
    return content;
}

json BuildXPostPlan(const XPostSpec& spec, const PlanOptions& options) {
    Builder builder(options);
    AddXPost(builder, spec);
    builder.FinalFade();
    return builder.Finish("socials_x_post",
                          OutputName(1, "X_Post", options));
}

json BuildRedditThreadPlan(const RedditThreadSpec& spec,
                           const PlanOptions& options) {
    Builder builder(options);
    AddRedditThread(builder, spec);
    builder.FinalFade();
    return builder.Finish("socials_reddit_thread",
                          OutputName(2, "Reddit_Thread", options));
}

json BuildYouTubeCommentPlan(const YouTubeSpec& spec,
                             const PlanOptions& options) {
    Builder builder(options);
    AddYouTubeComment(builder, spec);
    builder.FinalFade();
    return builder.Finish("socials_youtube_comment",
                          OutputName(3, "YouTube_Comment", options));
}

json BuildTikTokOverlayPlan(const TikTokSpec& spec, const PlanOptions& options) {
    Builder builder(options);
    AddTikTokOverlay(builder, spec);
    builder.FinalFade();
    return builder.Finish("socials_tiktok_overlay",
                          OutputName(4, "TikTok_Overlay", options));
}

std::vector<ShowcasePlan> BuildSocialShowcasePlans(
    const SocialShowcaseContent& content, const PlanOptions& options) {
    std::vector<ShowcasePlan> plans;
    plans.reserve(4);
    const std::string x_mp4 = OutputName(1, "X_Post", options);
    const std::string reddit_mp4 = OutputName(2, "Reddit_Thread", options);
    const std::string youtube_mp4 = OutputName(3, "YouTube_Comment", options);
    const std::string tiktok_mp4 = OutputName(4, "TikTok_Overlay", options);
    plans.push_back(MakeShowcasePlan(
        "x_post", "x", "X / post card", "plans/01_x_post.plan.json",
        x_mp4, FileName(x_mp4), options, BuildXPostPlan(content.x, options)));
    plans.push_back(MakeShowcasePlan(
        "reddit_thread", "reddit", "Reddit / thread cascade",
        "plans/02_reddit_thread.plan.json", reddit_mp4, FileName(reddit_mp4),
        options, BuildRedditThreadPlan(content.reddit, options)));
    plans.push_back(MakeShowcasePlan(
        "youtube_comment", "youtube", "YouTube / comment highlight",
        "plans/03_youtube_comment.plan.json", youtube_mp4,
        FileName(youtube_mp4), options,
        BuildYouTubeCommentPlan(content.youtube, options)));
    plans.push_back(MakeShowcasePlan(
        "tiktok_overlay", "tiktok", "TikTok / vertical overlay",
        "plans/04_tiktok_overlay.plan.json", tiktok_mp4, FileName(tiktok_mp4),
        options, BuildTikTokOverlayPlan(content.tiktok, options)));
    return plans;
}

}  // namespace chrononplugin::socials
