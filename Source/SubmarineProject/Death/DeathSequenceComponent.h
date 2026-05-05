#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeathSequenceComponent.generated.h"

class ASubmarinePawn;
class ATorpedoPawn;
class UReplayData;
class UReplaySettings;
class UReplayPlaybackComponent;
class UScreenFadeComponent;

// -----------------------------------------------------------------------
//  What killed this submarine - determines death-cam angle
// -----------------------------------------------------------------------
UENUM(BlueprintType)
enum class EDeathCamMode : uint8
{
    /** Fixed 3rd-person view behind the killer (no orbit) */
    KillerThirdPerson,
    /** POV camera of the killer (torpedo nose-cam or attacker sub POV) */
    KillerPOV,
    /** Static 3rd person behind the dead submarine */
    StaticBehindDead,
    /** No death cam */
    None
};

// -----------------------------------------------------------------------
//  Phase of the death sequence state machine
// -----------------------------------------------------------------------
UENUM(BlueprintType)
enum class EDeathSequencePhase : uint8
{
    Inactive,       // No sequence running
    PreDelay,       // Waiting before starting death cam
    DeathCam,       // Playing the death replay / death cam
    TransitionOut   // Brief fade before spectator (optional, BP-driven)
};

// Fired when the death cam phase starts (useful for Blueprint UI)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDeathCamStarted,
    EDeathCamMode, Mode, float, Duration);

// Fired when the sequence is fully complete and we should switch to spectator
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeathSequenceComplete);

/**
 * UDeathSequenceComponent
 *
 * Death flow (revised):
 *   1. BeginDeathSequence is called AFTER DeathPreviewDelay has elapsed
 *      (GameMode waits for the timer before calling this). The replay slice
 *      already contains the explosion VFX frames.
 *   2. Replay playback starts immediately (no pre-delay phase here —
 *      the pre-delay was the GameMode timer).
 *   3. Ghost actors drive the camera for DeathCamWallClockDuration seconds
 *      (= SliceSeconds / PlaybackSpeed).
 *   4. OnDeathSequenceComplete -> GameMode fades out and spawns spectator.
 */
UCLASS(ClassGroup = (Submarine), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API UDeathSequenceComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDeathSequenceComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // -----------------------------------------------------------------------
    //  API — call from GameMode when a submarine dies
    // -----------------------------------------------------------------------

    /**
     * Begin the death replay sequence.
     * @param DeadSubmarine       Submarine that died (already hidden by FreezeOnDeath).
     * @param DeadController      Its controller (already unpossessed).
     * @param Killer              Killing actor (nullptr = environmental).
     * @param ReplaySlice         Recorded data slice.
     * @param InPlaybackComponent Playback component (nullptr = live cam fallback).
     * @param InPlaybackSpeed     Speed multiplier from ReplaySettings.
     * @param InScreenFade        Optional ScreenFadeComponent for fade effects.
     */
    UFUNCTION(BlueprintCallable, Category = "DeathSequence")
    void BeginDeathSequence(ASubmarinePawn* DeadSubmarine,
        AController* DeadController,
        AActor* Killer,
        UReplayData* ReplaySlice,
        UReplayPlaybackComponent* InPlaybackComponent = nullptr,
        float                     InPlaybackSpeed = 1.f,
        UScreenFadeComponent* InScreenFade = nullptr);

    /** Skip the death cam and go straight to spectator. */
    UFUNCTION(BlueprintCallable, Category = "DeathSequence")
    void SkipDeathCam();

    /** Current phase (read from Blueprint for UI). */
    UFUNCTION(BlueprintPure, Category = "DeathSequence")
    EDeathSequencePhase GetPhase() const { return Phase; }

    /** 0..1 progress through the death cam phase. */
    UFUNCTION(BlueprintPure, Category = "DeathSequence")
    float GetDeathCamProgress() const;

    // -----------------------------------------------------------------------
    //  Settings reference (set from GameMode)
    // -----------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DeathSequence")
    TObjectPtr<UReplaySettings> ReplaySettings;

    // -----------------------------------------------------------------------
    //  Delegates
    // -----------------------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "DeathSequence|Events")
    FOnDeathCamStarted OnDeathCamStarted;

    UPROPERTY(BlueprintAssignable, Category = "DeathSequence|Events")
    FOnDeathSequenceComplete OnDeathSequenceComplete;

private:
    // -- State machine -------------------------------------------------------
    EDeathSequencePhase Phase = EDeathSequencePhase::Inactive;
    float PhaseTimer = 0.f;
    float PhaseDuration = 0.f;

    // -----------------------------------------------------------------------
    //  Session references
    // -----------------------------------------------------------------------
    TWeakObjectPtr<ASubmarinePawn> CachedDeadSub;
    TWeakObjectPtr<AController>    CachedDeadController;

    /** The actor that killed us (torpedo or submarine). May go invalid. */
    TWeakObjectPtr<AActor>         CachedKiller;

    /**
     * The submarine that fired the killing torpedo (or the killer sub itself).
     * Used to estimate muzzle position when the torpedo doesn't exist yet.
     * Kept alive longer than CachedKiller.
     */
    TWeakObjectPtr<ASubmarinePawn> KillerSubmarine;

    TObjectPtr<UReplayData>        CachedReplaySlice;
    EDeathCamMode                  ActiveDeathCamMode = EDeathCamMode::None;

    UReplayPlaybackComponent* PlaybackComponent = nullptr;
    UScreenFadeComponent* ScreenFade = nullptr;
    bool                      bUsingReplayPlayback = false;

    // -----------------------------------------------------------------------
    //  Frozen camera (when killer dies before the cam duration ends)
    // -----------------------------------------------------------------------
    bool     bCameraFrozen = false;
    FVector  FrozenCamLocation = FVector::ZeroVector;
    FRotator FrozenCamRotation = FRotator::ZeroRotator;
    bool bHasValidFrozenFrame = false;

    // -----------------------------------------------------------------------
    //  Internal helpers
    // -----------------------------------------------------------------------
    EDeathCamMode DetermineDeathCamMode(AActor* Killer) const;

    // Fade timers — scheduled in StartDeathCam, cosmetic only
    FTimerHandle FadeInTimerHandle;
    FTimerHandle FadeOutTimerHandle;

    void StartDeathCam(float InPlaybackSpeed);
    void FinishSequence();
    void TickDeathCam(float DeltaTime);

    void ScheduleFades(float WallClockDuration);
    void CancelFadeTimers();

    /**
     * Computes the camera world Location+Rotation for the current frame.
     * Returns false if there is nothing to look at.
     */
    bool ComputeReplayDeathCamTransform(FVector& OutLoc, FRotator& OutRot) const;
    bool ComputeLiveDeathCamTransform(FVector& OutLoc, FRotator& OutRot) const;

    /**
     * Estimates the killer's transform from the firing submarine's muzzle.
     * Called when the torpedo hasn't spawned or no longer exists.
     */
    bool EstimateKillerTransformFromSub(FVector& OutLoc, FRotator& OutRot) const;

    /**
     * Computes a fixed 3rd-person position directly behind TargetActor
     * at DeathCamThirdPersonRadius distance.
     */
    static void GetThirdPersonBehind(const FVector& TargetLoc, const FRotator& TargetRot,
        FVector& OutCamLoc, FRotator& OutCamRot, float Radius);

    /** Directly positions the dead controller's view without possessing anything. */
    void ApplyViewToController(const FVector& Location, const FRotator& Rotation) const;

    const UReplaySettings* GetSettings() const;
};