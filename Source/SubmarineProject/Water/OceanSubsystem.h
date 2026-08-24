#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OceanTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "OceanSubsystem.generated.h"

class AWaterRegionActor;
class UWaterRegionDataAsset;
class UOceanDebugSettings;
class UMaterialParameterCollection;

// ---------------------------------------------------------------------------
//  UOceanSubsystem
//  Single source of truth for all runtime water state.
//
//  Ownership summary:
//    Owned by UGameInstance. Persistent across level transitions.
//    Holds the registered region list (populated by AWaterRegionActor at
//    BeginPlay, cleared on EndPlay).
//    Holds the DefaultRegionData fallback (assigned via Details on the
//    GameInstance Blueprint).
//    Owns the per-frame cache (FOceanFrameCache).
//    Writes MPC_Ocean once per frame. Never reads from it.
//
//  Query contract:
//    GetWaterHeightAtPosition() and SampleWaterAt() are synchronous and
//    cache-friendly. Physics components call these every tick with zero
//    re-evaluation cost after the first query per frame.
//
//  Data flow (one-way, enforced by convention):
//    UWaterRegionDataAsset  -->  AWaterRegionActor  -->  UOceanSubsystem
//    UOceanSubsystem  -->  MPC_Ocean  -->  Materials / PostProcess / Niagara
//    UOceanSubsystem  -->  Physics components (via query API)
//    Nothing reads back from MPC_Ocean into gameplay. Ever.
// ---------------------------------------------------------------------------
// Fired when a region is registered or unregistered at runtime.
// UOceanHeightFieldGenerator binds to this to rebuild the height field.
DECLARE_MULTICAST_DELEGATE(FOceanRegionChangedDelegate);

