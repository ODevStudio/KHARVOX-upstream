# Remaining Game Hook / Native Memory-Safety Audit

Date: 2026-09-20. Scope: native game-memory access in the Vulkan layer, camera,
weapon, HUD, OpenXR integration, upstream Native renderer, and optional probes.

Status: fixes and synthetic validation complete for the cases below; **not a
memory-safety clearance**. Startup patching and engine-owned object lifetimes
still have open high-priority risks. The source index is an aid to review, not
proof that every pointer expression has a verified lifetime contract. A complete
semantic review of the upstream renderer remains outstanding.

PR baseline: ODevStudio `main` at `b9a3b99`. This baseline already includes
KH-07's `CameraPoseState` and KH-08's MinHook-backed `MuzzleBranchHook`. This PR
extends the existing snapshot with captured control/sync flags, freshness and
out-of-order rejection; it keeps the existing muzzle thunk and adds bounded
signature reads. The benchmark work and AER PR #8 are outside this PR.

## Findings Fixed

| ID | Priority | Failure | Change |
| --- | --- | --- | --- |
| GH-01 | P1 | Fixed-RVA `memcmp`, metadata strings, hashes and PE reads could fault before rejecting an unsupported image. | `GameMemory.h` checks overflow, image bounds, allocation identity and page protections, then copies through `ReadProcessMemory`. Camera, weapon, HUD, renderer guards, Native fingerprints and adaptive-tick signature checks use bounded reads. Camera/weapon/HUD installation rejects the unsupported timestamp/image-size pair. |
| GH-02 | P1 | Consumers invoked physics and read control flags through retained player addresses, despite KH-07's coherent body/physics publication. | Extend `CameraPoseState` with captured control/sync flags, freshness and out-of-order rejection. Consumers read copied values. Physics calls remain inside the player callback. Body/HUD pose readers and writers use matching locks. Invalidation rejects older in-flight publications. |
| GH-03 | P1, fixed on baseline | Muzzle aiming formerly rewrote a six-byte branch while other threads could execute it. | Preserve KH-08's MinHook-backed conditional thunk and aligned `LONG` toggle. This PR adds bounded reads before signature comparison, with unreadable/overflow regression cases. Manual patches elsewhere remain under GH-08. |
| GH-04 | P1 | The optional camera scanner wrote through a retained heap candidate; weapon diagnostic probes read memory after a non-owning `VirtualQuery` check and called native joints through a retained prop. | Camera scanning is read-only; F9/F10 candidate writes are removed. Diagnostic scans use local checked copies. Retained-prop joint calls are withheld. Callback-owned hands/model calls remain. |
| GH-05 | P1 | Player ViewAxis installation could publish competing originals or hook its own adapter during concurrent callbacks. HUD menu installation could overwrite an intervening hook. | Serialize installation, validate image-owned slots and targets, and use expected-pointer CAS. HUD callbacks acquire the publication lock before loading the original; HUD installation publishes it only after successful CAS. Report protection-restore failures. The dynamic ViewAxis target still lacks an exact fingerprint. |
| GH-06 | P2 | Pattern parsing accepted malformed tokens; camera scanning selected the first duplicate signature; unwind traversal could return an arbitrary record after exhausting its cycle bound. | Reject malformed and duplicate signatures; scan copied bytes; reject invalid PE/section bounds and exhausted unwind chains. |
| GH-07 | P1 | XInput import parsing walked unbounded descriptors, DLL names and thunks. | `GameImports.h` bounds descriptor/name/ordinal/IAT reads, rejects duplicate ordinals, missing original thunks and misaligned slots. Serialize installation and CAS the expected IAT value rather than overwriting an intervening hook. |

Other changes: OpenXR CVar writes require the supported image and writable,
aligned image storage. Adaptive participant detour installation moves from the
first Present to loader negotiation after the layer is pinned. Adaptive runtime
repair uses a fault-contained 0-to-1 CAS, rather than a separate read and write.
Neither change proves the heap object's lifetime.

## Open Risks

### GH-08: P1, Startup Instruction Patches Have No Proven Quiescence

