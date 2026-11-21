// GPU map-render prototype — headless EGL + OpenGL ES 3.2.
//
// This is deliberately independent from the OpenCV CPU map compositor:
//   * render target: RGBA8 FBO + GL_DEPTH_COMPONENT24 depth renderbuffer;
//   * 2.5D: instanced extruded blocks, perspective camera, hardware depth test;
//   * particles: GPU agents in an SSBO updated by a compute shader, rendered
//     as additive GPU line trails and glow points;
//   * output: glReadPixels -> FFmpeg/NVENC pipe.
//
// No OpenCV, no CPU raster fallback, no synthetic depth. The only CPU image
// work is the small HUD bitmap font drawn after readback.
//
// This unit is the CLI: option parsing and the self-test/render entry point.
// The renderer lives in map_gpu_renderer.cpp and the EGL/GL helpers in
// map_gpu_gl.cpp.

#include "map_gpu_renderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

using namespace chrononplugin::maps::gpu_detail;

namespace {

struct Options {
    std::string input = "plugins/maps/showcases/assets/jerusalem.geojson";
    std::string feature = "Jerusalem";
    std::string scene = "volumetric";
    std::string output;
    std::string summary;
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int frames = 120;
    bool self_test = false;
};

void usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s --scene volumetric|particles --output out.mp4\n"
        "       [--input geojson] [--feature name] [--width 1920]\n"
        "       [--height 1080] [--fps 30] [--frames 120]\n"
        "       [--summary-output out.json]\n"
        "       %s --self-test\n",
        argv0, argv0);
}

bool parse_options(int argc, char** argv, Options& options, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                error = std::string("missing value for ") + name;
                return {};
            }
            return argv[++i];
        };
        if (argument == "--self-test") {
            options.self_test = true;
        } else if (argument == "--input") {
            options.input = next("--input");
        } else if (argument == "--feature") {
            options.feature = next("--feature");
        } else if (argument == "--scene") {
            options.scene = next("--scene");
        } else if (argument == "--output") {
            options.output = next("--output");
        } else if (argument == "--summary-output") {
            options.summary = next("--summary-output");
        } else if (argument == "--width") {
            options.width = std::atoi(next("--width").c_str());
        } else if (argument == "--height") {
            options.height = std::atoi(next("--height").c_str());
        } else if (argument == "--fps") {
            options.fps = std::atoi(next("--fps").c_str());
        } else if (argument == "--frames") {
            options.frames = std::atoi(next("--frames").c_str());
        } else if (argument == "--help" || argument == "-h") {
            usage(argv[0]);
            std::exit(0);
        } else {
            error = "unknown argument: " + argument;
            return false;
        }
        if (!error.empty()) return false;
    }
    if (options.self_test) return true;
    if (options.scene != "volumetric" && options.scene != "particles") {
        error = "--scene must be volumetric or particles";
        return false;
    }
    if (options.output.empty()) {
        error = "--output is required for a render";
        return false;
    }
    if (options.width <= 0 || options.height <= 0 || options.fps <= 0 ||
        options.frames < 2) {
        error = "width, height and fps must be positive and frames must be >= 2";
        return false;
    }
    if (options.summary.empty()) options.summary = options.output + ".gpu.json";
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    std::string error;
    if (!parse_options(argc, argv, options, error)) {
        std::fprintf(stderr, "map-gpu: %s\n", error.c_str());
        usage(argv[0]);
        return 2;
    }

    if (options.self_test) {
        EglContext probe;
        if (!probe.init(error)) {
            std::fprintf(stderr, "GPU_SELF_TEST_SKIP %s\n", error.c_str());
            return 77;
        }
        GLint invocations = 0;
        glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &invocations);
        std::printf("GPU_SELF_TEST_PASS renderer=%s version=%s compute_invocations=%d\n",
                    probe.renderer.c_str(), probe.version.c_str(), invocations);
        return 0;
    }

    try {
        std::filesystem::path output_path(options.output);
        if (output_path.has_parent_path())
            std::filesystem::create_directories(output_path.parent_path());
        std::filesystem::path summary_path(options.summary);
        if (summary_path.has_parent_path())
            std::filesystem::create_directories(summary_path.parent_path());

        GpuRenderer renderer;
        renderer.width = options.width;
        renderer.height = options.height;
        if (!renderer.init(options.input, options.feature, error)) {
            std::fprintf(stderr, "map-gpu: %s\n", error.c_str());
            return 1;
        }
        if (!renderer.render(options.scene, options.output, options.summary,
                             options.frames, options.fps, error)) {
            std::fprintf(stderr, "map-gpu: %s\n", error.c_str());
            return 1;
        }
    } catch (const std::exception& exc) {
        std::fprintf(stderr, "map-gpu: %s\n", exc.what());
        return 1;
    }
    return 0;
}
