#include "chrononplugin/maps/map_story_effects.hpp"

#include "chrononplugin/maps/map_camera.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace chrononplugin::maps {
namespace {
constexpr double kPi = 3.14159265358979323846;
const cv::Scalar kAmber(35, 196, 255);
const cv::Scalar kCyan(255, 222, 107);

void line(cv::Mat& frame, cv::Point a, cv::Point b, cv::Scalar color,
          int thickness = 1) {
    cv::line(frame, a, b, color, thickness, cv::LINE_AA);
}

void draw_bracket(cv::Mat& frame, cv::Point center, int radius, int length,
                  cv::Scalar color) {
    for (int sx : {-1, 1}) for (int sy : {-1, 1}) {
        const cv::Point corner(center.x + sx * radius, center.y + sy * radius);
        line(frame, corner, {corner.x - sx * length, corner.y}, color, 2);
        line(frame, corner, {corner.x, corner.y - sy * length}, color, 2);
    }
}

void put_hud(cv::Mat& frame, const std::string& text, cv::Point origin,
             double scale = 0.55) {
    cv::putText(frame, text, origin, cv::FONT_HERSHEY_DUPLEX, scale,
                cv::Scalar(10, 17, 21), 4, cv::LINE_AA);
    cv::putText(frame, text, origin, cv::FONT_HERSHEY_DUPLEX, scale,
                cv::Scalar(230, 242, 237), 1, cv::LINE_AA);
}

std::vector<cv::Point> project_ring(const std::vector<GeoPosition>& ring,
                                    std::pair<double, double> anchor,
                                    double zoom, cv::Size size) {
    std::vector<cv::Point> points;
    points.reserve(ring.size());
    for (const auto& position : ring) {
        const auto [x, y] = geo_to_screen(position[1], position[0], anchor, zoom,
                                          size.width, size.height);
        points.emplace_back(x, y);
    }
    return points;
}
}  // namespace

void draw_satellite_lock(cv::Mat& frame, std::pair<double, double> target,
                         std::pair<double, double> camera_anchor, double zoom,
                         double progress, const std::string& label,
                         bool show_telemetry) {
    progress = std::clamp(progress, 0.0, 1.0);
    const auto [x, y] = geo_to_screen(target.first, target.second, camera_anchor,
                                      zoom, frame.cols, frame.rows);
    const cv::Point center(x, y);
    const int radius = static_cast<int>(std::nearbyint(112.0 - 78.0 * progress));
    const int len = static_cast<int>(std::nearbyint(24.0 - 10.0 * progress));
    draw_bracket(frame, center, radius, len, kAmber);
    const int cross = std::max(8, 34 - static_cast<int>(progress * 23.0));
    line(frame, {x - cross, y}, {x - 5, y}, cv::Scalar(235, 243, 243));
    line(frame, {x + 5, y}, {x + cross, y}, cv::Scalar(235, 243, 243));
    line(frame, {x, y - cross}, {x, y - 5}, cv::Scalar(235, 243, 243));
    line(frame, {x, y + 5}, {x, y + cross}, cv::Scalar(235, 243, 243));
    cv::circle(frame, center, 3, kAmber, cv::FILLED, cv::LINE_AA);

    const double lat_rad = target.first * kPi / 180.0;
    const double mpp = 156543.03392 * std::cos(lat_rad) / std::pow(2.0, zoom);
    const double altitude_km = mpp * frame.rows * 0.95 / 1000.0;
    if (show_telemetry) {
        char ticker[256];
        std::snprintf(ticker, sizeof(ticker), "LAT %+.4f   LON %+.4f   ALT %.2f km   GSD %.1f m/px",
                      target.first, target.second, altitude_km, mpp);
        const int base_y = frame.rows - 28;
        cv::rectangle(frame, {18, base_y - 30}, {frame.cols - 18, frame.rows - 12},
                      cv::Scalar(10, 17, 21), cv::FILLED);
        cv::rectangle(frame, {18, base_y - 30}, {frame.cols - 18, frame.rows - 12},
                      cv::Scalar(79, 99, 91), 1, cv::LINE_AA);
        put_hud(frame, ticker, {30, base_y - 7}, 0.48);
    }
    put_hud(frame, label, {30, 36}, 0.68);
    if (show_telemetry)
        put_hud(frame, progress >= 0.985 ? "TARGET LOCK" : "ACQUIRING TARGET",
                {frame.cols - 260, 36}, 0.48);
}