UCLASS()
class SUBMARINEPROJECT_API UOceanSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:

    // -----------------------------------------------------------------------
    //  UGameInstanceSubsystem interface
    // -----------------------------------------------------------------------

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // -----------------------------------------------------------------------
    //  Configuration (assign in the GameInstance Blueprint Details panel)
    // -----------------------------------------------------------------------

    // Fallback region used when no AWaterRegionActor covers a queried position.
    // Must be assigned. If null, a hardcoded safe default is used with a warning.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Config")
    TObjectPtr<UWaterRegionDataAsset> DefaultRegionData;

    // Reference to the MPC_Ocean asset. Assign in the GameInstance BP.
    // The subsystem writes to this every tick. Materials read from it.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Config")
    TObjectPtr<UMaterialParameterCollection> OceanMPC;

    // Debug settings DataAsset. Optional - debug draws are suppressed if null.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|Config")
    TObjectPtr<UOceanDebugSettings> DebugSettings;

    // -----------------------------------------------------------------------
    //  Region registration (called by AWaterRegionActor, not by game code)
    // -----------------------------------------------------------------------

    void RegisterRegion(AWaterRegionActor* Region);
    void UnregisterRegion(AWaterRegionActor* Region);

    // -----------------------------------------------------------------------
    //  Region change notification
    //  Fired after RegisterRegion / UnregisterRegion.
    //  UOceanHeightFieldGenerator binds to this to rebuild the height
    //  field texture when the map layout changes at runtime.
    // -----------------------------------------------------------------------
    FOceanRegionChangedDelegate OnRegionChanged;

    // Returns the default region BaseWaterHeight (or 0 if no default set).
    // Used by OceanHeightFieldGenerator as the surface-level Z probe.
    float GetDefaultWaterHeight() const;

    // -----------------------------------------------------------------------
    //  Canonical surface-height probe
    //  THE single definition of "water surface height at this XY". Evaluates
    //  regions at the canonical surface Z (GetDefaultWaterHeight) so that the
    //  height-field bake, QuerySurface, physics, and debug all agree.
    //  Regions are 3D volumes; the surface is 2D -- this fixes the Z that all
    //  surface queries use, eliminating probe-depth divergence.
    // -----------------------------------------------------------------------
    float GetSurfaceHeightAtXY(const FVector2D& XY) const;

    // -----------------------------------------------------------------------
    //  Actor sample registration
    //  Any actor (player sub, CPU sub, torpedo spawner) can register to have
    //  its position pre-warmed in the frame cache each tick.
    //  Registration is cheap; unregistration is automatic on actor destroy
    //  (weak pointer silently skips dead entries).
    // -----------------------------------------------------------------------

    // Register an actor for per-frame cache pre-warming.
    // bIsLocalPlayer and PlayerIndex are used for MPC per-player parameter writes.
    void RegisterSampleActor(AActor* Actor, FName DebugLabel = NAME_None,
        bool bIsLocalPlayer = false, int32 PlayerIndex = -1);

    // Explicitly unregister an actor (also called automatically on EndPlay).
    void UnregisterSampleActor(AActor* Actor);

    // -----------------------------------------------------------------------
    //  Public query API (used by physics components and any gameplay system)
    // -----------------------------------------------------------------------

    // Returns the authoritative water surface height (world Z) at WorldPos.
    // Uses the frame cache when available. Safe to call every tick.
    UFUNCTION(BlueprintCallable, Category = "Ocean")
    float GetWaterHeightAtPosition(const FVector& WorldPos) const;

    // Returns the agitation intensity [0..1] at WorldPos.
    // Data-only in 6.1. Used to drive physics perturbations in 6.3.
    UFUNCTION(BlueprintCallable, Category = "Ocean")
    float GetAgitationAtPosition(const FVector& WorldPos) const;

    // Returns the full FWaterSample for WorldPos.
    // Preferred over individual getters when multiple values are needed,
    // since it evaluates all regions exactly once.
    UFUNCTION(BlueprintCallable, Category = "Ocean")
    FWaterSample SampleWaterAt(const FVector& WorldPos) const;

    // -----------------------------------------------------------------------
    //  Canonical surface query (the single authoritative surface API)
    //  Used by physics now; reused by Phase 6.6 buoyancy/righting later.
    //  Returns regional base + primary (octave 0) Gerstner + analytic normal.
    //  This is FULL amplitude (no NearSurfaceAlpha) -- the absolute surface.
    // -----------------------------------------------------------------------
    FOceanSurfaceSample QuerySurface(const FVector2D& XY, float Time) const;

    // Evaluates a single Gerstner octave's Z displacement at XY/Time.
    // Shared canonical implementation. OctaveIndex selects OceanGerstner tables.
    // BaseAmplitude/BaseSpeed/BaseDirection/WaveLengthScale come from the
    // region sample at XY. This is the EXACT formula the material must mirror.
    float EvaluateGerstnerOctave(int32 OctaveIndex, const FVector2D& XY,
        float Time, float BaseAmplitude, float BaseSpeed,
        const FVector2D& BaseDirection, float WaveLengthScale) const;

    // -----------------------------------------------------------------------
    //  Debug reconstruction API (validation tooling)
    //  Lets debug code reconstruct the VISUAL height on the CPU, term by term,
    //  to compare against physics height and isolate where they diverge.
    // -----------------------------------------------------------------------

    // Sums octaves [0..MaxOctave] of Gerstner at XY/Time using the region
    // sample at XY. MaxOctave=0 gives primary only; MaxOctave=3 gives all.
    // Used to measure how much visual-only detail (Oct1-3) contributes.
    float EvaluateGerstnerSum(const FVector2D& XY, float Time, int32 MaxOctave) const;

    // Returns the transition-slot correction that the material applies at XY,
    // given the raw height-field value. Mirrors the material slot loop exactly.
    // This is the canonical slot formula -- the material must match THIS.
    // Returns the corrected height (not the delta).
    float EvaluateSlotCorrectionCPU(const FVector2D& XY, float HeightFieldValue) const;

    // Returns true if WorldPos is currently below the water surface.
    UFUNCTION(BlueprintCallable, Category = "Ocean")
    bool IsPositionUnderwater(const FVector& WorldPos) const;

    /**
     * Returns the blended regional drag multiplier at WorldPos.
     * 1.0 = no drag change. >1.0 = heavier water. <1.0 = lighter.
     * bTorpedoQuery = true: only considers regions with bAffectTorpedoes=true.
     * bTorpedoQuery = false (default): considers all regions.
     * Falls back to 1.0 (no change) if no region covers WorldPos.
     */
    UFUNCTION(BlueprintCallable, Category = "Ocean")
    float GetRegionalDragMultiplierAt(const FVector& WorldPos,
        bool bTorpedoQuery = false) const;

    // -----------------------------------------------------------------------
    //  Per-frame update (called from the subsystem's world tick binding)
    // -----------------------------------------------------------------------

    // Invalidates the frame cache and re-evaluates for known player positions.
    // Receives the ticking world so editor preview worlds are correctly filtered.
    void UpdateOceanFrame(UWorld* World, ELevelTick TickType, float DeltaTime);

