#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Curves/CurveFloat.h"
#include "RadarSettings.generated.h"

/**
 * ERadarDetectionState
 *
 * Priority order (highest to lowest):
 *   ClearID > VulnerableID > NormalDetection > WeakDetection
 */
UENUM(BlueprintType)
enum class ERadarDetectionState : uint8
{
    WeakDetection,
    NormalDetection,
    VulnerableID,
    ClearID
};

/**
 * ERadarEntityType
 *
 * Used by Unknown until identification threshold is reached.
 */
UENUM(BlueprintType)
enum class ERadarEntityType : uint8
{
    Unknown,
    Submarine,
    Torpedo
};

/**
 * URadarSettings
 *
 * Single DataAsset driving all radar and vulnerability behaviour.
 * Assign one instance to USubmarineCharacteristics or directly to
 * URadarComponent from the Blueprint editor.
 *
 * Design rules:
 *   - No gameplay constants are hardcoded in C++.
 *   - All range multipliers are driven by FRuntimeFloatCurve so designers
 *     can tune response curves without recompiling.
 *   - Vulnerability thresholds A and B map [0,A] -> Low, [A,B] -> Normal,
 *     [B,1] -> High vulnerability.
 */
UCLASS(BlueprintType)
class SUBMARINEPROJECT_API URadarSettings : public UDataAsset
{
    GENERATED_BODY()

public:

    // -----------------------------------------------------------------------
    //  Radar Scan
    // -----------------------------------------------------------------------

