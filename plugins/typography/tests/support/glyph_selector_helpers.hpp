#pragma once

#include <doctest/doctest.h>
#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/core/random/deterministic_random.hpp>
#include <string>
#include <vector>

namespace test_glyph_sel {

using namespace chronon3d;

/// Build a minimal PlacedGlyphRun for testing purposes.
/// Each glyph gets a simple identity: cluster = index, advance = 10.
inline PlacedGlyphRun make_test_placed_run(size_t count) {
    PlacedGlyphRun run;
    for (size_t i = 0; i < count; ++i) {
        PlacedGlyph g;
        g.glyph_id = static_cast<u32>(i + 1);
        g.cluster = static_cast<u32>(i);
        g.is_cluster_start = true;
        g.advance_x = 10.0f;
        g.x = static_cast<float>(i) * 10.0f;
        g.byte_offset = i;
        g.byte_len = 1;
        run.glyphs.push_back(g);
    }
    for (size_t i = 0; i < count; ++i) {
        PlacedGlyphRun::Cluster cl;
        cl.start_glyph = i;
        cl.end_glyph = i + 1;
        cl.byte_offset = i;
        cl.byte_len = 1;
        cl.advance = 10.0f;
        cl.raw_advance = 10.0f;
        run.clusters.push_back(cl);
    }
    run.total_width = static_cast<float>(count) * 10.0f;
    run.total_height = 16.0f;
    run.ascent = 12.0f;
    run.descent = 4.0f;
    run.baseline = 12.0f;
    run.font_size = 16.0f;
    return run;
}

/// Build a source string of `count` lowercase ASCII letters starting from 'a'.
inline std::string make_test_source(size_t count) {
    std::string s;
    s.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        s.push_back(static_cast<char>('a' + (i % 26)));
    }
    return s;
}

/// Build a source string with spaces and newlines for word/line tests.
inline std::string make_word_test_source() {
    return "hello world";
}

/// Build a source string with multiple lines.
inline std::string make_line_test_source() {
    return "line1\nline2\nline3";
}

/// Build a placed run + source where the source contains whitespace bytes
/// at specific glyph positions.  Layout: each glyph occupies one source byte.
inline PlacedGlyphRun make_run_for_source(std::string_view source) {
    PlacedGlyphRun run;
    for (size_t i = 0; i < source.size(); ++i) {
        PlacedGlyph g;
        g.glyph_id = static_cast<u32>(i + 1);
        g.cluster = static_cast<u32>(i);
        g.is_cluster_start = true;
        g.advance_x = 10.0f;
        g.x = static_cast<float>(i) * 10.0f;
        g.byte_offset = i;
        g.byte_len = 1;
        run.glyphs.push_back(g);

        PlacedGlyphRun::Cluster cl;
        cl.start_glyph = i;
        cl.end_glyph = i + 1;
        cl.byte_offset = i;
        cl.byte_len = 1;
        cl.advance = 10.0f;
        cl.raw_advance = 10.0f;
        run.clusters.push_back(cl);
    }
    run.total_width = static_cast<float>(source.size()) * 10.0f;
    return run;
}

} // namespace test_glyph_sel
