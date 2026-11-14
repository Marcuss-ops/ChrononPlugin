// C++ authoring entry point for the Socials showcase.
//
// Usage:
//   chrononplugin_emit_socials_showcase <output-dir>
//
// Writes four chronon.render-plan.v3 JSON files plus series.json. Rendering,
// verification and Drive upload stay in the Python orchestration script.

#include "chrononplugin/socials/social_showcase.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using chrononplugin::socials::BuildSocialShowcasePlans;
using chrononplugin::socials::DefaultSocialShowcaseContent;
using chrononplugin::socials::PlanOptions;
using json = nlohmann::ordered_json;

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void WriteJson(const fs::path& path, const json& value) {
    fs::create_directories(path.parent_path());
    std::ofstream handle(path);
    Require(handle.good(), "cannot write " + path.string());
    handle << value.dump(2) << '\n';
    handle.close();
    Require(handle.good(), "failed writing " + path.string());
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "usage: " << argv[0] << " <output-dir>\n";
            return 2;
        }
        const fs::path out_dir = fs::absolute(argv[1]);
        const PlanOptions options;

        const auto plans = BuildSocialShowcasePlans(DefaultSocialShowcaseContent(),
                                                    options);
        Require(plans.size() == 4, "expected four showcase clips");

        // The Drive root is a machine binding, not a C++ default: it comes
        // from refactored/config.yaml through stack.env / ops/lib/common.sh.
        const char* drive_root_env = std::getenv("VELOX_DRIVE_OVERLAY_ROOT");
        Require(drive_root_env && *drive_root_env,
                "VELOX_DRIVE_OVERLAY_ROOT is not set; source stack.env first");
        const std::string drive_root = drive_root_env;
        const std::string drive_url =
            "https://drive.google.com/drive/u/1/folders/" + drive_root;

        json series = {
            {"schema", "chrononplugin.socials-showcase.v1"},
            {"generator", "chrononplugin_emit_socials_showcase"},
            {"canvas", json{{"width", options.width},
                            {"height", options.height},
                            {"fps", options.fps}}},
            {"stack",
             json{{"authoring", "C++ RenderPlan V3 social layouts"},
                  {"surfaces", "native SDF rounded_rect / ellipse layers (18-layer budget)"},
                  {"motion", "opacity, position, scale, rotation_z, progress tracks"},
                  {"typography", "native Inter text + DejaVu symbol glyphs"},
                  {"backend", "vulkan / require_gpu_native / nvenc"}}},
            {"drive",
             json{{"folder_id", drive_root},
                  {"folder_url", drive_url},
                  {"placement", "root"}}},
            {"clips", json::array()},
        };

        for (const auto& plan : plans) {
            const fs::path plan_path = out_dir / plan.plan_file;
            WriteJson(plan_path, plan.plan);
            series["clips"].push_back(json{
                {"id", plan.id},
                {"platform", plan.platform},
                {"label", plan.label},
                {"plan", plan.plan_file},
                {"mp4", plan.mp4_file},
                {"drive_name", plan.drive_name},
                {"frames", plan.frames},
                {"duration_s",
                 static_cast<int>(plan.frames / static_cast<double>(options.fps))},
            });
            std::cout << plan_path.string() << "  " << plan.frames << "f  "
                      << plan.mp4_file << '\n';
        }

        const fs::path series_path = out_dir / "plans" / "series.json";
        WriteJson(series_path, series);
        std::cout << series_path.string() << "  " << plans.size()
                  << " clips, all authored in C++\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "socials showcase: " << error.what() << '\n';
        return 1;
    }
}
