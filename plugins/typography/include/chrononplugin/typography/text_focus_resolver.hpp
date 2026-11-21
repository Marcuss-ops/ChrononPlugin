#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_focus_resolver.hpp — TextFocusResolver authority (text_depth_focus_v1)
// ═══════════════════════════════════════════════════════════════════════════
//
// One authority that turns "which word is in focus" into per-word visual state
// (z / blur / opacity / scale), and lowers the text_depth_focus_v1 preset
// family into the canonical TextAnimatorSpec primitives. It owns the focus
// policy ONLY: Chronon3D keeps applying its existing Gaussian blur (per-glyph
// blur property evaluated post-layout, post-MTSDF composition) and the
// existing CameraPlan DOF. No second blur engine, no new runtime stage.
//
// Two resolution modes:
//   1. Editorial focus — blur grows with |word_index - focus_position| from
//      the focused word. The words stay 2D; the focus plane "travels".
//          blur(i) = clamp(|i - focus_position| * blur_scale, 0, max_blur)
//   2. True depth focus — each word sits at a distinct world Z (word depths),
//      and blur follows the camera depth error (mirrors the legacy
//      DepthOfFieldSettings contract in scene/model/camera/camera_common_types.hpp):
//          depth_error = | |word_z - camera_z| - focus_distance |
//          blur        = clamp(depth_error * aperture_scale, 0, max_blur)
//
// Both modes are continuous in `focus_position` / `focus_distance`, so the
// plane of focus interpolates smoothly between words (no pop at word
// boundaries) and lands exactly on the final word at travel end.
//
// Presets (family text_depth_focus_v1):
//   text_focus_word_static    — focus locked on one word
//   text_focus_word_travel    — focus travels word 0 → N-1
//   text_focus_near_to_far    — depth focus sweep, near plane → far plane
//   text_focus_far_to_near    — depth focus sweep, far plane → near plane
//   text_focus_center_out     — focus from the middle word outward
//   text_focus_edges_in       — focus from the edge words inward
//   text_focus_rack_duo       — two-word rack focus A ⇄ B ⇄ A
//   text_focus_depth_cascade  — words spread in Z (depth_step) + focus travel
//
// Anti-duplication: this header is an authoring helper like text_motion.hpp —
// it emits TextAnimatorSpec data over the canonical selector/evaluator
// pipeline. It is not a registry, not a cache, and not a second runtime.
// ═══════════════════════════════════════════════════════════════════════════

#include "text_focus_animators.hpp"
#include "text_focus_presets.hpp"
#include "text_focus_profile.hpp"
#include "text_focus_sampling.hpp"