    /** Cooldown between radar scans (seconds). 0 = no cooldown. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Scan",
        meta = (ClampMin = "0"))
    float ScanCooldown = 5.f;

    // -----------------------------------------------------------------------
    //  Detection ranges (base values, before vulnerability multipliers)
    // -----------------------------------------------------------------------

    /** Maximum circle detection range (cm). Entity is weakly detected if inside. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Ranges",
        meta = (ClampMin = "0"))
    float CircleDetectionRange = 5000.f;

    /** Maximum FOV detection range (cm). Requires entity inside FOV cone too. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Ranges",
        meta = (ClampMin = "0"))
    float FOVDetectionRange = 8000.f;

    /**
     * Base periscope detection range (cm).
     * Actual range = PeriscopeBaseRange * Zoom * (1 + (1 - Zoom) * BoostZoomDetection)
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Ranges",
        meta = (ClampMin = "0"))
    float PeriscopeBaseRange = 12000.f;

    /**
     * Distance threshold for ClearID (cm).
     * Entity must be inside this range AND inside FOV to reach ClearID.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Ranges",
        meta = (ClampMin = "0"))
    float IdentificationRange = 3000.f;

    /**
     * Very short distance override (cm).
     * Low vulnerability submarines can STILL be detected if closer than this.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Ranges",
        meta = (ClampMin = "0"))
    float LowVulnerabilityOverrideRange = 500.f;

    // -----------------------------------------------------------------------
    //  FOV parameters
    // -----------------------------------------------------------------------

    /**
     * Half-angle of the detection cone in degrees (each side from forward).
     * E.g. 45 = 90° total FOV cone.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|FOV",
        meta = (ClampMin = "1", ClampMax = "180"))
    float FOVHalfAngleDegrees = 45.f;

    /**
     * How much zoom reduces FOV half-angle.
     * FinalHalfAngle = FOVHalfAngleDegrees / (1 + (Zoom - 1) * ZoomFOVReductionFactor)
     * ZoomFOVReductionFactor = 1 means zoom doubles -> half angle halves.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|FOV",
        meta = (ClampMin = "0"))
    float ZoomFOVReductionFactor = 1.f;

    /**
     * Multiplier applied to the FOV half-angle for detection purposes.
     * > 1.0 widens the detection cone (e.g. 1.5 = 50% wider than camera FOV).
     * < 1.0 narrows it (e.g. 0.5 = only centre half of the camera FOV detects).
     * Default 1.0 = detection cone matches camera FOV exactly.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|FOV",
        meta = (ClampMin = "0.01"))
    float FOVDetectionAngleMultiplier = 1.f;

    // -----------------------------------------------------------------------
    //  Zoom
    // -----------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Zoom",
        meta = (ClampMin = "0.1"))
    float ZoomMin = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Zoom",
        meta = (ClampMin = "0.1"))
    float ZoomMax = 3.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Zoom")
    float ZoomDefault = 1.f;

    /** How much one scroll step changes zoom. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Zoom",
        meta = (ClampMin = "0.01"))
    float ZoomStep = 0.2f;

    /**
     * Boost coefficient for periscope range at zoom > 1.
     * Formula: PeriscopeRange * Zoom * (1 + (1 - Zoom) * BoostZoomDetection)
     * Note: when Zoom > 1, (1 - Zoom) is negative -> acts as a boost limiter.
     * Set to 0 to disable boost entirely (linear zoom scaling only).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Zoom")
    float BoostZoomDetection = -0.3f;

    // -----------------------------------------------------------------------
    //  Vulnerability score weights
    // -----------------------------------------------------------------------

    /** Contribution weight for movement. Clamped to [0,1] before weighting. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Vulnerability",
        meta = (ClampMin = "0", ClampMax = "1"))
    float MovementWeight = 0.2f;

    /** Contribution weight for radar usage. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Vulnerability",
        meta = (ClampMin = "0", ClampMax = "1"))
    float RadarWeight = 0.35f;

    /** Contribution weight for torpedo firing. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Vulnerability",
        meta = (ClampMin = "0", ClampMax = "1"))
    float TorpedoWeight = 0.45f;

    /**
     * How fast the radar contribution decays per second after triggering.
     * A value of 1.0 means the contribution drops to 0 over 1 second.
     * Typically set to 1/RadarCooldown.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Vulnerability",
        meta = (ClampMin = "0.01"))
    float RadarContributionDecayRate = 0.5f;

    /** Decay rate for torpedo contribution (per second). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Vulnerability",
        meta = (ClampMin = "0.01"))
    float TorpedoContributionDecayRate = 0.333f;

    /**
     * Vulnerability score threshold A: [0, ThresholdA] = Low vulnerability.
     * Must be < ThresholdB.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Vulnerability",
        meta = (ClampMin = "0", ClampMax = "1"))
    float VulnerabilityThresholdA = 0.2f;

    /**
     * Vulnerability score threshold B: [ThresholdA, ThresholdB] = Normal.
     * [ThresholdB, 1] = High vulnerability.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Vulnerability",
        meta = (ClampMin = "0", ClampMax = "1"))
    float VulnerabilityThresholdB = 0.6f;

    /**
     * Activate displaying automatically on the radar map identified objects from FOV
     * Option to false will require Radar scan to see it on the radar map.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Vulnerability")
    bool bFOVAutoDetection = true;

    /**
     * Activate displaying automatically on the radar map torpedoes
     * Option to false will require Radar scan to see it on the radar map.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Vulnerability")
    bool bTorpedoAutoDisplay = true;

    /**
     * If true, own torpedoes (fired by this submarine) are also shown on the radar.
     * Useful for tracking your own shots. Off by default.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Vulnerability")
    bool bOwnTorpedoDetect = false;

    // -----------------------------------------------------------------------
    //  Range multiplier curves  (X = VulnerabilityScore 0..1, Y = multiplier)
    // -----------------------------------------------------------------------

    /**
     * Multiplies CircleDetectionRange based on target vulnerability.
     * Low score -> near 0 (circle blocked). High score -> 1 or above.
     * Default: flat 1.0 — override in editor to tune behaviour.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Curves")
    FRuntimeFloatCurve CircleRangeMultiplierCurve;

    /**
     * Multiplies FOVDetectionRange based on target vulnerability.
     * FOV detection is less affected by low vulnerability.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Curves")
    FRuntimeFloatCurve FOVRangeMultiplierCurve;

    /**
     * Multiplies IdentificationRange based on target vulnerability.
     * High vulnerability -> identification is possible at longer ranges.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Curves")
    FRuntimeFloatCurve IDRangeMultiplierCurve;

    // -----------------------------------------------------------------------
    //  Detection lifetimes
    // -----------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Lifetimes",
        meta = (ClampMin = "0"))
    float DefaultDetectionLifetime = 5.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Lifetimes",
        meta = (ClampMin = "0"))
    float DefaultDisplayLifetime = 4.f;

    /** Per-state overrides. 0 = use DefaultDetectionLifetime. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Lifetimes")
    float WeakDetectionLifetime = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Lifetimes")
    float NormalDetectionLifetime = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Lifetimes")
    float VulnerableIDLifetime = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Lifetimes")
    float ClearIDLifetime = 0.f;

    /** Per-state display lifetime overrides. 0 = use DefaultDisplayLifetime. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Lifetimes")
    float WeakDisplayLifetime = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Lifetimes")
    float NormalDisplayLifetime = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Lifetimes")
    float VulnerableIDDisplayLifetime = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Lifetimes")
    float ClearIDDisplayLifetime = 0.f;

    // -----------------------------------------------------------------------
    //  Fade
    // -----------------------------------------------------------------------

    /**
     * When DisplayTimeRemaining < FadeDuration, the icon fades linearly to 0.
     * Set to 0 to disable fading.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Fade",
        meta = (ClampMin = "0"))
    float FadeDuration = 1.f;

    // -----------------------------------------------------------------------
    //  Display thresholds
    // -----------------------------------------------------------------------

    /**
     * Entity icons are hidden when their radar position exceeds this fraction
     * of the radar circle radius. e.g. 0.9 = 90% of the visual circle.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Display",
        meta = (ClampMin = "0", ClampMax = "1"))
    float EntityHideThreshold = 0.9f;

    /**
     * World range mapped to the full radar circle radius (cm).
     * Entities beyond this world distance are clamped to the edge.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Display",
        meta = (ClampMin = "1"))
    float RadarWorldRange = 10000.f;

    /**
     * World-space direction that maps to "up" on the radar display.
     * Default (1,0) = world +X (UE forward) points up.
     * Set to (0,1) if your level north is along world +Y, etc.
     * Does not need to be normalized -- normalized at runtime.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Display")
    FVector2D NorthDirection = FVector2D(1.f, 0.f);

    /**
     * Angle offset (degrees) applied to torpedo icon rotation.
     * Compensates for texture orientation (e.g. texture points right by default).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Display")
    float TorpedoIconAngleOffset = -90.f;

    // -----------------------------------------------------------------------
    //  Debug
    // -----------------------------------------------------------------------

    /**
     * Angle offset (degrees) applied to torpedo icon rotation.
     * Compensates for texture orientation (e.g. texture points right by default).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Debug")
    bool bRadarComponentDebug = false;

    // -----------------------------------------------------------------------
    //  Helpers
    // -----------------------------------------------------------------------

    /** Returns the detection lifetime for the given state, falling back to default. */
    float GetDetectionLifetime(ERadarDetectionState State) const
    {
        switch (State)
        {
        case ERadarDetectionState::WeakDetection:
            return WeakDetectionLifetime > 0.f ? WeakDetectionLifetime : DefaultDetectionLifetime;
        case ERadarDetectionState::NormalDetection:
            return NormalDetectionLifetime > 0.f ? NormalDetectionLifetime : DefaultDetectionLifetime;
        case ERadarDetectionState::VulnerableID:
            return VulnerableIDLifetime > 0.f ? VulnerableIDLifetime : DefaultDetectionLifetime;
        case ERadarDetectionState::ClearID:
            return ClearIDLifetime > 0.f ? ClearIDLifetime : DefaultDetectionLifetime;
        }
        return DefaultDetectionLifetime;
    }

