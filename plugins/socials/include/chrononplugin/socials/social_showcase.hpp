#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace chrononplugin::socials {

// The Socials plugin is the Level-C authoring layer for social-media
// structures. It produces chronon.render-plan.v3 JSON only: rounded boxes,
// avatars and media plates are delegated to Chronon3D's native SDF shapes,
// while typography is delegated to the native text layer.
//
// Every showcase clip is a 1920x1080 composition of at most 300 frames
// (10 seconds at 30 fps).
struct PlanOptions {
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int duration_frames = 240;
};

struct SocialIdentity {
    std::string display_name = "Chronon3D";
    std::string handle = "@chronon3d";
    std::string avatar_initial = "C";
    std::string avatar_color = "#1D9BF0";
    bool verified = true;
};

struct XPostSpec {
    SocialIdentity author;
    std::string timestamp = "2h";
    std::vector<std::string> body_lines;
    std::string media_eyebrow = "LIVE PREVIEW";
    std::string media_headline = "SDF - CURVES - SPRINGS";
    std::string media_subline = "un RenderPlan, zero browser";
    std::string replies = "128";
    std::string reposts = "430";
    std::string likes = "2.7K";
    std::string views = "48.2K";
    std::string accent = "#1D9BF0";
    std::string surface = "#15202B";
    std::string background = "#090D12";
};

struct RedditCommentSpec {
    std::string author;
    std::string age;
    std::string body;
    std::string score;
    bool original_poster = false;
};

struct RedditThreadSpec {
    std::string subreddit = "r/Chronon3D";
    std::string author = "u/pierone";
    std::string age = "4h";
    std::string title;
    std::string meta = "128 commenti - 96% upvoted";
    std::string score = "2.4K";
    std::vector<RedditCommentSpec> comments;
    std::string accent = "#FF4500";
    std::string surface = "#161617";
    std::string background = "#0B0A0A";
};

struct YouTubeCommentSpec {
    std::string author = "@frame_by_frame";
    std::string age = "2 mesi fa";
    std::string body;
    std::string likes = "1,4K";
    std::string avatar_initial = "F";
    std::string avatar_color = "#7C3AED";
};

struct YouTubeSpec {
    std::string channel = "Chronon Motion Lab";
    std::string title;
    std::string meta = "18K visualizzazioni - 2 giorni fa";
    std::string thumbnail_eyebrow = "SOCIALS";
    std::string thumbnail_label = "STRUCTURES / 01";
    std::string duration = "8:24";
    std::string accent = "#FF0000";
    std::string background = "#0A0A0A";
    YouTubeCommentSpec comment;
};

struct TikTokSpec {
    std::string creator = "chronon.lab";
    std::string handle = "@chronon.lab";
    std::string caption;
    std::string sound = "Original sound - chronon.lab";
    std::string likes = "24.8K";
    std::string comments = "1.2K";
    std::string shares = "3.4K";
    std::string accent = "#FE2C55";
    std::string secondary = "#25F4EE";
    std::string background = "#08080B";
};

struct SocialShowcaseContent {
    XPostSpec x;
    RedditThreadSpec reddit;
    YouTubeSpec youtube;
    TikTokSpec tiktok;
};

struct ShowcasePlan {
    std::string id;
    std::string platform;
    std::string label;
    std::string plan_file;
    std::string mp4_file;
    std::string drive_name;
    int frames = 0;
    nlohmann::ordered_json plan;
};

// Editable, fictional demo content. No platform capture or real account data
// is involved: every clip is an illustrative reconstruction authored as data.
[[nodiscard]] SocialShowcaseContent DefaultSocialShowcaseContent();

[[nodiscard]] nlohmann::ordered_json BuildXPostPlan(const XPostSpec& spec,
                                                    const PlanOptions& options = {});
[[nodiscard]] nlohmann::ordered_json BuildRedditThreadPlan(const RedditThreadSpec& spec,
                                                           const PlanOptions& options = {});
[[nodiscard]] nlohmann::ordered_json BuildYouTubeCommentPlan(const YouTubeSpec& spec,
                                                             const PlanOptions& options = {});
[[nodiscard]] nlohmann::ordered_json BuildTikTokOverlayPlan(const TikTokSpec& spec,
                                                            const PlanOptions& options = {});

// Four animated showcase clips, ready for chronon3d_cli render.
[[nodiscard]] std::vector<ShowcasePlan> BuildSocialShowcasePlans(
    const SocialShowcaseContent& content, const PlanOptions& options = {});

}  // namespace chrononplugin::socials
