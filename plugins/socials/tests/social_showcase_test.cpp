#include "chrononplugin/socials/motion.hpp"
#include "chrononplugin/socials/social_showcase.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {

using chrononplugin::socials::AnimProperty;
using chrononplugin::socials::BuildSocialShowcasePlans;
using chrononplugin::socials::DefaultSocialShowcaseContent;
using chrononplugin::socials::EaseOutBack;
using chrononplugin::socials::EaseOutExpo;
using chrononplugin::socials::Easing;
using chrononplugin::socials::PlanOptions;
using json = nlohmann::ordered_json;

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

const json* FindTrack(const json& layer, const std::string& property) {
    if (!layer.contains("animation")) return nullptr;
    for (const json& track : layer.at("animation").at("tracks")) {
        if (track.at("property") == property) return &track;
    }
    return nullptr;
}

double TrackValue(const json& track, std::size_t index) {
    const json& value = track.at("keyframes").at(index).at("value");
    if (value.is_array()) return value.at(0).get<double>();
    return value.get<double>();
}

const json* FindLayer(const json& plan, const std::string& id) {
    for (const json& layer : plan.at("layers")) {
        if (layer.at("id") == id) return &layer;
    }
    return nullptr;
}

void TestMotion() {
    Require(std::abs(EaseOutExpo(0.0f)) < 1e-6f, "EaseOutExpo must start at 0");
    Require(std::abs(EaseOutExpo(1.0f) - 1.0f) < 1e-6f,
            "EaseOutExpo must end at 1");
    Require(EaseOutBack(0.55f) > 1.0f, "EaseOutBack must overshoot");
    Require(std::abs(EaseOutBack(0.0f)) < 1e-6f, "EaseOutBack must start at 0");
    Require(std::abs(EaseOutBack(1.0f) - 1.0f) < 1e-6f,
            "EaseOutBack must end at 1");

    AnimProperty property;
    property.start_value = 10.0f;
    property.target_value = 20.0f;
    property.start_time = 1.0f;
    property.duration = 2.0f;
    property.easing = Easing::EaseOutExpo;
    Require(std::abs(property.Evaluate(0.5f) - 10.0f) < 1e-6f,
            "AnimProperty must hold before the tween");
    Require(std::abs(property.Evaluate(3.5f) - 20.0f) < 1e-6f,
            "AnimProperty must hold after the tween");
    const float middle = property.Evaluate(2.0f);
    Require(middle > 10.0f && middle < 20.0f,
            "AnimProperty must interpolate inside the tween");

    Require(chrononplugin::socials::RenderPlanEasingName(Easing::EaseOutExpo) ==
                "out_expo",
            "out_expo wire name changed");
    Require(chrononplugin::socials::RenderPlanEasingName(Easing::EaseOutBack) ==
                "out_back",
            "out_back wire name changed");
    Require(chrononplugin::socials::RenderPlanEasingName(Easing::Spring) ==
                "spring",
            "spring wire name changed");
    std::cout << "PASS motion\n";
}

void ValidatePlan(const json& plan, const std::string& id, int frames) {
    Require(plan.at("schema") == "chronon.render-plan.v3",
            id + ": wrong schema");
    Require(plan.at("version") == 3, id + ": wrong version");
    const json& canvas = plan.at("canvas");
    Require(canvas.at("width") == 1920 && canvas.at("height") == 1080,
            id + ": canvas is not 1920x1080");
    Require(canvas.at("fps_num") == 30 && canvas.at("fps_den") == 1,
            id + ": canvas is not 30 fps");
    Require(canvas.at("duration_frames") == frames, id + ": frame mismatch");
    Require(frames <= 300, id + ": longer than 10 seconds");
    Require(plan.at("layers").size() > 12 && plan.at("layers").size() <= 18,
            id + ": expected 13..18 layers for the native surface budget");

    std::set<std::string> ids;
    std::size_t text_layers = 0;
    std::size_t shape_layers = 0;
    for (const json& layer : plan.at("layers")) {
        const std::string layer_id = layer.at("id");
        Require(ids.insert(layer_id).second, id + ": duplicate layer " + layer_id);
        Require(layer.at("start_frame") == 0, id + ": " + layer_id + " start frame");
        Require(layer.at("duration_frames") == frames,
                id + ": " + layer_id + " duration");
        const std::string type = layer.at("type");
        Require(type == "text" || type == "shape" || type == "color",
                id + ": unsupported layer type " + type);
        if (type == "text") {
            ++text_layers;
            const std::string font = layer.at("style").at("font");
            Require(font.rfind("assets/fonts/", 0) == 0,
                    id + ": " + layer_id + " has a non-portable font");
            Require(layer.at("style").at("font_size").get<double>() >= 13.0,
                    id + ": " + layer_id + " font is too small");
        }
        if (type == "shape") {
            ++shape_layers;
            const std::string shape_type = layer.at("shape").at("type");
            Require(shape_type == "rect" || shape_type == "rounded_rect" ||
                        shape_type == "ellipse",
                    id + ": unsupported SDF shape " + shape_type);
        }
        if (layer.contains("animation")) {
            for (const json& track : layer.at("animation").at("tracks")) {
                const std::string property = track.at("property");
                Require(property == "opacity" || property == "position_x" ||
                            property == "position_y" || property == "scale" ||
                            property == "scale_x" || property == "scale_y" ||
                            property == "rotation_z",
                        id + ": unsupported track " + property);
                int previous = -1;
                for (const json& keyframe : track.at("keyframes")) {
                    const int frame = keyframe.at("frame");
                    Require(frame >= 0 && frame < frames,
                            id + ": keyframe outside the canvas");
                    Require(frame > previous,
                            id + ": keyframes are not strictly increasing");
                    previous = frame;
                }
            }
        }
    }
    Require(text_layers >= 8, id + ": expected typography layers");
    Require(shape_layers >= 4, id + ": expected SDF surface layers");

    const json& output = plan.at("output");
    Require(output.at("format") == "mp4", id + ": output is not mp4");
    Require(output.at("path").get<std::string>().find("1920x1080") !=
                std::string::npos,
            id + ": output name misses the canvas tag");
}

