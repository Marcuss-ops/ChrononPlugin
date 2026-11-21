#pragma once

// Private to plugins/socials: the level-C RenderPlan V3 builder and the shared
// layout vocabulary of the spotlight showcase. social_showcase.cpp is split
// into builder/plan helpers and one translation unit per platform; only these
// entities cross the split. This header is not part of the public contract.

#include "chrononplugin/socials/social_showcase.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <initializer_list>
#include <string>
#include <utility>

namespace chrononplugin::socials::social_detail {

using json = nlohmann::ordered_json;

inline constexpr int kWidth = 1920;
inline constexpr int kHeight = 1080;
inline constexpr int kFps = 30;
inline constexpr int kMaxFrames = 300;
inline constexpr std::size_t kMaxLayers = 18;

inline constexpr const char* kRegular = "Inter-Regular.ttf";
inline constexpr const char* kBold = "Inter-Bold.ttf";
inline constexpr const char* kSemiBold = "Inter-SemiBold.ttf";
inline constexpr const char* kBlack = "Inter-Black.ttf";
inline constexpr const char* kSymbol = "DejaVuSans.ttf";

inline constexpr const char* kMuted = "#8B949E";
inline constexpr const char* kSoft = "#C9D1D9";
inline constexpr const char* kInk = "#F7F9F9";
inline constexpr const char* kHairline = "#2F3336";

json Rgba(const std::string& hex, double alpha = 1.0);
json Keyframe(int frame, const json& value);
json ScalarTrack(const std::string& property, const std::string& easing,
                 std::initializer_list<std::pair<int, double>> keys);
json VectorTrack(const std::string& property, const std::string& easing,
                 std::initializer_list<std::pair<int, std::pair<double, double>>> keys);
json OpacityIn(int delay, int duration, double peak = 1.0);
json WithStroke(json& layer, const std::string& color, double width);

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

void AddXPost(Builder& builder, const XPostSpec& spec);
void AddRedditThread(Builder& builder, const RedditThreadSpec& spec);
void AddYouTubeComment(Builder& builder, const YouTubeSpec& spec);
void AddTikTokOverlay(Builder& builder, const TikTokSpec& spec);

void ValidatePlan(const json& plan, const std::string& id);
std::string Seconds(const PlanOptions& options);
std::string OutputName(int index, const std::string& slug,
                       const PlanOptions& options);
std::string FileName(const std::string& path);
ShowcasePlan MakeShowcasePlan(std::string id, std::string platform,
                              std::string label, std::string plan_file,
                              std::string mp4_file, std::string drive_name,
                              const PlanOptions& options, json plan);

}  // namespace chrononplugin::socials::social_detail
