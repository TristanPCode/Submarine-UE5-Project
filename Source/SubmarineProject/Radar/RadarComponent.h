#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TrackableSubmarine.h"
#include "Radar/RadarSettings.h"
#include "RadarComponent.generated.h"

/**
 * URadarComponent
 *
 * Attach to ASubmarinePawn. One instance per submarine.
 *
 * Responsibilities:
 *
 *  1. VULNERABILITY TRACKING (self)
 *     Maintains a float VulnerabilityScore [0..1] based on:
 *       - Movement contribution (active while moving, no decay timer)
 *       - Radar contribution    (spiked by NotifyRadarUsed, decays over time)
 *       - Torpedo contribution  (spiked by NotifyTorpedoFired, decays over time)
 *     Score = clamp(Move*Wm + Radar*Wr + Torpedo*Wt, 0, 1)
 *     Exposed via GetVulnerabilityScore() on ITrackableSubmarine.
 *
 *  2. DETECTION (active scan of world)
 *     Every tick, iterates all actors implementing ITrackableSubmarine:
 *       - Reads target VulnerabilityScore -> range multipliers
 *       - Checks circle range (always active)
 *       - Checks FOV cone + FOV range (depends on camera mode)
 *       - Checks periscope range (when periscope active, zoom-modified)
 *       - Assigns ERadarDetectionState
 *       - Manages DetectionTimeRemaining per entry
 *
 *  3. SCAN TRIGGER (input-driven)
 *     TriggerScan(): refreshes DisplayTimeRemaining on all currently detected
 *     entries. Called from SubmarinePawn::OnRadarScan().
 *
 *  4. DATA EXPOSURE
 *     GetDetectionEntries() returns the live TArray<FDetectedEntry> for the UI.
 *
 * Design rules:
 *   - UI reads only, never writes to this component.
 *   - No UMG or Slate code here.
 *   - All parameters come from URadarSettings DataAsset.
 */
UCLASS(ClassGroup = (Submarine), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API URadarComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    URadarComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // -----------------------------------------------------------------------
    //  Configuration
    // -----------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar")
    TObjectPtr<URadarSettings> Settings;

    // -----------------------------------------------------------------------
    //  Vulnerability notifications (called from SubmarinePawn)
    // -----------------------------------------------------------------------

    /** Call when this submarine fires a torpedo. Spikes TorpedoContribution. */
    UFUNCTION(BlueprintCallable, Category = "Radar")
    void NotifyTorpedoFired();

    // -----------------------------------------------------------------------
    //  Scan trigger (called from SubmarinePawn input handler)
    // -----------------------------------------------------------------------

    /**
     * Trigger a radar scan:
     *   - Spikes RadarContribution (vulnerability)
     *   - Refreshes DisplayTimeRemaining on all currently detected entries
     */
    UFUNCTION(BlueprintCallable, Category = "Radar")
    void TriggerScan();

    // -----------------------------------------------------------------------
    //  Scan Cooldown
    // -----------------------------------------------------------------------

    /** Remaining cooldown before next scan is allowed (0 = ready). */
    UFUNCTION(BlueprintPure, Category = "Radar")
    float GetScanCooldownRemaining() const { return ScanCooldownRemaining; }

    /**
     * Scan cooldown ratio for UI:
     *   0.0 = just scanned (cooldown just started)
     *   1.0 = ready to scan again
     * Safe to use directly as a progress bar value.
     */
    UFUNCTION(BlueprintPure, Category = "Radar")
    float GetScanCooldownRatio() const
    {
        const URadarSettings* S = GetSettings();
        const float CD = S ? S->ScanCooldown : 0.f;
        if (CD <= 0.f) return 1.f;
        return FMath::Clamp(1.f - ScanCooldownRemaining / CD, 0.f, 1.f);
    }

    /**
     * True for exactly two tick after TriggerScan() succeeds.
     * Read by RadarModule to start the pulse animation.
     * Decreases tick by tick in Tick component.
     */
    UFUNCTION(BlueprintPure, Category = "Radar")
    bool GetScanJustTriggered() const { return ScanJustTriggeredFrames>=1; }

    /**
     * Resets ScanJustTriggeredFrames in case we catch the GetScanJustTriggered().
     */
    void ResetScanJustTriggeredFrames();

    // -----------------------------------------------------------------------
    //  Data access (read-only, for ITrackableSubmarine)
    // -----------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Radar")
    float GetVulnerabilityScore() const { return VulnerabilityScore; }

    UFUNCTION(BlueprintPure, Category = "Radar")
    const TArray<FDetectedEntry>& GetDetectionEntries() const { return DetectedEntries; }

    // -----------------------------------------------------------------------
    //  Periscope zoom (stored here, persists across mode switches)
    // -----------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Radar")
    void SetZoom(float NewZoom);

    UFUNCTION(BlueprintCallable, Category = "Radar")
    void IncrementZoom(float Delta);

    UFUNCTION(BlueprintPure, Category = "Radar")
    float GetZoom() const { return CurrentZoom; }

private:

    // -----------------------------------------------------------------------
    //  Vulnerability state
    // -----------------------------------------------------------------------

    float MovementContribution = 0.f;  // Active while speed != 0 (no decay timer)
    float RadarContribution = 0.f;  // Decays over time after TriggerScan
    float TorpedoContribution = 0.f;  // Decays over time after NotifyTorpedoFired
    float VulnerabilityScore = 0.f;


    // -----------------------------------------------------------------------
    //  Scan data
    // -----------------------------------------------------------------------

    float ScanCooldownRemaining = 0.f;
    int32 ScanJustTriggeredFrames = 0;  // counts down from 2; >0 means scan just fired

    // -----------------------------------------------------------------------
    //  Detection data
    // -----------------------------------------------------------------------

    TArray<FDetectedEntry> DetectedEntries;

    // -----------------------------------------------------------------------
    //  Zoom state
    // -----------------------------------------------------------------------

    float CurrentZoom = 1.f;
    float DetectLogTimer = 0.f;  // per-instance diagnostic timer

    // -----------------------------------------------------------------------
    //  Internal helpers
    // -----------------------------------------------------------------------

    const URadarSettings* GetSettings() const;

    /** Recompute VulnerabilityScore from current contributions. */
    void UpdateVulnerabilityScore(float DeltaTime);

    /**
     * Scan all ITrackableSubmarine actors in the world and update DetectedEntries.
     * @param OwnerPawn  The ASubmarinePawn this component belongs to.
     */
    void UpdateDetection(float DeltaTime);

    /**
     * Evaluate detection state for a single target.
     * Returns ERadarDetectionState or INDEX_NONE-style "not detected" via
     * bOutDetected = false.
     */
    bool EvaluateTarget(
        const FVector& OwnerLocation,
        const FVector& OwnerForward,
        float           OwnerYaw,
        bool            bPeriscopeActive,
        float           Zoom,
        float           CameraFOV,
        AActor* TargetActor,
        float           TargetVulnerability,
        ERadarDetectionState& OutState) const;

    /**
     * Find an existing entry by GUID, or return nullptr.
     */
    FDetectedEntry* FindEntry(const FGuid& Guid);

    /**
     * Compute torpedo icon rotation in radar space.
     */
    float ComputeTorpedoIconRotation(
        const FVector& TorpedoVelocity,
        float          OwnerYaw) const;
};