void TestPlans() {
    const PlanOptions options;
    const auto plans =
        BuildSocialShowcasePlans(DefaultSocialShowcaseContent(), options);
    Require(plans.size() == 4, "expected four showcase plans");

    std::set<std::string> ids;
    for (const auto& entry : plans) {
        Require(ids.insert(entry.id).second, "duplicate clip id");
        Require(entry.frames == 240, "default clip is not 8 seconds");
        Require(entry.mp4_file.find("_1920x1080_8s.mp4") != std::string::npos,
                "default clip name is not 1080p/8s");
        ValidatePlan(entry.plan, entry.id, entry.frames);
    }

    const auto& x = plans.at(0).plan;
    const json* card = FindLayer(x, "x_card");
    Require(card != nullptr, "X card missing");
    const json* card_scale = FindTrack(*card, "scale");
    Require(card_scale != nullptr, "X card scale track missing");
    Require(TrackValue(*card_scale, 0) < 1.0, "X card must scale in");
    Require(TrackValue(*card_scale, 1) == 1.0, "X card must settle at 1.0");

    const auto& reddit = plans.at(1).plan;
    const json* spine = FindLayer(reddit, "rd_thread_spine");
    Require(spine != nullptr, "Reddit thread spine missing");
    const json* spine_scale = FindTrack(*spine, "scale_y");
    Require(spine_scale != nullptr, "Reddit spine scale track missing");
    Require(TrackValue(*spine_scale, 0) < 0.1, "Reddit spine must grow from zero");
    Require(TrackValue(*spine_scale, 1) == 1.0, "Reddit spine must reach full height");

    const auto& youtube = plans.at(2).plan;
    const json* progress = FindLayer(youtube, "yt_progress_fill");
    Require(progress != nullptr, "YouTube progress fill missing");
    const json* progress_scale = FindTrack(*progress, "scale_x");
    Require(progress_scale != nullptr, "YouTube progress track missing");
    Require(TrackValue(*progress_scale, 1) > 0.8,
            "YouTube progress must advance across the clip");

    const auto& tiktok = plans.at(3).plan;
    auto font_of = [](const json& plan, const std::string& layer_id) {
        const json* layer = FindLayer(plan, layer_id);
        Require(layer != nullptr, "missing font layer " + layer_id);
        return layer->at("style").at("font").get<std::string>();
    };
    Require(font_of(x, "x_actions").find("DejaVuSans.ttf") != std::string::npos,
            "X action glyphs must use DejaVuSans");
    Require(font_of(reddit, "rd_upvote").find("DejaVuSans.ttf") != std::string::npos,
            "Reddit arrows must use DejaVuSans");
    Require(font_of(youtube, "yt_play_glyph").find("DejaVuSans.ttf") != std::string::npos,
            "YouTube play glyph must use DejaVuSans");
    Require(font_of(tiktok, "tk_burst_heart").find("DejaVuSans.ttf") != std::string::npos,
            "TikTok heart must use DejaVuSans");

    const json* burst = FindLayer(tiktok, "tk_burst_heart");
    Require(burst != nullptr, "TikTok heart burst missing");
    const json* burst_scale = FindTrack(*burst, "scale");
    Require(burst_scale != nullptr, "TikTok heart scale track missing");
    double peak = 0.0;
    for (std::size_t i = 0; i < burst_scale->at("keyframes").size(); ++i) {
        peak = std::max(peak, TrackValue(*burst_scale, i));
    }
    Require(peak >= 1.3, "TikTok heart must burst above 130%");
    const json* note = FindLayer(tiktok, "tk_note");
    Require(note != nullptr, "TikTok audio note missing");
    const json* spin = FindTrack(*note, "rotation_z");
    Require(spin != nullptr, "TikTok audio disc must rotate");
    Require(TrackValue(*spin, 1) >= 359.0, "TikTok audio disc must complete a turn");

    std::cout << "PASS plans (" << plans.size() << " clips, "
              << plans.at(0).plan.at("layers").size() << " layers, <=18 each)\n";
}

void TestCanvasGuard() {
    bool rejected = false;
    try {
        PlanOptions bad;
        bad.width = 1280;
        bad.height = 720;
        (void)BuildSocialShowcasePlans(DefaultSocialShowcaseContent(), bad);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Require(rejected, "non-1080p canvas must be rejected");

    rejected = false;
    try {
        PlanOptions bad;
        bad.duration_frames = 301;
        (void)BuildSocialShowcasePlans(DefaultSocialShowcaseContent(), bad);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Require(rejected, "clips longer than 10 seconds must be rejected");
    std::cout << "PASS canvas guard\n";
}

}  // namespace

int main() {
    try {
        TestMotion();
        TestPlans();
        TestCanvasGuard();
        std::cout << "ALL SOCIALS TESTS PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SOCIALS TEST FAILURE: " << error.what() << '\n';
        return 1;
    }
}
