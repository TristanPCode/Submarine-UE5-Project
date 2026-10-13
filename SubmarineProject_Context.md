# SubmarineProject — Project Context

> Knowledge file for the Claude Project. Update the "Current Status" and "Open Items" sections whenever a system moves forward. Items marked **(confirm)** were inferred from past sessions and should be checked by Tristan.

---

## 1. The game

- **What:** Local split-screen submarine combat game. 2 human players (split-screen) + CPU submarines, torpedoes, radar, death replay.
- **Developer:** Tristan, solo.
- **Engine:** Unreal Engine **5.8** (upgraded from 5.7 in October 2026 on a new PC). C++ heavy, Blueprints/Materials for assets.
- **Toolchain:** Visual Studio 2026 (MSVC 14.51), Windows, **French UE/VS install** (logs and editor labels are in French).
- **Repo:** GitHub. Project path: `C:\Users\Tristan\Documents\Unreal Projects\Submarine-UE5-Project\`
- **Module:** `SubmarineProject`, API macro `SUBMARINEPROJECT_API`.
- **Art direction (ocean):** stylized, readable, cinematic — not photoreal. Gameplay readability first.

---

## 2. How Tristan wants to work (rules for Claude)

1. **List every change explicitly, per file**: file name, function, what changed and why. Never "updated the file" without detail.
2. **Read the actual file before editing it.** Don't describe or patch from memory.
3. **Don't over-claim.** If unsure (engine API, product feature, root cause), say so and propose a test. Tristan tests hypotheses and will catch wrong ones.
4. **Validation-driven, not architecture-driven:** build measurement/debug tools first, let numbers decide whether a feature is needed.
5. **Isolate tunable logic in its own function** so it can be retuned later (e.g. `ComputeSurfaceNerfedPitch`).
6. **All tuning exposed in DataAssets** with physical meaning and units in the doc comment (cm, deg, s, deg/s, cm/s²).
7. **DA-based log toggles** per subsystem (`bLogX` flags in debug settings DAs), not hardcoded logs.
8. Keep the existing systems intact; extend rather than rewrite unless asked.
9. When something is editor-side (material graph, Blueprint), give step-by-step node instructions; Tristan shares screenshots of graphs.
10. Prefer ASCII in source strings (no em-dashes `—` in `TEXT()` literals — they broke the 5.8 build, see §8).

---

## 3. Core architecture rules (inviolable)

- **Gameplay/physics NEVER reads GPU data.** `UOceanSubsystem` (CPU, `UGameInstanceSubsystem`) is the single authority for water.
- **One-directional data flow:**
  `UWaterRegionDataAsset` → `AWaterRegionActor` → `UOceanSubsystem` → `MPC_Ocean` / per-MID params → Materials
- **Water regions are 3D volumes** (Box, Sphere, Cylinder, Spline, LayeredSpline) evaluated by XYZ. The **ocean surface is 2D** (XY only).
- **Canonical surface probe:** surface queries always probe at `GetDefaultWaterHeight()` (DefaultSurfaceZ) via `GetSurfaceHeightAtXY(XY)` — never the submarine's own depth. Using different Z probes caused a past physics/visual mismatch.
- **Shared canonical Gerstner** (`namespace OceanGerstner` in `OceanTypes.h`) is the single source of truth for waves, used bit-for-bit by physics and material:
  - `Phase_i = K_i · dot(Dir_i, XY) − (BaseSpeed · SpeedFraction_i) · Time`
  - `Z_i = (BaseAmp · AmpFraction_i) · sin(Phase_i)` (**sin, not cos**)
  - 4 octaves: `WaveK = {0.0001, 0.00017, 0.00023, 0.00033}`, `AmpFraction = {1.0, 0.5, 0.35, 0.2}`, `RotAngleDeg = {0, 30, -20, 50}`, `SpeedFraction = {1.0, 1.3, 1.7, 2.1}`
  - **Option A:** physics rides **octave 0 only** (the primary swell). Octaves 1–3 are visual detail.
  - C++ writes `Ocean_WaveK_Oct1..4` to `MPC_Ocean`; the material must read them from MPC (no hardcoded K in the graph).
- **Split-screen:** the ocean surface mesh is one shared MID for both viewports; per-player selection on the surface can't truly differ per view. Per-player effects belong in post-process.

---

## 4. Folder / file map (main C++)

`Source/SubmarineProject/`

- **Water/**: `OceanSubsystem.h/.cpp` (authority, `QuerySurface`, `EvaluateGerstnerOctave`, MPC writes, debug harness), `OceanTypes.h` (`OceanGerstner`, `FWaterSample`, `FOceanSurfaceSample`, `FOceanFrameCache`), `OceanSurfaceActor.h/.cpp` (surface + underside meshes/MIDs, `Ocean_MeshBaseZ`), `OceanHeightFieldGenerator.h/.cpp` (bakes regional height into `Ocean_HeightField` texture, pushes texture + MapMin/MapMax to MIDs, CPU readback copy), `WaterRegionDataAsset.h`, `WaterRegionActor` + Box/Sphere/Cylinder/Spline/LayeredSpline region classes, `OceanDebugSettings`, wake system.
- **Submarine/**: `SubmarinePawn.h/.cpp`, `SubmarinePhysicsComponent.h/.cpp`, `SubmarineCharacteristics.h/.cpp` (main tuning DA), collision component.
- **Torpedo/**: `TorpedoPawn`, `TorpedoPhysicsComponent`, `SubmarineTorpedoComponent`, `TorpedoCharacteristics`.
- **HUD/**: modular HUD (`PitchModule`, `PositionalIndicatorModule`, depthometer, ammo, engine state, radar), `HUDTransitionManager`, settings/debug DAs.
- **Replay/**: `ReplayGhostComponent`, `AReplayGhostActor`.
- **Death/**: `DeathSequenceComponent`. **CameraEdit/**: `ScreenFadeComponent`.
- **Input:** `USubmarineViewportClient` (keyboard partitioning), `IPlatformInputDeviceMapper` (gamepad → LocalPlayer remap).
- **Billboards:** `UBillboardDisplayComponent` (screen-space, per PlayerController), `USubmarineInfoBillboardComponent` (data only).

### Key editor assets
- `MPC_Ocean` — global params (per-player `_P1/_P2` wave/color/height values, `Ocean_MeshBaseZ`, `Ocean_WaveK_Oct1..4`, `Ocean_WaveLengthScale`, `Ocean_PlayerIndex`, `WaveLOD_FarDistance`, ...).
- `M_Ocean_Surface` (Single Layer Water), `M_Ocean_Underside` (translucent), `MF_Ocean_HeightOffset` (UV HeightField + Gerstner + height offset).
- **Per-MID material params (NOT MPC):** `Ocean_HeightField` (texture), `Ocean_HeightField_MapMin`, `Ocean_HeightField_MapMax`. Every material using the MF has its own copy; the generator must push them to both surface and underside MIDs.
- `SM_OceanPlane` — now **500×500 subdivisions** (fixed transition mismatch; vertex density, not texture resolution, was the issue).
- Height field: 1024 res, `PF_R16F` (≈4 UU quantization — accurate enough).

---

## 5. Roadmap

**Phase 6 — Ocean** (current focus)
- 6.1 Ocean global architecture (subsystem, regions, MPC) — **done**
- 6.2 Visual ocean surface (materials, Gerstner, foam, region DAs) — **done**
- 6.3 Surface interaction: wake, physics/visual consistency, height field, debug harness, pitch leveling — **in progress, nearly closed**
- 6.4 Underwater post-process / transitions — next
- 6.5 Caustics
- 6.6 Advanced submarine physics (QuerySurface is the hook)
- 6.7 Weather / polish (Gerstner XY displacement, etc.)
- 6.8 Optimization & scalability (Quality Switch octave counts, LOD, height-field res per quality, possibly Nanite/tessellated ocean)

Earlier phases (done): foundational submarine systems (Enhanced Input, speed state machine, pitch states with ghost states, 6DOF drag/buoyancy, cameras, torpedoes), collision, replay, screen fade, HUD modules, radar, billboards, split-screen input.

---

## 6. Current status (as of October 2026)

**Validated**
- Physics surface vs height field: residual < 4 UU everywhere, including region transitions.
- Rendered surface matches physics with waves off (after 500×500 mesh).
- Transition slot system **disabled** (was buggy, camera-dependent, unnecessary). Code still compiled but uncalled — can be deleted.

**Debug harness:** console `Ocean.DebugSurface 1` (backtick console in PIE), `Ocean.DebugMaxOctave 0..3`. On-screen readout top-left per player. Spheres: **white** = RegionalBase, **yellow** = PhysicsHeight, **orange** = VisualHeight (height field + waves). Cyan wireframe grid = physics surface around the sub.

**Pitch system (implemented, needs in-game feel test)**
- `TargetPitch` = **wished** pitch (player intent, shown on HUD via `GetCurrentPitch()`/`GetWishedPitch()`).
- `NerfedTargetPitch = ComputeSurfaceNerfedPitch(TargetPitch, DeltaTime)` — near the surface, limits upward pitch to the **forward surface slope** (from `QuerySurface` normal, primary octave only), low-passed + rate-limited, minus safety margin, blended by depth, then **clamped to ±MaxPitchAngle**.
- `CurrentPitch` = applied pitch, chases `NerfedTargetPitch` with the existing accel/decel controller.
- Anti-breach: `ComputeAntiBreachForce` in physics component, downward accel near the surface.
- DA params (`SubmarineCharacteristics`): `bEnableSurfacePitchLeveling`, `SurfaceLevelingDepth`, `SurfaceLevelingExponent`, `SurfacePitchSafetyMarginDeg`, `SurfaceSlopeResponseTime`, `SurfaceSlopeMaxRateDegPerSec`, `bEnableReverseAntiBreach`, `AntiBreachStartDepth`, `AntiBreachMaxAccel`.
- Assumes **positive pitch = nose up** **(confirm)**.

---

## 7. Open items / known problems

1. **Waves on: visuals sometimes far from physics.** Wavelength (K) reportedly already from MPC. Next suspects: wave **speed** and **direction** sources in the material vs C++ (`BaseSpeed·SpeedFraction`, rotation angles `0/30/-20/50`), and per-player `If(PlayerIndex)` selection on a shared surface.
2. **Waves appear to move very fast when the sub moves fast** — likely tied to item 1 (mismatched wavelength/speed); recheck after 1.
3. **Per-region waves bleed:** distant calm regions show the current region's waves because the surface uses one global wave set. Proposed fix: bake a **wave-parameter field texture** (like the height field) sampled by world XY. Decision pending: **amplitude-only** (1 channel, fastest) vs **full RGBA** (amp, speed, direction, wavelength). Do NOT revive the slot system.
4. **Designer wavelength control:** `WaveLengthScale` already flows C++ → MPC; material should multiply K by it.
5. Underside material must use the same WPO (height offset + Gerstner) as the surface.
6. Pitch leveling: in-game feel test, sign convention check, maybe make anti-breach reverse/velocity-gated.
7. Cleanup: delete dead slot machinery (`FTransitionSlotState`, `UpdateTransitionSlots`, `WriteTransitionSlotsMPC`, `EvaluateSlotCorrectionCPU`, `GetNearestBoundaryPoint` overrides, 64 `Slot_N_*` MPC params).
8. 5.8 upgrade follow-ups (non-blocking): `IncludeOrderVersion` still `Unreal5_7`; consider `Unreal5_8`. Engine deprecation warnings (`GetAssetRegistryTags`, landscape) come from engine headers — ignore.
9. Material cleanup (6.8): `MF_SelectPerPlayer` to replace ~15 repeated `If(PlayerIndex<0.5, P1, P2)` blocks; Quality Switch octave counts; LOD_Alpha lerp.

---

## 8. Environment notes / gotchas

- **UE 5.8 build settings:** `DefaultBuildSettings = BuildSettingsVersion.V7` in `SubmarineProjectEditor.Target.cs`. Do **not** use `BuildEnvironment = TargetBuildEnvironment.Unique` (not allowed with the Launcher/installed engine).
- **5.8 format-string checker:** `UE_LOG` format strings are validated at compile time. Non-ASCII bytes (corrupted em-dash `0x97`) inside `TEXT()` trigger `C2338 missing format specifier`. Fix: replace with ASCII `-`. A PowerShell sweep of `Source/` was used.
- **Building:** build only the game target (`SubmarineProjectEditor`). Failures in `LiveLinkHub`, `DotNetPerforceLib`, `EventLoopUnitTests`, `IoStoreOnDemandTests` are engine programs, irrelevant.
- **UE 5.7 crashed on this PC** (NPU driver `npu_d3d12_umd` via `NNERuntimeORT`, Bitdefender in stack); 5.8 works. Stay on 5.8.
- Compiler warning "MSVC 14.51 newer than preferred" is harmless.
- Commit to GitHub before any large change.