private:

    // -----------------------------------------------------------------------
    //  Region evaluation (internal)
    // -----------------------------------------------------------------------

    // Evaluates all registered regions at WorldPos and returns a blended
    // FWaterSample. Falls back to DefaultRegionData if no region matches.
    FWaterSample EvaluatePositionInternal(const FVector& WorldPos) const;

    // Blends two water samples together by weight.
    static FWaterSample BlendSamples(const FWaterSample& A, const FWaterSample& B, float BlendAlpha);

    // Returns the FWaterSample from DefaultRegionData (or safe hardcoded values).
    FWaterSample GetDefaultSample() const;

    // -----------------------------------------------------------------------
    //  MPC writes (internal)
    // -----------------------------------------------------------------------

    void WriteMPCParameters() const;

    // Sets PlayerIndex scalar on post-process MIDs so the underwater
    // material selects the correct P1/P2 parameter set per viewport.
    // Called once from WriteMPCParameters after players are registered.
    void AssignPlayerPostProcessIndices() const;

    // True once AssignPlayerPostProcessIndices has succeeded.
    // mutable: WriteMPCParameters is const but needs to set this flag.
    mutable bool bPlayerIndicesAssigned = false;

    // -----------------------------------------------------------------------
    //  Debug (internal)
    // -----------------------------------------------------------------------

    void DrawDebugVisualization() const;

    // Accumulated time used for log throttling, mirrors PhysicsLogFrequency pattern.
    float DebugLogTimer = 0.f;

    // -----------------------------------------------------------------------
    //  State
    // -----------------------------------------------------------------------

    // All region actors currently registered in the world.
    // Sorted by Priority (descending) after each registration change.
    UPROPERTY()
    TArray<TObjectPtr<AWaterRegionActor>> RegisteredRegions;

    // All actors registered for per-frame sample pre-warming.
    TArray<FOceanSampleRequest> RegisteredSampleActors;

    // Per-frame cache. Invalidated at the start of UpdateOceanFrame().
    FOceanFrameCache FrameCache;

    // Delegate handle for the world tick binding.
    FDelegateHandle WorldTickHandle;

    // Cached world pointer to avoid repeated GetWorld() calls.
    TWeakObjectPtr<UWorld> CachedWorld;

    // -----------------------------------------------------------------------
    //  Dynamic transition slots
    //  Each slot represents one active region boundary being corrected
    //  at full float precision in the material.
    // -----------------------------------------------------------------------

    static constexpr int32 MaxTransitionSlots = 8;

    struct FTransitionSlotState
    {
        // The region this slot tracks. Null = slot is free.
        TWeakObjectPtr<AWaterRegionActor> Region;

        // Last computed boundary data.
        FVector2D BoundaryPoint = FVector2D::ZeroVector;
        FVector2D OutwardNormal = FVector2D(1.f, 0.f);
        float     HeightInner = 0.f;
        float     HeightOuter = 0.f;
        float     BlendRadius = 500.f;

        // Fade weight [0,1]. Fades in when assigned, fades out when released.
        float     Weight = 0.f;
        bool      bFadingOut = false;

        // Distance from nearest camera to this boundary (for priority).
        float     CameraDistance = FLT_MAX;

        bool IsActive() const { return Region.IsValid() && !bFadingOut; }
        bool IsFree()   const { return Weight < 0.001f && bFadingOut; }
    };

    FTransitionSlotState TransitionSlots[MaxTransitionSlots];

    // Fade speed: slots reach full weight in 0.5s, fade out in 0.5s.
    static constexpr float SlotFadeSpeed = 2.f; // 1/0.5s

    // Updates transition slot assignment and fade weights.
    // Called from UpdateOceanFrame each tick.
    void UpdateTransitionSlots(float DeltaTime);

    // Writes all 8 slot MPC parameters.
    void WriteTransitionSlotsMPC() const;

    // -----------------------------------------------------------------------
    //  Validation / debug reconstruction
    // -----------------------------------------------------------------------

    // Cached weak pointer to the height field generator (found lazily).
    mutable TWeakObjectPtr<class UOceanHeightFieldGenerator> CachedHeightFieldGen;

    // Finds (and caches) the height field generator in the world.
    class UOceanHeightFieldGenerator* GetHeightFieldGenerator() const;

    // Per-frame debug reconstruction: compares physics height vs visual
    // height (CPU-reconstructed) at each local player position and prints
    // the residual breakdown. Toggled by the console var Ocean.DebugSurface.
    void DebugReconstructSurface() const;
};