Camera, weapon and HUD entry/mid-function trampolines, the VT append patch and
the adaptive participant patch still use `VirtualProtect` plus multi-byte
`memcpy`. Serializing installers does not stop game threads. Loader negotiation
and early device setup are timing assumptions, not an engine rendezvous.
MinHook-backed Native entry hooks use queued activation, but that does not
protect the separate manual patches. Several manual trampolines remain RWX;
legacy paths also ignore instruction-cache/protection-restore failures.

Before clearing this finding, establish that these targets cannot execute at
installation, or migrate entry hooks to the existing MinHook transaction and
give mid-function patches a thread/IP-aware installation protocol. Do not
attempt rollback by restoring live instruction bytes without the same protocol.
Synthetic thunk execution does not validate concurrent initial installation,
exception unwinding through generated code, or Control Flow Guard behavior.

### GH-09: P1 Risk, HUD Factory Objects Lack a Destruction Lease

`HudHook.cpp`: `progFrame`, `progCreate`, `progResetGeometry`, `progUpdate`,
`progRetire`, `renderProgMeter`. The thread-local GUI/world pair survives native
callbacks. A camera-derived level generation is not a synchronous notification
of engine destruction. `readableRange` cannot distinguish a live object from
freed storage that remains mapped or has been reused. Native calls and writes
through these retained objects remain unsafe if destruction precedes camera
invalidation. No teardown race was reproduced against the game in this audit.

An engine destruction/ownership hook, or a documented factory retention API,
is needed before clearing this risk. If neither exists, retain native Ammo
grouping and disable detached ProgMeter extraction rather than adding probes.

### GH-10: P1 Risk, Adaptive Runtime Heap Repair Can Race Destruction

`reenableDoomAdaptiveRuntimeState` reads the global pointer at `0x35EB8C0` and
CASes the byte at `+0x24`. The new guard contains an access violation and rejects
non-boolean bytes; it cannot prevent ABA/reuse or establish ownership. The game
must guarantee the timing object remains alive during the repair, or the repair
must move to an engine-owned callback. SEH is not a lifetime mechanism.

### GH-11: P2, Remaining Identity and Failure Contracts

- Weapon helpers `getHandsModel`, `getJointTransform` and `hideRenderModelMesh`
  rely on build identity rather than individual function fingerprints. A matching
  timestamp/size does not authenticate executable contents or third-party mods.
- HUD lifecycle pairs can install partially. Mismatch disables the affected
  hook, but already-installed siblings remain. Exercise each partial failure
  before treating lifecycle-derived ownership as reliable. In particular,
  `installProgMeterHooks` patches entry hooks before its separate vtable patch;
  the menu-vtable CAS fix does not make that sequence transactional.
- Upstream Raw Input diagnostics are outside the active layer target. Their
  registration logging trusts caller buffers before the API validates them;
  mouse logging lacks a returned-size check. Partial install/retry handling also
  needs correction and a standalone test before enabling that integration.
- Upstream `safe_read_bytes` / `safe_write_bytes` use SEH around copies. They
  contain faults, but cannot promise coherent snapshots or prevent partial
  writes. The many probe/replay call sites require owner-by-owner review.
- Multi-field head/controller, ledge, ammo and other renderer publications are
  indexed but have not all been proven coherent by the new player snapshot test.

## Hook and Pointer Inventory

The following groups identify installation routes and pointer ownership. Exact
source occurrences, offsets, signatures, reads, writes and publications are in
the generated CSV index. Conditional and dormant paths remain in that index.

