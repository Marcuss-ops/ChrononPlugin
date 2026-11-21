// Socials showcase entry points: demo content, the four platform plans and the
// series builder. The builder vocabulary lives in social_showcase_internal.hpp;
// each platform composition has its own translation unit.

#include "chrononplugin/socials/social_showcase.hpp"

#include "social_showcase_internal.hpp"

#include <string>
#include <utility>
#include <vector>

namespace chrononplugin::socials {

using namespace social_detail;

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
