// ── tests/support/layer_builder_inspection.hpp  // drift-allow: stale-ref
//
// Phase-3.3 mechanical split.  Adapter TEST-ONLY (Phase-3.3 P1 spec,
// per AGENTS feature-freeze rules — diagnostic-only extension is
// permitted under "test deterministic gate" + "consumer SDK extension"
// sanity, NOT under the visual-feature category which is frozen).
//
// Behavioural contract (mirrors what the previous `pending_text_runs()`
// public method returned, with three hardening improvements):
//
//   1. NO RAW INTERNAL POINTERS — the public surface of
//      `LayerBuilder` previously exposed
//      `std::vector<const PendingTextRun*>`, leaking the internal
//      `m_text_runs` storage layout to every consumer TU.  The
//      inspector here returns a value-typed
//      `std::vector<PendingRunSnapshot>` instead — copies the
//      `name` and `animators` fields, leaves the storage layout
//      encapsulated.
//
//   2. STABLE IDENTITY — reallocations inside `m_text_runs` after
//      another `text_run(...)` call no longer invalidate the
//      inspector's output: each `PendingRunSnapshot` is a value
//      type owned by the caller (test code).
//
//   3. FRIEND-CLOSED — reading `m_text_runs` is gated on the
//      `friend class chronon3d::builders::testing::LayerBuilderInspector;`
//      declaration in <chronon3d/scene/builders/layer_builder.hpp>.
//      Production callers cannot construct this inspector without
//      pulling in this test-only header, which deliberately lives
//      under .../builders/test/ to signal "test-only / diagnostic-only".
//
// Usage (test TU):
//
//     #include <chronon3d/scene/builders/test/layer_builder_inspection.hpp>
//
//     using chronon3d::builders::testing::LayerBuilderInspector;
//     using chronon3d::builders::testing::PendingRunSnapshot;
//
//     LayerBuilder lb("...", Frame{0}, FrameRate{30, 1});
//     const auto pre = LayerBuilderInspector::pending_runs(lb);
//     for (const PendingRunSnapshot& snap : pre) {
//         CHECK(snap.name == "camera_text");
//         // ...
//     }

#pragma once

#include <string>
#include <vector>

#include <chronon3d/text/animation/text_animator_spec.hpp>  // chronon3d::TextAnimatorSpec (snapshot copy target)
#include <chronon3d/scene/builders/layer_builder.hpp> // LayerBuilder (friend mediation)
#include <chronon3d/text/prepared_text.hpp>           // PreparedText (snapshot copy target)

namespace chronon3d {
namespace builders {
namespace testing {

    // ── Value-typed snapshot of one pending text-run entry ─────────────────
    // Lifetime-independent of any LayerBuilder instance; safe to store
    // across reallocations inside `m_text_runs`.  Each snapshot is
    // produced by `LayerBuilderInspector::pending_runs(lb)` (below)
    // through the friend-declaration gated access read.
    //
    // The field set is deliberately minimal:
    //   * `name` — copies the entry's name (value-typed).
    //   * `animators` — copies the entry's resolved animator spec
    //                   stack (vector of TextAnimatorSpec by value).
    //
    // If a downstream test needs additional fields (font spec,
    // appearance.color override, etc.), extend this struct behind
    // a new test-only accessor — DO NOT re-introduce a public
    // surface on LayerBuilder for that purpose.
    struct PendingRunSnapshot {
        std::string name;
        std::vector<chronon3d::TextAnimatorSpec> animators;
        // Full lowered payload copy (style.color / style.shadows / font /
        // frame ...). Added for the subtitle style-parity test: the test
        // needs to read the emitted run's appearance (fill + shadow) after
        // SubtitleTrackBuilder::build() commits it.
        chronon3d::PreparedText text;
    };

    // ── Test-only inspector (friend-closed) ─────────────────────────────────
    // Static free-function class.  Production code that does not
    // include this header cannot instantiate a `LayerBuilder` with
    // `m_text_runs` storage — those members remain private and the
    // friend-declaration does NOT propagate transitively (friendship
    // is not inherited; it is a 1:1 declaration between the
    // declaring class and the friend).
    class LayerBuilderInspector {
    public:
        // Read a stable value-typed snapshot of every pending text-run
        // entry on `lb`.  Returns an empty vector when no `text_run(...)`
        // call has been made on the builder (Sub-cases 7-9 use
        // `lb.text(...)` simple-entry path, producing an empty view).
        //
        // Order is preservation of insertion order, identical to the
        // pre-split `pending_text_runs()` public method's order.
        //
        // Per-entry fields copied:
        //   * `name`        (std::string-by-value copy via std::pmr::string→std::string)
        //   * `params.animators` (vector copy, full TextAnimatorSpec elements)
        //
        // NOT copied (intentional, hard-odometer of forward-changes):
        //   * `params.text` (PreparedText typically very heavy; tests
        //                    that need it pull `text_run(name, ...)`
        //                    directly).
        //   * `params.motion(...)` any-of — same as above.
        //
        // noexcept: this snapshot read does not throw under any
        // current call path (`std::vector::reserve`, `push_back`
        // via `std::move`, and `unique_ptr::get` are all
        // non-throwing in the canonical chrono3d TUs).
        static std::vector<PendingRunSnapshot>
        pending_runs(const LayerBuilder &lb) noexcept {
            std::vector<PendingRunSnapshot> out;
            out.reserve(lb.m_text_runs.size());  // friend access here
            for (const auto &up : lb.m_text_runs) {
                PendingRunSnapshot snap;
                // pending entries own their PendingTextRun slot;
                // the snapshot fully materialises `name`, `params.animators`
                // and `params.text` so that reallocations of
                // `m_text_runs` cannot invalidate previously-read
                // snapshots.
                snap.name = std::string(up->name);
                snap.animators = up->params.animation.animators;
                snap.text = up->params;
                out.push_back(std::move(snap));
            }
            return out;
        }
    };

} // namespace testing
} // namespace builders
} // namespace chronon3d
