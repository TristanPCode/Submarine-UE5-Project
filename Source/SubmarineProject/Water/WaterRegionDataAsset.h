#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WaterRegionDataAsset.generated.h"

// ---------------------------------------------------------------------------
//  UWaterRegionDataAsset
//  Pure data container for one ocean region.
//  No logic, no tick. Read by AWaterRegionActor and UOceanSubsystem.
//
//  Gameplay authority:
//    BaseWaterHeight is the flat authoritative surface Z for this region.
//    Agitation and Turbidity are data-only in 6.1; wired to physics in 6.3.
//
//  Visual authority:
//    All rendering-adjacent values (colors, wave params, foam) are forwarded
//    to MPC_Ocean by UOceanSubsystem. Materials read from MPC only.
//    Nothing in this asset is ever read directly by a material.
// ---------------------------------------------------------------------------
UCLASS(BlueprintType)
class SUBMARINEPROJECT_API UWaterRegionDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:

    // Human-readable name for this region (used in logs and debug draws).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region")
    FName RegionName = NAME_None;

    // ---------------------------------------------------------------------------
    //  Gameplay - Water Height
    // ---------------------------------------------------------------------------

    // Authoritative flat water surface height for this region (world Z).
    // Physics components query UOceanSubsystem which resolves to this value.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Gameplay")
    float BaseWaterHeight = 0.f;

    // ---------------------------------------------------------------------------
    //  Gameplay - Environment Data
    // ---------------------------------------------------------------------------

    // Wave agitation intensity [0..1].
    // 0 = perfectly calm. 1 = maximum storm.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Environment",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float AgitationIntensity = 0.1f;

    // Water turbidity / suspended particle density [0..1].
    // Drives underwater visibility range via PostProcess and MPC_Ocean.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Environment",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Turbidity = 0.2f;

    // -----------------------------------------------------------------------
    //  Regional Physics
    // -----------------------------------------------------------------------

    /**
     * Drag multiplier for submarines in this region.
     * 1.0 = no change from baseline.
     * 1.2 = 20% more drag (heavy/murky water, storm region).
     * 0.9 = 10% less drag (calm tropical water).
     * Applied multiplicatively on top of the submarine's existing drag coefficients.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Physics",
        meta = (ClampMin = "0.1", ClampMax = "3.0"))
    float RegionalDragMultiplier = 1.f;

    /**
     * If true, this region's drag multiplier also affects torpedoes.
     * Disabled by default -- torpedoes are fast and numerous, so torpedo drag
     * is only affected when you explicitly want the region to feel heavy.
     * Example use: extremely dense underwater kelp forest region.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Physics")
    bool bAffectTorpedoes = false;

    // -----------------------------------------------------------------------
    //  Rendering - Wave Parameters
    //  Forwarded to MPC_Ocean. Never read directly by gameplay.
    // -----------------------------------------------------------------------

    // Macro wave peak height (world units, cm).
    // Scales World Position Offset displacement in the surface material.
    // Pure visual: does NOT affect the authoritative water height.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Rendering|Waves",
        meta = (ClampMin = "0.0"))
    float WaveAmplitude = 80.f;

    // Global animation speed multiplier for all wave layers.
    // 1.0 = normal speed. 2.0 = twice as fast. 0.0 = frozen.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Rendering|Waves",
        meta = (ClampMin = "0.0"))
    float WaveSpeed = 1.0f;

    // Dominant wave propagation direction (world XY, should be normalized).
    // The material derives secondary octave directions automatically (+/- 30 deg).
    // Also drives the editor ArrowComponent on AWaterRegionActor.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Rendering|Waves")
    FVector2D WaveDirection = FVector2D(1.f, 0.f);

    // World-space UV tiling scale for wave sampling.
    // Larger values = fewer, larger waves. Smaller = more, tighter waves.
    // Typical range: 20000 (tight chop) to 100000 (large ocean swell).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Rendering|Waves",
        meta = (ClampMin = "1000.0"))
    float WaveTilingScale = 50000.f;

    /**
     * Wave length scale multiplier. Controls the spatial frequency of waves.
     * In the material: WaveK_Oct1 = WaveLengthScale * BaseWaveK_Oct1
     * where BaseWaveK_Oct1 is your material parameter default.
     * Values < 1.0 = longer waves (ocean swell). Values > 1.0 = shorter waves (chop).
     * Default 1.0 = no change from material default wave numbers.
     *
     * IMPORTANT: The base per-octave wave numbers (BaseWaveK_Oct1..4) live in
     * C++ as the single source of truth (OceanGerstner::WaveK in OceanTypes.h).
     * They are written to MPC as Ocean_PrimaryWaveK (+ octave variants) so the
     * material and physics use identical values. This WaveLengthScale multiplies
     * them per-region. To change base wavelengths, edit OceanGerstner::WaveK.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave",
        meta = (ClampMin = "0.1", ClampMax = "10.0"))
    float WaveLengthScale = 1.0f;

    // -----------------------------------------------------------------------
    //  Rendering - Water Colors
    //  Forwarded to MPC_Ocean. Drives Single Layer Water absorption.
    // -----------------------------------------------------------------------

    // Water color in shallow/near-surface areas.
    // Visible where depth fade is minimal (near the camera waterline).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Rendering|Colors")
    FLinearColor WaterShallowColor = FLinearColor(0.05f, 0.25f, 0.30f, 1.f);

    // Water color at depth / scattering color.
    // Drives the deep absorption tint in Single Layer Water.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Rendering|Colors")
    FLinearColor WaterDeepColor = FLinearColor(0.01f, 0.05f, 0.15f, 1.f);

    // Absorption scale divisor for Single Layer Water.
    // Controls how quickly light is absorbed with depth at map scale.
    // Higher value = clearer water = longer visibility range.
    // Tune this to your map scale:
    //   ~20000  = very dark / murky
    //   ~50000  = standard ocean
    //   ~150000 = very clear tropical water
    // This value is divided into WaterDeepColor and WaterShallowColor before
    // being passed to the Single Layer Water material. It compensates for
    // UE5's absorption being designed for meter-scale scenes, not large maps.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Rendering|Colors",
        meta = (ClampMin = "1000.0"))
    float AbsorptionScale = 50000.f;

    // Underwater tint when the camera is below the water surface.
    // Used by the underwater post-process in Phase 6.4.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Rendering|Colors")
    FLinearColor UnderwaterColor = FLinearColor(0.02f, 0.12f, 0.25f, 1.f);

    // -----------------------------------------------------------------------
    //  Rendering - Foam
    // -----------------------------------------------------------------------

    // Foam coverage intensity [0..1].
    // 0 = no foam. 1 = heavy foam on all wave crests.
    // Scales the foam mask threshold in the surface material.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Rendering|Foam",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float FoamIntensity = 0.2f;

    // -----------------------------------------------------------------------
    //  Rendering - Refraction
    // -----------------------------------------------------------------------

    // Strength of underwater refraction distortion [0..1].
    // 0 = no distortion. 1 = maximum distortion.
    // Used by Single Layer Water refraction offset in the surface material.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Rendering|Refraction",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float RefractionStrength = 0.5f;

    // ---------------------------------------------------------------------------
    //  Blending
    // ---------------------------------------------------------------------------

    // Distance over which this region blends into adjacent regions or the
    // default fallback. Evaluated by AWaterRegionActor::EvaluateWeight().
    // A value of 0 means hard edges (no blending).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Blending",
        meta = (ClampMin = "0.0"))
    float BlendRadius = 500.f;

    // Priority used when multiple regions overlap at the same position.
    // Higher value wins before blending is applied.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region|Blending")
    int32 Priority = 0;
};