    float GetDisplayLifetime(ERadarDetectionState State) const
    {
        switch (State)
        {
        case ERadarDetectionState::WeakDetection:
            return WeakDisplayLifetime > 0.f ? WeakDisplayLifetime : DefaultDisplayLifetime;
        case ERadarDetectionState::NormalDetection:
            return NormalDisplayLifetime > 0.f ? NormalDisplayLifetime : DefaultDisplayLifetime;
        case ERadarDetectionState::VulnerableID:
            return VulnerableIDDisplayLifetime > 0.f ? VulnerableIDDisplayLifetime : DefaultDisplayLifetime;
        case ERadarDetectionState::ClearID:
            return ClearIDDisplayLifetime > 0.f ? ClearIDDisplayLifetime : DefaultDisplayLifetime;
        }
        return DefaultDisplayLifetime;
    }

    /**
     * Evaluate CircleRangeMultiplierCurve at the given vulnerability score.
     * Falls back to 1.0 if no curve key exists.
     */
    float EvalCircleMultiplier(float Score) const
    {
        const FRichCurve* C = CircleRangeMultiplierCurve.GetRichCurveConst();
        return (C && C->GetNumKeys() > 0) ? C->Eval(Score) : 1.f;
    }

    float EvalFOVMultiplier(float Score) const
    {
        const FRichCurve* C = FOVRangeMultiplierCurve.GetRichCurveConst();
        return (C && C->GetNumKeys() > 0) ? C->Eval(Score) : 1.f;
    }

    float EvalIDMultiplier(float Score) const
    {
        const FRichCurve* C = IDRangeMultiplierCurve.GetRichCurveConst();
        return (C && C->GetNumKeys() > 0) ? C->Eval(Score) : 1.f;
    }

    /** Compute periscope effective range from zoom level. */
    float ComputePeriscopeRange(float Zoom) const
    {
        return PeriscopeBaseRange * Zoom
            * (1.f + (1.f - Zoom) * BoostZoomDetection);
    }

    /** Compute effective FOV half-angle from zoom level. */
    float ComputeFOVHalfAngle(float Zoom) const
    {
        return FOVHalfAngleDegrees
            / FMath::Max(1.f + (Zoom - 1.f) * ZoomFOVReductionFactor, 0.01f);
    }
};