| Family | Hooks / image targets | Lifetime, threading and failure contract |
| --- | --- | --- |
| Camera core | `.text` camera-copy and cutscene-FOV signatures; focus trace `0xD48938`; ledge axis `0xDA1870`; ledge state `0xDA24A3`; player origin callsite `0xE3F8D3`; dynamic ViewAxis slot `0x368` | Camera/player arguments belong to the current callback. Cross-callback origin/control data is copied. Bounded signatures and duplicate rejection disable unsupported hooks. Initial patching: GH-08. |
| Camera classifiers | Sync start `0xDA7980`, sync flag read `0x9B2908`, reflected inhibit field `0x30AACD8`; player offsets `0x3DC9`, `0x14C4C`, physics `0x14E58` | Validate layout before enabling classifiers. Owner values are identity tokens, not lifetime handles. Control and sync values now travel with captured origin. |
| Legacy camera stereo | Shader arena `0x1CCD5A0`, backend resize `0x17B9330`, backend owner `0x186E000`, proof `0xDC80D0`, native view-list/layout globals | `installNativeSameFrameStereo` hard-disables split frontend and native two-view. Do not confuse these with active Native replay. Guarded signatures do not make the dormant heap writes safe to enable. |
| Weapon installation | Axis setter `0x3B5400`; muzzle branch `0xD5EFF3`; pushback `0x6305B4`; identity `0xF137B0`; hands update `0xD7D5D0`; render probe `0xF275A0`; FOV `0xD60760`; resource guard `0x16E18D0` | Callback-owned render entities/hands are still native objects. Startup signatures are bounded. Installation is serialized and not retried after partial failure. Muzzle runtime toggles are data-only. |
| Weapon helpers/state | Inventory `0x2E0CD0`, `0xEEDC50`, `0xF24BD0`; named joint `0x15EFB60`; model/joint/mesh helpers; hit-reaction and weapon-kick CVars; collectible metadata | Native calls require current callback ownership. Diagnostics no longer call through the retained prop. Identity scans are fallible copies. Build-only helper identity: GH-11. |
| HUD entry hooks | Origin `0x3B7500`, crosshair `0xC317BE`, canvas `0x1590D60`, localized binding `0x280580`, tutorial render `0x1621000`; EliteGuard activation, pickup notification, PlayerUpgrade frame, FieldDrone bind/release, VegaTraining use/lifecycle, SuitUpgrade lifecycle, pause show/hide | Shared entry installer rechecks the bounded signature. Callback-local menu/sprite/entity pointers still depend on the native call contract. Multi-hook installation is not transactional. |
| HUD vtables | Campaign death, end-of-level, PlayerUpgrade, RuneSelect, RuneChallenge, tutorials, HUD movie, voice communication, objective, boss vitals and rune counter | Validate expected image-owned slot contents before installation. Retained SWF and manager identity must not be treated as ownership. |
| HUD ProgMeter | Frame slot `0x2240888 + 0x48`; allocation `0x158E6B0`; queue `0x161B660`; model/GUI factory and update functions | TLS frame/surface/vertex state limits cross-thread sharing, but does not protect engine destruction. GH-09 remains open. |
| Renderer sizing/memory | Width `0x1871480`, height `0x1871250`, allocator `0x1A32DA0`, allocator object `0x571D2B0`; VT append `0x17E3567` / continuation `0x17E35D7` | Bounded instruction checks; allocator object is image storage. Per-call arguments require engine ownership. Mixed MinHook/manual installation and rollback remain relevant. |
| Native engine profile | All 19 entries in `src/native/EngineProfile.inc`; fresh-shadow setter `0x295780`; AER model matrix `0x1ADE630`; world matrix `0x1830B80` | Build identity, checked hashes and checked unwind-entry identity precede MinHook creation. Queue activation has rollback. Native job/view/model ownership and replay are separate from signature validation. |
| OpenXR game access | Adaptive participant `0x3BED77`; timing CVars; joystick and weapon CVars; XInput ordinal 2/3 IAT slots | CVar storage is image-owned. Heap timing repair: GH-10. Import parsing rejects malformed layouts; IAT replacement uses aligned CAS. |
| Optional probes | Camera scanner F6/F7; weapon F4 and joint scans; Native research probes/caller-unwind lookup; standalone Raw Input export hooks | Scanner and weapon diagnostics copy memory and tolerate failed reads. Research probes are not all inactive: the layer builds with `DOOMVR_RESEARCH_TOOLS=1`. Remaining upstream probe ownership: GH-11. |

