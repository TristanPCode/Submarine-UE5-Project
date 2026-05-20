#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Replay/ReplaySettings.h"
#include "ScreenFadeComponent.generated.h"


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFadeAlphaChanged, float, Alpha);

/**
 * UScreenFadeComponent
 *
 * Attach to ASubmarineGameMode.
 *
 * Drives APlayerCameraManager::StartCameraFade() - UE5's built-in
 * per-player screen fade.  No extra assets required.
 *
 * Full sequence supported per FScreenFadeSettings:
 *
 *   [BlackScreenIn: instant black + hold] -> FadeIn (black->clear) -> [content] ->
 *   FadeOut (clear->black) -> [BlackScreenOut: hold at black]
 *
 * Each step is independently optional via bools in FScreenFadeSettings.
 */
UCLASS(ClassGroup = (Submarine), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API UScreenFadeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UScreenFadeComponent();

    // -----------------------------------------------------------------------
    //  API
    // -----------------------------------------------------------------------

    /**
     * Starts a fade-in sequence.
     *   - If bBlackScreenIn: snaps to solid black instantly, holds BlackScreenInDuration
     *     seconds, THEN fades clear over FadeInDuration.
     *   - If only bFadeIn: fades from black to clear immediately.
     *   - If neither: does nothing.
     */
    UFUNCTION(BlueprintCallable, Category = "ScreenFade")
    void FadeIn(APlayerController* PC, const FScreenFadeSettings& Settings);

    /**
     * Starts a fade-in sequence.
     *   - If bBlackScreenIn: snaps to solid black instantly, holds BlackScreenInDuration
     *     seconds, THEN fades clear over FadeInDuration.
     *   - If only bFadeIn: fades from black to clear immediately.
     *   - If neither: does nothing.
     */
    UFUNCTION(BlueprintCallable, Category = "ScreenFade")
    void FadeOut(APlayerController* PC, const FScreenFadeSettings& Settings);

    /**
     * Instantly clears any ongoing fade for this player (cuts to clear).
     */
    UFUNCTION(BlueprintCallable, Category = "ScreenFade")
    void ClearFade(APlayerController* PC);

    /**
     * Schedule a Clear Fade
     */
    UFUNCTION(BlueprintCallable, Category = "ScreenFade")
    void ScheduleClearFade(APlayerController* PC, float Delay);

    /**
     * Convenience: fade out (+ optional black-screen-out hold), then fire OnComplete.
     * Total time before OnComplete fires = FadeOutDuration + BlackScreenOutDuration.
     * If neither fade nor black-screen-out is configured, fires OnComplete immediately.
     */
    void FadeOutThenCall(APlayerController* PC,
        const FScreenFadeSettings& Settings,
        FSimpleDelegate OnComplete);

    /**
     * Returns the total wall-clock delay introduced by a FadeOut call
     * (fade duration + black-screen-out hold).  Useful for scheduling timers
     * that should fire after the complete fade-out sequence.
     */
    float GetFadeOutTotalDuration(const FScreenFadeSettings& Settings) const;

    /** Enable tick for fade alpha monitoring. */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // -----------------------------------------------------------------------
    //  Named fade library API
    // -----------------------------------------------------------------------

    /**
     * Merge entries from Library into the runtime FadeLibrary.
     * Existing entries with the same key are overwritten.
     * Entries not present in Library are kept unchanged.
     * Call this from GameMode::BeginPlay after loading ReplaySettings.
     */
    void InitFadeLibrary(const TMap<FName, FScreenFadeSettings>& Library);

    /**
     * Add or replace a single entry in the runtime FadeLibrary.
     */
    UFUNCTION(BlueprintCallable, Category = "ScreenFade|Library")
    void SetFadeEntry(FName Key, const FScreenFadeSettings& Settings);

    /**
     * Change which fade follows FadeKey when its fade-out completes.
     * Equivalent to modifying FadeLibrary[FadeKey].NextFadeName at runtime.
     * Does nothing if FadeKey is not found in the library.
     *
     * Example: ScreenFade->SetNextFade("DeathReplay", "Victory");
     */
    UFUNCTION(BlueprintCallable, Category = "ScreenFade|Library")
    void SetNextFade(FName FadeKey, FName NextFadeKey);

    /**
     * Trigger the fade-in portion of a named fade entry.
     * Looks up FadeKey in FadeLibrary. Does nothing if not found.
     */
    UFUNCTION(BlueprintCallable, Category = "ScreenFade|Library")
    void PlayFadeIn(APlayerController* PC, FName FadeKey);

    /**
     * Trigger the fade-out portion of a named fade entry.
     * Looks up FadeKey in FadeLibrary. Does nothing if not found.
     * If the entry has NextFadeName set, chains automatically on completion.
     */
    UFUNCTION(BlueprintCallable, Category = "ScreenFade|Library")
    void PlayFadeOut(APlayerController* PC, FName FadeKey);

    /**
     * Returns a copy of a library entry, or a default FScreenFadeSettings
     * if the key is not found. Use HasFadeEntry() to check existence first.
     */
    UFUNCTION(BlueprintPure, Category = "ScreenFade|Library")
    FScreenFadeSettings GetFadeEntry(FName Key) const;

    UFUNCTION(BlueprintPure, Category = "ScreenFade|Library")
    bool HasFadeEntry(FName Key) const;

    // Broadcast every tick while fading, and at fade start/end
    // Alpha: 0=visible, 1=black (matches ClientSetCameraFade convention)
    UPROPERTY(BlueprintAssignable, Category = "ScreenFade")
    FOnFadeAlphaChanged OnFadeAlphaChanged;   // DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam

    // -----------------------------------------------------------------------
    //  Runtime library (readable from Blueprint)
    // -----------------------------------------------------------------------

    /**
     * The runtime fade library. Initialised from UReplaySettings::FadeLibrary
     * at BeginPlay and overridable at runtime via the API above.
     * Readable from Blueprint for debugging; modify via SetFadeEntry/SetNextFade.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ScreenFade|Library")
    TMap<FName, FScreenFadeSettings> FadeLibrary;

private:

    // -----------------------------------------------------------------------
    //  Helpers
    // -----------------------------------------------------------------------

    /** Snap screen to fully opaque black (alpha = 1) with zero duration. */
    void Internal_SnapToBlack(APlayerController* PC, const FLinearColor& Color);

    /** Start the camera-fade animation.  bFromBlack=true -> fade-in; false -> fade-out. */
    void Internal_StartFade(APlayerController* PC,
        float Duration, bool bFromBlack,
        const FLinearColor& Color);

    /**
     * Called at the end of a FadeOut (after fade + black-screen-out hold).
     * If bHasNextFade: fires FadeIn(NextFade), optionally preceded by ClearFade
     * if NextFade has no fade-in and no black-screen-in.
     * Otherwise: calls ClearFade.
     */
    void Internal_OnFadeOutComplete(APlayerController* PC, const FScreenFadeSettings& Settings);

    /** Returns true when ScreenFade logging is enabled in ReplaySettings. */
    bool ShouldLog() const;

    // -----------------------------------------------------------------------
    //  State
    // -----------------------------------------------------------------------

    /** Pending fade-out timers: PC -> timer handle */
    TMap<TWeakObjectPtr<APlayerController>, FTimerHandle> PendingFadeTimers;

    /** Per-frame fade-alpha monitor. */
    float FadeMonitorTimer = 0.f;
    bool  bFadeMonitorActive = false;
    TWeakObjectPtr<APlayerController> MonitoredPC;

    /** Timer handles for the BlackScreen hold phases. */
    FTimerHandle BlackScreenInTimerHandle;
    FTimerHandle BlackScreenOutTimerHandle;
};

// ---------------------------------------------------------------------------
//  Safe Transition Transform FScreenSettings
// ---------------------------------------------------------------------------

FScreenFadeSettings SafeTransitionTransform(FScreenFadeSettings pFade, float MaxDuration, bool TransitionIn);