# KH-15: AER Fallback Renderer Correctness

Audited from `main` commit `0df12695`, 2026-09-20.

Scope: AER alternating-eye camera/source attribution, coherent-pair publication,
per-eye cache lifetime, source-qualified hands/weapon alignment, render order,
level transitions, command-recording failure and owner-copy submission failure.
SFS and Native rendering were reviewed only where they share the XR copy/cache
recovery path.

## P1: Exact camera bytes could be assigned to the wrong AER source identity

`AerWorldViewHistory::recognize` already rejected exact camera bytes when more
than one recent source identity matched them. `align`, however, considered the
same bytes ambiguous only when their camera context differed. Two observations
from the same player/camera context with different pose IDs, eyes or domains
therefore depended on ring order.

A stationary view makes this possible without any corruption: two consecutive
AER source publications can have byte-identical origin/basis/FOV while still
belonging to different tracking samples. A copied render view containing those
bytes does not prove which source produced it. The old alignment path could pick
the newest matching entry and then relabel the view to another eye. Its target
search also did not require the target to remain in the source domain.

Fix: source recognition and alignment now use the same complete identity:
context, pose ID, level, physical eye and scene domain. Target alignment also
requires the source domain. Ambiguous exact bytes fail closed instead of
guessing from history order.

Regression coverage adds identical-byte cases for different pose IDs and eyes,
plus a gameplay-to-scripted target attempt. The previous alignment logic fails
the new regression; the corrected implementation passes.

## P1: Failed XR copy submission retained AER cache metadata for GPU work that never ran

The owner copy command buffer advances AER host metadata while it is recorded:
per-eye cache initialization/revision, source keys, cached eye poses/FOVs and
coherent-pair readiness inputs. Command-buffer reset/begin/end failures already
call `invalidateAlternatingStereoHistory(true)`.

A successful recording followed by a failed `vkQueueSubmit` was different.
The shared failure handler discarded FSR's recorded state, but retained AER's
cache/source claims. A retry could therefore treat stale private eye images as
the newly recorded source even though none of the copy commands executed.

Fix: the production `XrCopySubmitFailure.inc` path now invalidates AER
alternating-stereo history on every failed copy submission, using the same
recovery boundary as a command-recording failure. The next AER cycle must
re-establish both eye caches and their source identities.

The existing FSR GPU regression includes this production failure handler for
injected host/device OOM submissions. It now also asserts that AER history is
invalidated on those failures.

## P2: A failed stereo-cache resize could later validate a partial cache

`ensureStereoCache` previously accepted an existing cache when eye 0 was
non-null and the stored extent matched. During a resize it destroyed the old
cache, then allocated the new eyes directly into the live state. If creation,
memory selection, allocation or binding failed after eye 0 succeeded, the
function returned with partial new resources while `stereoCacheExtent` still
described the old cache.

If a subsequent request returned to that old extent, the fast path could accept
the partial cache as valid even though eye 1 was missing and eye 0 belonged to
the failed resize.

Fix: cache validity now requires both eye images and the requested extent.
Rebuilds clear old extent metadata, allocate/bind both eyes into temporary
handles, destroy all temporary resources on any failure, and publish the new
handles/extent only after both eyes are complete.

The portable image-state regression checks that a one-eye cache can never
satisfy the completeness predicate even when its stale extent matches.

## Reviewed paths without a retained change

- AER render order remains intentionally right-then-left at the physical image
  level, while pair caches map physical eyes to capture/reuse phases separately.
  No physical-eye/phase index mix-up was found.
- `AerSourceWindow` rejects mixed producer keys within one observation window,
  and coherent pair publication requires opposite eyes with identical
  pose/level/domain and a pose newer than the last published source.
- Level/checkpoint generation changes already invalidate both private eye
  histories and re-enter the central-mono warmup path.
- Command-buffer reset, begin and end failures already invalidate AER history.
- The weapon source history retains exact keyed camera/input snapshots for
  queued draws. A tentative change that forced the globally newest camera to
  win was discarded during this audit: an older camera publication can
  legitimately belong to the worker constructing that older AER frame, so
  timestamp/pose ordering alone is not a safe replacement for exact source
  identity.

## Verification and limits

Local portable validation in this sandbox:

- C++17 `aer_world_view_history_tests` passed with `-Wall -Wextra -Werror`.
- C++17 `swapchain_image_state_tests` plus the new stereo-cache completeness
  checks passed with `-Wall -Wextra -Werror`.
- Re-running the new camera ambiguity regression against the previous
  context-only alignment logic aborts as expected (non-zero exit), confirming
  that the test distinguishes the fix from the parent behavior.

The Windows/Vulkan `fsr1-gpu-current-eyes` test was updated to execute the
production copy-submit failure include and assert AER invalidation, but it was
not run in this Linux sandbox. A full MSVC Release layer build, injected Vulkan
allocation/bind failures in the production OpenXR path, and live DOOM/headset
AER validation remain external verification. No headset performance or visual
quality improvement is claimed by this source audit.