Native profile entries: render view, opaque, gather prepare, world-surface
prepare, cull, BSP walk, sort, model/decal/light prepare, add-always, GUI models,
GUI setup, view matrices, light cull, world setup, depth, frame root and job
dispatch. `EngineProfile.inc` is the authoritative RVA/hash list.

## Reproduce

```powershell
cmake -S . -B ../doomvr-memory-audit-pr-build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DKHARVOX_BUILD_LAYER=ON -DKHARVOX_BUILD_SFS_COMPILER=OFF
python -B tools/inventory_game_memory.py --output ../doomvr-memory-audit-pr-build/inventory
python -B tests/test_game_memory_inventory.py
cmake --build ../doomvr-memory-audit-pr-build --config Release --target KharvoxLayer KharvoxGameMemorySafetyTests KharvoxGamePatternScanTests KharvoxVirtualTextureGuardTests KharvoxAerCameraPairCacheTests KharvoxEngineMemoryCapacityTests KharvoxForeignProcessLayerTests KharvoxHudMenuPolicyTests KharvoxCameraBasisPolicyTests KharvoxGameImageLifetimeTests KharvoxDoomViewEffectsTests KharvoxBodyCameraSnapshotTests KharvoxPhysicsOriginTests KharvoxMuzzleBranchTests KharvoxMenuVtableHookTests
ctest --test-dir ../doomvr-memory-audit-pr-build -C Release -R "^(game-memory-safety|game-pattern-scan|virtual-texture-guard|aer-camera-pair-cache|engine-memory-capacity|foreign-process-layer|hud-menu-policy|camera-basis-policy|game-image-lifetime|doom-view-effects|body-camera-snapshot|physics-origin|muzzle-branch|menu-vtable-hook)$" --output-on-failure
```

The index scans source directories for pointer/cast/offset, detour, protection,
signature, probe and publication occurrences. It includes false positives and
cannot find all implicit dereferences, aliases or assembly effects. CSVs stay in
the external build directory so line-number churn does not enter source control.
The revised PR working tree produced 9,310 occurrences in 23 CSVs. The earlier audit
workspace produced 9,317; that workspace included benchmark edits and duplicate
helpers superseded by KH-07/KH-08. Neither count means verified hook contracts.

`game-memory-safety` exercises cross-page copies, guard/noaccess/execute-only,
read-only/decommitted/freed memory, overflow, image limits, malformed headers and
sections, duplicate signatures, bounded imports, unwind cycles, protection races,
multiwriter publication/invalidation, out-of-order captures and boolean CAS.
The existing `muzzle-branch` check executes the production hook during concurrent
toggles and verifies unchanged instruction bytes. It now also tests unreadable
signatures. `game-pattern-scan` includes the production parser/scanner directly.

Local verification: MSVC x64 Release layer build passed. Fourteen targeted CTest checks
passed: game-memory-safety, game-pattern-scan, virtual-texture-guard,
aer-camera-pair-cache, engine-memory-capacity, foreign-process-layer,
hud-menu-policy, camera-basis-policy, game-image-lifetime, doom-view-effects,
body-camera-snapshot, physics-origin, muzzle-branch and menu-vtable-hook.
The Python inventory check passed. The VT test calls the real installer with an
explicit synthetic `Image`, then executes its installed jump and continuation.
It checks RX protection restoration, cache-flush calls, refusal of invalid images,
and thunk cleanup on pre-patch protection failures. The production entry point
still requires the supported main executable and image-owned storage.

The menu-vtable test injects a competing slot replacement between validation and
CAS, first/second protection-call failures, and a callback during installation.
It verifies that a lost CAS preserves the competing hook and leaves the original
unpublished. After a successful CAS, a restoration failure leaves the installed
hook and original intact and produces a diagnostic; no live rollback is attempted.
These are local MSVC/CTest results, not GitHub Actions or commit-status evidence.

No game executable patch installation, level teardown, VR headset run, release
packaging, sanitizer run or performance benchmark was performed. Supported-build
metadata comes from the existing Native profile. Optional SDK warnings concern
missing PSVR2 Toolkit/bHaptics packaging dependencies, not audit test failures.
