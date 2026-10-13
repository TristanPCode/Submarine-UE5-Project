#pragma once

#include "CoreMinimal.h"
#include "OceanTypes.generated.h"

// ---------------------------------------------------------------------------
//  Canonical Gerstner wave definition
//
//  THE SINGLE SOURCE OF TRUTH for wave shape, shared by CPU physics and the
//  GPU material. The CPU reads these constants directly. The GPU receives the
//  fully resolved octaves via MPC Ocean_Oct1..4 (built by BuildOctave below)
//  and only evaluates Amp * sin(dot(KDir, XY) - TimePhase). If these values
//  change, BOTH physics and rendering update.
//
//  Per-octave shape (fractions/multipliers relative to the per-region base):
//    Octave 0 = PRIMARY wave. This is the only octave physics computes.
//               The submarine rides exactly this wave. It MUST match the
//               material's Oct1 bit-for-bit (same k, amp, dir, phase, speed).
//    Octaves 1-3 = visual detail. Material only. Physics never computes them.
//
//  Canonical phase formula (identical CPU and GPU):
//    Phase_i = K_i * dot(Dir_i, WorldXY) - (BaseSpeed * SpeedFraction_i) * Time
//    Z_i     = (BaseAmplitude * AmpFraction_i) * sin(Phase_i)
//
//  NOTE: sin, not cos. The material uses sin. Physics must use sin too.
//  K_i      = BasePrimaryWaveK_i * WaveLengthScale  (per-region dynamic scale)
//  Dir_i    = BaseDirection rotated by RotAngleDeg_i
// ---------------------------------------------------------------------------
namespace OceanGerstner
{
    // Number of octaves the material renders. Physics uses only octave 0.
    static constexpr int32 NumOctaves = 4;

    // Per-octave wave number (k = 2*PI / wavelength) at WaveLengthScale = 1.
    // These are the material's WaveK_Oct1..4 defaults. Single source here.
    static constexpr float WaveK[NumOctaves] = { 0.0001f, 0.00017f, 0.00023f, 0.00033f };

    // Per-octave amplitude fraction of the region's BaseAmplitude.
    static constexpr float AmpFraction[NumOctaves] = { 1.0f,    0.5f,     0.35f,    0.2f };

    // Per-octave direction rotation from the base wave direction (degrees).
    static constexpr float RotAngleDeg[NumOctaves] = { 0.0f,    30.0f,    -20.0f,   50.0f };

    // Per-octave speed fraction of the region's BaseSpeed.
    static constexpr float SpeedFraction[NumOctaves] = { 1.0f,   1.3f,     1.7f,     2.1f };

    // The primary octave index physics rides.
    static constexpr int32 PrimaryOctave = 0;

    // -----------------------------------------------------------------------
    //  Resolved octave (single code path for physics AND material)
    //
    //  BuildOctave() is the ONLY place that applies WaveK, WaveLengthScale,
    //  direction normalization, RotAngleDeg, AmpFraction and SpeedFraction.
    //  Physics evaluates the result with EvaluateZ(). The subsystem writes the
    //  same result to MPC Ocean_Oct1..4 via ToMPC(), and the material only does:
    //      Z = Oct.b * sin( dot(Oct.rg, WorldXY) - Oct.a )
    //  So the material holds no wave constants and cannot drift from C++.
    // -----------------------------------------------------------------------
    struct FOctave
    {
        // K_i * normalized rotated direction (rad/cm). Zero if direction is degenerate.
        FVector2D KDir = FVector2D::ZeroVector;

        // Amplitude (cm) = BaseAmplitude * AmpFraction_i.
        float Amp = 0.f;

        // Angular speed (rad/s) = BaseSpeed * SpeedFraction_i.
        float Omega = 0.f;
    };

    inline FOctave BuildOctave(int32 OctaveIndex, float BaseAmplitude, float BaseSpeed,
        const FVector2D& BaseDirection, float WaveLengthScale)
    {
        FOctave O;
        if (OctaveIndex < 0 || OctaveIndex >= NumOctaves) return O;

        const float K = WaveK[OctaveIndex] * FMath::Max(WaveLengthScale, 0.0001f);
        const FVector2D BaseDir = BaseDirection.GetSafeNormal();
        const float RotRad = FMath::DegreesToRadians(RotAngleDeg[OctaveIndex]);
        const float CosR = FMath::Cos(RotRad);
        const float SinR = FMath::Sin(RotRad);
        const FVector2D Dir(
            BaseDir.X * CosR - BaseDir.Y * SinR,
            BaseDir.X * SinR + BaseDir.Y * CosR);

        O.KDir = Dir * K;
        O.Amp = FMath::Max(BaseAmplitude, 0.f) * AmpFraction[OctaveIndex];
        O.Omega = BaseSpeed * SpeedFraction[OctaveIndex];
        return O;
    }