void draw_crime_scene(cv::Mat& frame, std::pair<double, double> target,
                      std::pair<double, double> camera_anchor, double zoom,
                      double radius_m, double progress, int frame_index,
                      const std::string& label) {
    cv::Mat gray, blue, green, red, graded;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    gray.convertTo(blue, CV_8U, 1.12);
    gray.convertTo(green, CV_8U, 0.91);
    gray.convertTo(red, CV_8U, 0.72);
    std::vector<cv::Mat> channels{blue, green, red};
    cv::merge(channels, graded);
    cv::addWeighted(graded, 0.92, frame, 0.08, 0.0, frame);
    frame.convertTo(frame, -1, 0.78, 0.0);

    const auto [x, y] = geo_to_screen(target.first, target.second,
        camera_anchor, zoom, frame.cols, frame.rows);
    const double mpp = 156543.03392 * std::cos(target.first * kPi / 180.0) /
                       std::pow(2.0, zoom);
    const int radius = std::clamp(static_cast<int>(std::nearbyint(radius_m / mpp)), 12,
                                  std::max(frame.cols, frame.rows));
    cv::Mat circle_mask(frame.size(), CV_8UC1, cv::Scalar(0));
    cv::circle(circle_mask, {x, y}, radius, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    cv::Mat hatch(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    for (int d = -frame.rows; d < frame.cols + frame.rows; d += 16)
        line(hatch, {d, frame.rows}, {d + frame.rows, 0}, cv::Scalar(113, 126, 76), 1);
    cv::Mat hatch_masked;
    hatch.copyTo(hatch_masked, circle_mask);
    cv::addWeighted(frame, 1.0, hatch_masked, 0.30, 0.0, frame);

    cv::Mat rings(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    for (int ring = 0; ring < 3; ++ring) {
        const double phase = std::fmod(progress * 2.8 + ring / 3.0, 1.0);
        const int ring_radius = std::max(3, static_cast<int>(phase * radius));
        cv::circle(rings, {x, y}, ring_radius, kCyan, 2, cv::LINE_AA);
    }
    cv::addWeighted(frame, 1.0, rings, 0.58, 0.0, frame);

    for (int degree = 0; degree < 360; degree += 18) {
        const int end = std::min(360, degree + 10);
        cv::ellipse(frame, {x, y}, {radius, radius}, 0.0, degree, end,
                    kAmber, 2, cv::LINE_AA);
    }
    const int pulse = 8 + (frame_index * 3) % std::max(12, radius);
    cv::circle(frame, {x, y}, pulse, kCyan, 2, cv::LINE_AA);
    cv::circle(frame, {x, y}, 6, cv::Scalar(235, 245, 243), cv::FILLED, cv::LINE_AA);
    char caption[160];
    std::snprintf(caption, sizeof(caption), "%s  /  RAGGIO %.1f km  /  %d MIN A PIEDI",
                  label.c_str(), radius_m / 1000.0,
                  static_cast<int>(std::nearbyint(radius_m / 1000.0 * 12.0)));
    put_hud(frame, caption, {28, 42}, 0.62);
}

void draw_territory_wipe(cv::Mat& frame, const std::vector<GeoPolygon>& polygons,
                         std::pair<double, double> camera_anchor, double zoom,
                         double progress, const std::string& label) {
    if (polygons.empty()) return;
    progress = std::clamp(progress, 0.0, 1.0);
    cv::Mat mask(frame.size(), CV_8UC1, cv::Scalar(0));
    std::vector<std::vector<cv::Point>> exteriors;
    std::vector<std::vector<cv::Point>> holes;
    std::vector<std::vector<cv::Point>> every_ring;
    for (const auto& polygon : polygons) {
        if (polygon.rings.empty()) continue;
        for (std::size_t r = 0; r < polygon.rings.size(); ++r) {
            auto points = project_ring(polygon.rings[r], camera_anchor, zoom, frame.size());
            if (points.size() < 3) continue;
            every_ring.push_back(points);
            if (r == 0) exteriors.push_back(points);
            else holes.push_back(points);
        }
    }
    if (exteriors.empty()) return;
    cv::fillPoly(mask, exteriors, cv::Scalar(255), cv::LINE_AA);
    cv::fillPoly(mask, holes, cv::Scalar(0), cv::LINE_AA);

    cv::Mat dimmed;
    frame.convertTo(dimmed, -1, 0.68, 0.0);
    cv::Mat outside;
    cv::bitwise_not(mask, outside);
    dimmed.copyTo(frame, outside);

    cv::Mat tint(frame.size(), CV_8UC3, cv::Scalar(178, 60, 224));
    cv::Mat blended;
    cv::addWeighted(frame, 0.72, tint, 0.28, 0.0, blended);
    blended.copyTo(frame, mask);

    cv::Mat glow(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    std::size_t total = 0;
    for (const auto& ring : every_ring) total += ring.size();
    const std::size_t revealed = static_cast<std::size_t>(std::ceil(total * progress));
    std::size_t consumed = 0;
    for (const auto& ring : every_ring) {
        const std::size_t count = ring.size() > 1 ? ring.size() - 1 : 0;
        const std::size_t visible = std::min(count, revealed > consumed ? revealed - consumed : 0);
        if (visible > 1) {
            std::vector<cv::Point> partial(ring.begin(), ring.begin() +
                static_cast<std::ptrdiff_t>(visible + 1));
            cv::polylines(glow, partial, false, cv::Scalar(194, 75, 255), 8, cv::LINE_AA);
            cv::polylines(frame, partial, false, cv::Scalar(199, 118, 255), 2, cv::LINE_AA);
        }
        consumed += count;
    }
    cv::Mat bloom;
    cv::GaussianBlur(glow, bloom, {19, 19}, 0.0);
    cv::addWeighted(frame, 1.0, bloom, 0.55, 0.0, frame);
    put_hud(frame, "TERRITORIO // " + label, {28, 38}, 0.66);
}

void draw_documentary_concept(cv::Mat& frame, const std::string& concept_id,
                              const std::vector<cv::Point>& points,
                              double progress, int frame_index,
                              double parameter) {
    if (points.empty()) return;
    progress = std::clamp(progress, 0.0, 1.0);
    const auto p = [&](std::size_t i) -> cv::Point {
        return points[std::min(i, points.size() - 1)];
    };
    const cv::Scalar cyan(255, 225, 92), amber(28, 165, 255), red(70, 72, 255);
    const cv::Point c = p(0);
    cv::Mat overlay(frame.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    auto route = [&] {
        if (points.size() > 1) {
            cv::polylines(overlay, points, false, amber, 4, cv::LINE_AA);
            cv::polylines(frame, points, false, cyan, 2, cv::LINE_AA);
        }
    };
    auto title = [&](const char* text) { put_hud(frame, text, {26, 40}, 0.62); };
    if (concept_id == "cell_tower_triangulation" || concept_id == "frontline_fluid_corridor") {
        std::vector<cv::Mat> masks;
        for (std::size_t i = 0; i < points.size(); ++i) {
            cv::Mat mask(frame.size(), CV_8UC1, cv::Scalar(0));
            const int radius = std::max(60, std::min(frame.cols, frame.rows) / 3);
            if (concept_id == "cell_tower_triangulation") {
                const double angle = static_cast<double>(i) * 120.0 + frame_index * 0.15;
                cv::ellipse(mask, p(i), {radius, radius}, angle, -60, 60, 255, cv::FILLED, cv::LINE_AA);
            } else {
                const cv::Point end = i + 1 < points.size() ? p(i + 1) : p(i);
                cv::line(mask, p(i), end, 255, radius / 2, cv::LINE_AA);
            }
            masks.push_back(std::move(mask));
            cv::circle(frame, p(i), 7, concept_id == "cell_tower_triangulation" ? cyan : red, cv::FILLED, cv::LINE_AA);
        }
        cv::Mat common = masks.front().clone();
        for (std::size_t i = 1; i < masks.size(); ++i) cv::bitwise_and(common, masks[i], common);
        if (concept_id == "cell_tower_triangulation") {
            cv::Mat tint(frame.size(), CV_8UC3, cv::Scalar(20, 235, 90));
            tint.copyTo(overlay, common);
            cv::addWeighted(frame, 1.0, overlay, 0.24, 0, frame);
            title("SIGNAL ANALYSIS // OVERLAP ESTIMATE");
        } else {
            cv::Mat contours; cv::Canny(common, contours, 40, 120);
            cv::Mat glow; cv::cvtColor(contours, glow, cv::COLOR_GRAY2BGR);
            cv::GaussianBlur(glow, glow, {15, 15}, 0);
            cv::addWeighted(frame, 1.0, glow, 0.8, 0, frame);
            title("FRONTLINE // SCHEMATIC BUFFER ZONE");
        }
    } else if (concept_id == "isochrone_travel_bubble") {
        for (int i = 3; i >= 1; --i) {
            const double t = std::clamp(progress * 3.0 - (3 - i), 0.0, 1.0);
            std::vector<cv::Point> ring;
            const double r = (55.0 + 42.0 * i) * t;
            for (int a = 0; a < 48; ++a) {
                const double q = 2 * kPi * a / 48.0;
                const double wobble = 1.0 + 0.11 * std::sin(3*q + i) + 0.06 * std::cos(5*q);
                ring.emplace_back(cvRound(c.x + r*wobble*std::cos(q)), cvRound(c.y + r*wobble*std::sin(q)));
            }
            cv::polylines(frame, ring, true, i == 1 ? cv::Scalar(80, 230, 100) : i == 2 ? amber : red, 2, cv::LINE_AA);
        }
        title("ISOCHRONE PREVIEW // ROAD NETWORK DATA REQUIRED");
    } else if (concept_id == "timeline_suspect_trail" || concept_id == "subsea_cable_glow_network" ||
               concept_id == "great_circle_polar_route" || concept_id == "port_hub_radial_burst" ||
               concept_id == "pipeline_energy_flow" || concept_id == "supply_chain_artery_pulse") {
        if (points.size() > 1) {
            const std::size_t count = std::max<std::size_t>(2, static_cast<std::size_t>(1 + progress * (points.size()-1)));
            std::vector<cv::Point> partial(points.begin(), points.begin() + std::min(count, points.size()));
            cv::polylines(overlay, partial, false, concept_id == "pipeline_energy_flow" ? red : amber,
                          concept_id == "supply_chain_artery_pulse" ? 8 : 4, cv::LINE_AA);
            if (concept_id == "pipeline_energy_flow") {
                cv::Mat dashed(frame.size(), CV_8UC3, cv::Scalar(0,0,0));
                for (std::size_t i = 1; i < partial.size(); ++i)
                    if ((i + static_cast<std::size_t>(frame_index / 2)) % 4 < 2)
                        cv::line(dashed, partial[i-1], partial[i], amber, 4, cv::LINE_AA);
                overlay = dashed;
            }
            cv::Mat glow; cv::GaussianBlur(overlay, glow, {19,19}, 0);
            cv::addWeighted(frame, 1.0, glow, 0.75, 0, frame);
            cv::addWeighted(frame, 1.0, overlay, 0.82, 0, frame);
            const cv::Point head = partial.back();
            cv::circle(frame, head, 8, cyan, cv::FILLED, cv::LINE_AA);
            if (concept_id == "timeline_suspect_trail") {
                const int minutes = static_cast<int>(progress * 29);
                char stamp[64]; std::snprintf(stamp, sizeof(stamp), "14:%02d  /  MOVEMENT TRACE", 23 + minutes);
                title(stamp);
            } else if (concept_id == "subsea_cable_glow_network") title("GLOBAL CABLE ROUTE // ILLUSTRATIVE");
            else if (concept_id == "great_circle_polar_route") title("GREAT CIRCLE ROUTE // SPHERICAL PREVIEW");
            else if (concept_id == "port_hub_radial_burst") title("HUB CONNECTIVITY // ROUTE PREVIEW");
            else if (concept_id == "pipeline_energy_flow") title("ENERGY FLOW // SCHEMATIC");
            else title("SUPPLY CHAIN FLOW // RELATIVE INTENSITY");
        }
    } else if (concept_id == "thermal_heatmap_overlay") {
        // Incident locations do not change per frame: bake the blurred color
        // field once and reuse it while the camera and dataset stay fixed.
        thread_local std::vector<int> cached_key;
        thread_local cv::Mat cached_color;
        std::vector<int> key{frame.cols, frame.rows};
        key.reserve(2 + points.size() * 2);
        for (const auto& point : points) {
            key.push_back(point.x);
            key.push_back(point.y);
        }
        if (key != cached_key) {
            cv::Mat heat(frame.size(), CV_8UC1, cv::Scalar(0));
            for (std::size_t i = 0; i < points.size(); ++i)
                cv::circle(heat, p(i), 48 + static_cast<int>((i * 17) % 30),
                           cv::Scalar(80 + (i * 37) % 170), cv::FILLED, cv::LINE_AA);
            cv::GaussianBlur(heat, heat, {0, 0}, 32);
            cv::applyColorMap(heat, cached_color, cv::COLORMAP_INFERNO);
            cached_key = std::move(key);
        }
        cv::addWeighted(frame, 0.62, cached_color, 0.38, 0, frame);
        title("INCIDENT DENSITY // SYNTHETIC SAMPLE");
    } else if (concept_id == "night_vision_infrared") {
        cv::Mat gray, lut(1, 256, CV_8UC3);
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        for (int i=0;i<256;++i) lut.at<cv::Vec3b>(0,i) = cv::Vec3b(i/5, i, i/8);
        cv::Mat green(frame.size(), CV_8UC3);
        for (int y=0;y<gray.rows;++y) for(int x=0;x<gray.cols;++x) { const auto v=gray.at<unsigned char>(y,x); green.at<cv::Vec3b>(y,x)=lut.at<cv::Vec3b>(0,v); }
        frame = green;
        for (int y=0;y<frame.rows;y+=4) cv::line(frame,{0,y},{frame.cols,y},{0,24,0},1);
        cv::circle(frame,c,62,cyan,1,cv::LINE_AA); cv::line(frame,{c.x-85,c.y},{c.x+85,c.y},cyan,1);
        title("NIGHT VISION // SURVEILLANCE MODE");
    } else if (concept_id == "naval_chokepoint_exclusion") {
        const int r = std::max(40, std::min(frame.cols,frame.rows)/5);
        const double alpha = 0.45 + 0.3*std::sin(frame_index*0.18);
        cv::circle(overlay,c,r,red,3,cv::LINE_AA); cv::circle(overlay,c,r*2/3,red,2,cv::LINE_AA);
        cv::addWeighted(frame,1.0,overlay,alpha,0,frame);
        if(points.size()>1) cv::polylines(frame,points,false,cyan,4,cv::LINE_AA);
        title("MARITIME CHOKEPOINT // SCHEMATIC ZONE");
    } else if (concept_id == "missile_arc_ballistic") {
        if(points.size()>1) {
            const cv::Point a=p(0), b=p(1); std::vector<cv::Point> arc;
            for(int i=0;i<=100;++i){double t=i/100.0; arc.emplace_back(cvRound(a.x+(b.x-a.x)*t),cvRound(a.y+(b.y-a.y)*t-4*parameter*t*(1-t)));}
            cv::polylines(overlay,arc,false,amber,4,cv::LINE_AA); cv::Mat glow; cv::GaussianBlur(overlay,glow,{17,17},0); cv::addWeighted(frame,1,glow,.8,0,frame); cv::addWeighted(frame,1,overlay,.9,0,frame);
            const auto q=arc[std::min<std::size_t>(100,static_cast<std::size_t>(progress*100))]; cv::line(frame,q,{q.x,q.y+static_cast<int>(parameter*4*progress*(1-progress))},cyan,1); cv::circle(frame,q,7,amber,cv::FILLED,cv::LINE_AA);
        } title("TRAJECTORY VISUALIZATION // NOT TO SCALE");
    } else if (concept_id == "multi_camera_split_matrix") {
        const int w=frame.cols/2,h=frame.rows/2; cv::Mat source=frame.clone();
        for(int i=0;i<4;++i){cv::Rect r((i%2)*w,(i/2)*h,w,h); cv::Mat panel; cv::resize(source(r),panel,r.size()); panel.copyTo(frame(r)); cv::rectangle(frame,r,cyan,2,cv::LINE_AA); cv::putText(frame,"SITE 0"+std::to_string(i+1),{r.x+14,r.y+26},cv::FONT_HERSHEY_SIMPLEX,.55,cyan,1,cv::LINE_AA);}
        title("MULTI-SITE MATRIX // SINGLE VIEWPORT PREVIEW");
    } else if (concept_id == "air_defense_interception_dome") {
        const cv::Size axes(std::max(40,frame.cols/7),std::max(20,frame.rows/5));
        for(int i=4;i>=1;--i) cv::ellipse(overlay,c,{axes.width*i/4,axes.height*i/4},0,0,360,cv::Scalar(50,50,255),1,cv::LINE_AA);
        cv::addWeighted(frame,1,overlay,.5,0,frame); cv::line(frame,{c.x-axes.width,c.y},{c.x+axes.width,c.y},cyan,1); title("COVERAGE ENVELOPE // SCHEMATIC");
    } else if (concept_id == "satellite_before_after_split") {
        cv::Mat old=frame.clone(); cv::Mat gray; cv::cvtColor(old,gray,cv::COLOR_BGR2GRAY); cv::cvtColor(gray,old,cv::COLOR_GRAY2BGR);
        const int split=static_cast<int>(frame.cols*progress); old(cv::Rect(0,0,std::max(1,split),frame.rows)).copyTo(frame(cv::Rect(0,0,std::max(1,split),frame.rows)));
        cv::line(frame,{split,0},{split,frame.rows},cv::Scalar(255,255,255),3,cv::LINE_AA); title("BEFORE / AFTER STYLE PREVIEW // SAME IMAGERY SOURCE");
    } else if (concept_id == "topographic_contour_rise") {
        cv::Mat gray,edges; cv::cvtColor(frame,gray,cv::COLOR_BGR2GRAY); cv::Canny(gray,edges,55,100); cv::Mat glow; cv::cvtColor(edges,glow,cv::COLOR_GRAY2BGR); cv::addWeighted(frame,1,glow,.55,0,frame); title("RELIEF CONTOUR STYLE // DEM DATA REQUIRED");
    } else if (concept_id == "flood_sea_level_inundation") {
        const int level=static_cast<int>(progress*frame.rows); cv::rectangle(overlay,{0,frame.rows-level},{frame.cols,frame.rows},cv::Scalar(220,120,20),cv::FILLED); cv::addWeighted(frame,1,overlay,.30,0,frame); cv::line(frame,{0,frame.rows-level},{frame.cols,frame.rows-level},cyan,3); title("SEA LEVEL RISE // ELEVATION MODEL REQUIRED");
    } else if (concept_id == "wildfire_burn_scar_expansion") {
        const int rx=static_cast<int>((20+progress*frame.cols/2)), ry=static_cast<int>((12+progress*frame.rows/3));
        cv::ellipse(overlay,c,{rx,ry},-20,0,360,cv::Scalar(22,22,22),cv::FILLED,cv::LINE_AA); cv::ellipse(overlay,c,{rx,ry},-20,0,360,amber,4,cv::LINE_AA); cv::addWeighted(frame,1,overlay,.68,0,frame); title("BURN SCAR EXPANSION // SCHEMATIC");
    } else if (concept_id == "cinematic_anchor_pitch_tilt") {
        const float inset = static_cast<float>(frame.cols * 0.16 * progress);
        const std::vector<cv::Point2f> source{{0,0}, {static_cast<float>(frame.cols-1),0},
            {static_cast<float>(frame.cols-1),static_cast<float>(frame.rows-1)},
            {0,static_cast<float>(frame.rows-1)}};
        const std::vector<cv::Point2f> target{{inset,0},
            {static_cast<float>(frame.cols-1)-inset,0},
            {static_cast<float>(frame.cols-1),static_cast<float>(frame.rows-1)},
            {0,static_cast<float>(frame.rows-1)}};
        cv::Mat warped;
        cv::warpPerspective(frame, warped, cv::getPerspectiveTransform(source, target),
                            frame.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        frame = std::move(warped);
        title("CAMERA PITCH TRANSITION // IMAGE WARP PREVIEW");
    } else {
        // Explicit fallback keeps unsupported identifiers visually obvious during authoring.
        title("DOCUMENTARY MAP CONCEPT");
    }
}

void apply_documentary_finish(cv::Mat& frame, int frame_index,
                              double vignette_strength, double grain_strength) {
    vignette_strength = std::clamp(vignette_strength, 0.0, 0.8);
    grain_strength = std::clamp(grain_strength, 0.0, 0.12);
    static std::map<std::pair<int, int>, cv::Mat> masks;
    const auto key = std::pair{frame.cols, frame.rows};
    auto found = masks.find(key);
    if (found == masks.end()) {
        cv::Mat mask(frame.size(), CV_32FC3);
        const double rx = std::max(1.0, frame.cols * 0.72);
        const double ry = std::max(1.0, frame.rows * 0.72);
        for (int y = 0; y < frame.rows; ++y) {
            cv::Vec3f* row = mask.ptr<cv::Vec3f>(y);
            for (int x = 0; x < frame.cols; ++x) {
                const double dx = (x - (frame.cols - 1) * 0.5) / rx;
                const double dy = (y - (frame.rows - 1) * 0.5) / ry;
                const double radial = std::clamp(dx * dx + dy * dy, 0.0, 1.0);
                const float value = static_cast<float>(1.0 - 0.42 * radial);
                row[x] = cv::Vec3f(value, value, value);
            }
        }
        found = masks.emplace(key, std::move(mask)).first;
    }
    if (vignette_strength > 0.0) {
        cv::Mat mask;
        found->second.convertTo(mask, CV_32FC3, vignette_strength / 0.42,
                               1.0 - vignette_strength / 0.42);
        cv::Mat float_frame;
        frame.convertTo(float_frame, CV_32FC3);
        cv::multiply(float_frame, mask, float_frame);
        float_frame.convertTo(frame, CV_8UC3);
    }
    if (grain_strength > 0.0) {
        cv::RNG rng(0x5EED1234U + static_cast<std::uint64_t>(frame_index));
        cv::Mat noise(frame.size(), CV_16SC3);
        rng.fill(noise, cv::RNG::NORMAL, 0.0, grain_strength * 255.0);
        cv::Mat wide, result;
        frame.convertTo(wide, CV_16SC3);
        cv::add(wide, noise, wide);
        wide.convertTo(result, CV_8UC3);
        result.copyTo(frame);
    }
}

void apply_radial_motion_blur(cv::Mat& frame, double zoom_delta) {
    if (zoom_delta <= 0.015) return;
    const double spread = std::clamp(zoom_delta * 0.035, 0.0, 0.022);
    const cv::Point2f center((frame.cols - 1) * 0.5F, (frame.rows - 1) * 0.5F);
    cv::Mat inner, outer, mixed;
    cv::warpAffine(frame, inner,
        cv::getRotationMatrix2D(center, 0.0, 1.0 - spread), frame.size(),
        cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    cv::warpAffine(frame, outer,
        cv::getRotationMatrix2D(center, 0.0, 1.0 + spread), frame.size(),
        cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    cv::addWeighted(frame, 0.54, inner, 0.23, 0.0, mixed);
    cv::addWeighted(mixed, 1.0, outer, 0.23, 0.0, frame);
}

}  // namespace chrononplugin::maps
