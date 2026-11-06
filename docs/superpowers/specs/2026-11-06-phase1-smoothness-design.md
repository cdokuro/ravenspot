# Phase 1 Design: Smoothness (soft animations, no per-keystroke churn)

**Date:** 2026-11-06 · **Size:** small change (short design)

## Intent
The user wants RavenSpot to feel "very soft": typing, opening and moving the
selection should glide, never jump or stutter. Phase 1 of the general plan.

## Goals
- Typing never rebuilds the list's delegates; rows update in place.
- Open/close and selection animations use soft, non-overshooting curves.
- The first open/first sound has no hitch from lazy audio initialisation.
- Remove the `Invalid path data; path truncated.` warning on open.

## Non-goals
- No new features, layout or theme changes (later phases).
- No change to search results or ordering.

## Anti-goals
- Busy motion: rows sliding around on every keystroke while typing fast.
- Any animation that adds input latency (results must still appear on the
  same frame as the keystroke; animation only smooths the change).

## Evidence (current state)
- `rebuildDisplay()` calls `displayModel.clear()` then appends every row: each
  keystroke destroys and re-creates every visible delegate (~23 QML objects
  per row).
- Open animation (`reopenAnim`, card `Behavior on scale`) uses
  `Easing.OutBack`, which overshoots by design: reads as a bounce.
- Selection highlight: `highlightMoveDuration: 55` (abrupt).
- `AudioEngine::setupVoices()` runs on the first `play*()`: QSoundEffect /
  FFmpeg backend load on the UI thread at first open (log shows the FFmpeg
  banner at the first `show`).

## Approaches (list update)
1. **In-place update (chosen).** Write new rows over existing indices with
   `ListModel.set()`, append/remove only the tail. Delegates survive; only
   their bindings re-evaluate. Smallest change, removes the churn entirely,
   no motion while typing (meets the anti-goal).
2. Keyed diff by `itemId` with move/insert/remove + ListView transitions.
   Rows would slide to new positions; prettier in screenshots but busy when
   typing fast and much more code. Rejected for Phase 1.
3. Keep `clear()` and set `reuseItems: true`. Reuse only applies to
   scrolling, not to `clear()`; would not fix the churn. Rejected.

## Decided (my calls; the user delegated planning)
- Rows: approach 1. Tail inserts fade in (opacity 0→1, 120 ms OutCubic);
  no displaced/move transitions.
- Open: opacity 0→1 + scale 0.97→1.0, 180 ms `Easing.OutQuint` (no overshoot).
  Close: 120 ms `Easing.InCubic`. Same curve family for `reopenAnim`.
- Card height: keep 130 ms but use `Easing.OutQuint` to match.
- Selection highlight: 110 ms `OutCubic` move (was 55 ms).
- Audio: call `setupVoices()` once from the event loop shortly after daemon
  start (queued, not in the constructor), so the first open is already warm.
- `Invalid path data`: find the offending SVG/Shape path and fix the data.

## Left to the builder
- Exact role set handling for `ListModel.set()` (rows with extra roles such
  as `filePath`/`isDir`): keep a consistent role set.
- Whether `settleCursor()`/selection preservation needs adjusting.

## Verification
- E2E suite (228) stays green; both repos build warning-free and in sync.
- Instrument `rebuildDisplay()` with a timing log behind an env var and
  compare before/after on the same query sequence.
- Manual: type fast, open/close repeatedly, arrow through results.