    // Time part of the phase (rad), wrapped to [0, 2*PI) in double precision.
    // Wrapping is exact for sin() and keeps the GPU float small at long play times.
    inline float TimePhase(float Omega, float Time)
    {
        const double P = static_cast<double>(Omega) * static_cast<double>(Time);
        return static_cast<float>(FMath::Fmod(P, 2.0 * UE_DOUBLE_PI));
    }

    // Phase_i = dot(KDir_i, XY) - Omega_i * Time
    inline float Phase(const FOctave& O, const FVector2D& XY, float Time)
    {
        return static_cast<float>(O.KDir.X * XY.X + O.KDir.Y * XY.Y)
            - TimePhase(O.Omega, Time);
    }

    // Z_i = Amp_i * sin(Phase_i)   (sin, not cos -- material uses sin too)
    inline float EvaluateZ(const FOctave& O, const FVector2D& XY, float Time)
    {
        return O.Amp * FMath::Sin(Phase(O, XY, Time));
    }

    // Packing written to MPC Ocean_Oct1..4: (KDir.x, KDir.y, Amp, TimePhase).
    inline FLinearColor ToMPC(const FOctave& O, float Time)
    {
        return FLinearColor(
            static_cast<float>(O.KDir.X), static_cast<float>(O.KDir.Y),
            O.Amp, TimePhase(O.Omega, Time));
    }

    // CPU mirror of the material's octave node, reading the packed MPC value.
    // Used by the debug harness to reconstruct exactly what the GPU receives.
    inline float EvaluateZFromMPC(const FLinearColor& Packed, const FVector2D& XY)
    {
        const float P = static_cast<float>(Packed.R * XY.X + Packed.G * XY.Y) - Packed.A;
        return Packed.B * FMath::Sin(P);
    }
}

// ---------------------------------------------------------------------------
//  FOceanSurfaceSample
//  Result of UOceanSubsystem::QuerySurface(). The canonical surface query
//  reused by physics now and by Phase 6.6 advanced buoyancy/righting later.
// ---------------------------------------------------------------------------
USTRUCT(BlueprintType)
struct FOceanSurfaceSample
{
    GENERATED_BODY()

    // Exact blended regional base height (world Z), no waves. CPU-authoritative.
    UPROPERTY(BlueprintReadOnly)
    float RegionalBaseHeight = 0.f;

    // Primary (octave 0) Gerstner Z displacement at this XY and time.
    // This is the rideable wave -- matches the material's Oct1 exactly.
    UPROPERTY(BlueprintReadOnly)
    float PrimaryWaveOffset = 0.f;

    // Full surface height = RegionalBaseHeight + PrimaryWaveOffset.
    // This is the absolute, full-amplitude surface (no NearSurfaceAlpha here).
    UPROPERTY(BlueprintReadOnly)
    float SurfaceHeight = 0.f;

    // Analytic surface normal from the primary wave's slope (closed form).
    // Unused in 6.3; the hook for 6.6 buoyancy torque / righting forces.
    UPROPERTY(BlueprintReadOnly)
    FVector SurfaceNormal = FVector::UpVector;
};

// ---------------------------------------------------------------------------
//  FOceanSampleRequest
//  Registered by any actor (player submarine, CPU submarine, torpedo spawner)
//  that wants its position pre-warmed in the frame cache each tick.
//  Registration happens at BeginPlay, unregistration at EndPlay.
// ---------------------------------------------------------------------------
USTRUCT()
struct FOceanSampleRequest
{
    GENERATED_BODY()

    // The actor whose position should be sampled each frame.
    // Stored as a weak pointer: if the actor is destroyed the entry is
    // silently skipped and cleaned up during the next cache update.
    TWeakObjectPtr<AActor> Actor;

    // Optional debug label shown in ocean debug visualization.
    FName DebugLabel = NAME_None;

    // True if this is a local player submarine (gets a dedicated cache slot
    // and drives the MPC per-player underwater flags).
    bool bIsLocalPlayer = false;

    // Local player index (0 or 1 for split-screen). Ignored if not a player.
    int32 PlayerIndex = -1;
};

// ---------------------------------------------------------------------------
//  FWaterSample
//  Batched query result returned by UOceanSubsystem::SampleWaterAt().
//  All gameplay-relevant water data for a given world position.
//  Extended with rendering parameters for MPC writes.
// ---------------------------------------------------------------------------
USTRUCT(BlueprintType)
struct FWaterSample
{
    GENERATED_BODY()

    // -----------------------------------------------------------------------
    //  Gameplay (authoritative)
    // -----------------------------------------------------------------------

