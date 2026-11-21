// Closed encoder contract of the native map renderer. Split out of
// map_plate_render.cpp.

#include "map_plate_render_internal.hpp"

#include <string>
#include <vector>

namespace chrononplugin::maps::map_plate_detail {

bool resolve_encoder_profile(const nlohmann::json& profile, EncoderProfile& out,
                             std::string& error) {
    if (!profile.is_object()) {
        error = "encoder_profile must be an object";
        return false;
    }
    const EncoderProfile given{
        profile.value("codec", ""), profile.value("gpu", false),
        profile.value("preset", ""), profile.value("quality", 0)};
    const bool certified_nvenc = given.codec == "h264_nvenc" && given.gpu &&
        given.preset == "p6" && given.quality == 15;
    const bool certified_fast_x264 = given.codec == "libx264" && !given.gpu &&
        given.preset == "veryfast" && given.quality == 23;
    const bool certified_quality_x264 = given.codec == "libx264" && !given.gpu &&
        given.preset == "slow" && given.quality == 17;
    if (!certified_nvenc && !certified_fast_x264 && !certified_quality_x264) {
        error = "unsupported or incomplete dynamic-map encoder profile";
        return false;
    }
    out = given;
    return true;
}

/// render_dynamic_map_overlay.encoder_arguments (exact argument order).
std::vector<std::string> encoder_arguments(const EncoderProfile& profile) {
    if (profile.gpu) {
        return {"-c:v", profile.codec, "-gpu", "0", "-preset", profile.preset,
                "-tune", "hq", "-rc", "vbr", "-cq", std::to_string(profile.quality),
                "-b:v", "0", "-profile:v", "high", "-level:v", "4.0",
                "-refs", "1", "-g", "48", "-keyint_min", "48",
                "-sc_threshold", "0", "-bf", "0", "-flags", "+cgop",
                "-color_primaries", "bt709", "-color_trc", "bt709",
                "-colorspace", "bt709", "-color_range", "tv"};
    }
    std::vector<std::string> args{"-c:v", profile.codec, "-preset", profile.preset,
                                  "-crf", std::to_string(profile.quality)};
    // Final-job packet-copy uses one canonical AVCDecoderConfigurationRecord.
    // Map overlays are assembled from raw frames here, so pin the same x264
    // parameter sets as the final-job fragment producer instead of inheriting
    // x264 defaults (B-frames, multiple references and no explicit SAR VUI).
    args.insert(args.end(), {
        "-vf", "setsar=1",
        "-profile:v", "high", "-level:v", "4.0", "-refs", "1",
        "-color_primaries", "bt709", "-color_trc", "bt709",
        "-colorspace", "bt709", "-color_range", "tv",
        "-g", "48", "-keyint_min", "48", "-sc_threshold", "0",
        "-bf", "0", "-flags", "+cgop",
    });
    if (profile.preset == "slow" && profile.quality == 17) {
        args.insert(args.end(), {"-maxrate", "16M", "-bufsize", "32M"});
    }
    return args;
}

/// Shell single-quote escaping for the ffmpeg command line.
[[nodiscard]] std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (const char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

std::string map_plate_ffmpeg_command(const MapPlateRequest& request, int fps_num,
                                     int fps_den, const EncoderProfile& profile,
                                     const std::string& staged_video) {
    std::string command = "/usr/bin/ffmpeg -hide_banner -loglevel error -y";
    command += " -f rawvideo -pix_fmt bgr24 -video_size " +
               std::to_string(request.output_width) + "x" +
               std::to_string(request.output_height);
    command += " -framerate " + std::to_string(fps_num) + "/" +
               std::to_string(fps_den);
    command += " -i -";
    for (const std::string& arg : encoder_arguments(profile)) {
        command += " " + arg;
    }
    command += " -pix_fmt yuv420p -movflags +faststart " + shell_quote(staged_video);
    return command;
}

[[nodiscard]] double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}
}  // namespace chrononplugin::maps::map_plate_detail
