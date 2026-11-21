// Shared layout helpers and RenderPlan V3 validation of the socials showcase.
// Split out of social_showcase.cpp.

#include "social_showcase_internal.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace chrononplugin::socials::social_detail {

json Rgba(const std::string& hex, double alpha) {
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

json OpacityIn(int delay, int duration, double peak) {
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
}  // namespace chrononplugin::socials::social_detail