    // Authoritative water surface height at the queried position (world Z)
    UPROPERTY(BlueprintReadOnly)
    float WaterHeight = 0.f;

    // Agitation intensity [0..1]. Data only in 6.1, wired to physics in 6.3.
    UPROPERTY(BlueprintReadOnly)
    float AgitationIntensity = 0.f;

    // Turbidity [0..1]. Drives fog density and underwater visibility.
    UPROPERTY(BlueprintReadOnly)
    float Turbidity = 0.f;

    // Whether the queried position is currently below the water surface.
    UPROPERTY(BlueprintReadOnly)
    bool bIsUnderwater = false;

    // -----------------------------------------------------------------------
    //  Rendering hints (forwarded to MPC, never read back into gameplay)
    // -----------------------------------------------------------------------

    // Blended underwater tint for this position.
    UPROPERTY(BlueprintReadOnly)
    FLinearColor UnderwaterColor = FLinearColor(0.02f, 0.12f, 0.25f, 1.f);

    // Wave parameters for this position.
    UPROPERTY(BlueprintReadOnly)
    float WaveAmplitude = 80.f;

    UPROPERTY(BlueprintReadOnly)
    float WaveSpeed = 1.0f;

    UPROPERTY(BlueprintReadOnly)
    FVector2D WaveDirection = FVector2D(1.f, 0.f);

    UPROPERTY(BlueprintReadOnly)
    float WaveTilingScale = 50000.f;

    UPROPERTY(BlueprintReadOnly)
    float WaveLengthScale = 1.0f;

    // Water colors.
    UPROPERTY(BlueprintReadOnly)
    FLinearColor WaterShallowColor = FLinearColor(0.05f, 0.25f, 0.30f, 1.f);

    UPROPERTY(BlueprintReadOnly)
    FLinearColor WaterDeepColor = FLinearColor(0.01f, 0.05f, 0.15f, 1.f);

    // Absorption scale divisor passed to Single Layer Water
    UPROPERTY(BlueprintReadOnly)
    float AbsorptionScale = 50000.f;

    // Foam and refraction.
    UPROPERTY(BlueprintReadOnly)
    float FoamIntensity = 0.2f;

    UPROPERTY(BlueprintReadOnly)
    float RefractionStrength = 0.5f;
};


// ---------------------------------------------------------------------------
//  FOceanFrameCache
//  Computed once per subsystem tick. All physics queries during that tick
//  read from here rather than re-evaluating regions.
//
//  Supports N registered actors (not just 2 players).
//  Player slots [0..1] are kept separate for MPC per-player writes.
//  All other actors (CPU submarines, torpedoes) use the general pool.
// ---------------------------------------------------------------------------
USTRUCT()
struct FOceanFrameCache
{
    GENERATED_BODY()

    // Cached samples for local players (index = player index, max 2).
    // Used for MPC per-player parameter writes (underwater flags, heights).
    FWaterSample PlayerSamples[2];

    // Cached samples for all registered non-player actors this frame.
    // Key = actor raw pointer (valid only within one frame, never stored).
    TMap<AActor*, FWaterSample> ActorSamples;

    // Global blended values derived from player 0 (or fallback).
    // Used for rendering parameters that are not per-player.
    float GlobalWaterHeight = 0.f;
    float GlobalAgitation = 0.f;
    float GlobalTurbidity = 0.2f;
    float GlobalWaveAmplitude = 80.f;
    float GlobalWaveSpeed = 1.0f;
    FVector2D GlobalWaveDirection = FVector2D(1.f, 0.f);
    float GlobalWaveTilingScale = 50000.f;
    float GlobalWaveLengthScale = 1.0f;
    FLinearColor GlobalWaterShallowColor = FLinearColor(0.05f, 0.25f, 0.30f, 1.f);
    FLinearColor GlobalWaterDeepColor = FLinearColor(0.01f, 0.05f, 0.15f, 1.f);
    FLinearColor GlobalUnderwaterColor = FLinearColor(0.02f, 0.12f, 0.25f, 1.f);
    float GlobalAbsorptionScale = 50000.f;
    float GlobalFoamIntensity = 0.2f;
    float GlobalRefractionStrength = 0.5f;

    // Packed octaves written to MPC Ocean_Oct1..4 this frame
    // (see OceanGerstner::ToMPC). Kept here so the debug harness can
    // reconstruct exactly what the material received.
    FLinearColor MPCOctaves[OceanGerstner::NumOctaves] = {
        FLinearColor::Transparent, FLinearColor::Transparent,
        FLinearColor::Transparent, FLinearColor::Transparent };

    // Game time (s) used to build MPCOctaves this frame.
    float WaveTime = 0.f;

    // True once the subsystem has computed values this frame.
    bool bValid = false;
};