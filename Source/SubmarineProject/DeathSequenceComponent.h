#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeathSequenceComponent.generated.h"

class ASubmarinePawn;
class ATorpedoPawn;
class ASubmarineSpectatorPawn;
class UReplayData;
class UReplaySettings;

// -----------------------------------------------------------------------
//  What killed this submarine — determines death-cam angle
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
    EDeathCamMode, Mode,
    float, Duration);

// Fired when the sequence is fully complete and we should switch to spectator
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeathSequenceComplete);

/**
 * UDeathSequenceComponent
 *
 * Death flow:
 *   1. Submarine mesh hidden + collisions disabled immediately on death.
 *   2. Optional pre-delay.
 *   3. Death cam tracks the killer from 3rd person (fixed, no orbit) or POV.
 *      - If killer torpedo hasn't spawned yet: estimate position from the
 *        firing submarine's muzzle transform.
 *      - If killer is destroyed mid-cam: freeze camera at last known
 *        position/rotation for the remainder of the duration.
 *   4. OnDeathSequenceComplete fires -> GameMode spawns spectator pawn and
 *      destroys the dead submarine actor.
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
     * Begin the death sequence for DeadSubmarine.
     *
     * @param DeadController   The controller that was possessing the sub.
     * @param Killer           The actor that dealt the killing blow (torpedo or sub).
     *                         Pass nullptr if unknown (e.g. depth pressure).
     * @param ReplaySlice      Optional: a slice of the replay buffer covering
     *                         the last N seconds leading up to the death.
     *                         If null, falls back to a live death-cam look-at.
     */
    UFUNCTION(BlueprintCallable, Category = "DeathSequence")
    void BeginDeathSequence(ASubmarinePawn* DeadSubmarine,
        AController* DeadController,
        AActor* Killer,
        UReplayData* ReplaySlice);

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

    // -----------------------------------------------------------------------
    //  Frozen camera (when killer dies before the cam duration ends)
    // -----------------------------------------------------------------------
    bool     bCameraFrozen = false;
    FVector  FrozenCamLocation = FVector::ZeroVector;
    FRotator FrozenCamRotation = FRotator::ZeroRotator;
    bool bHasValidFrozenFrame = false;

    // -----------------------------------------------------------------------
    //  Death cam fixed 3rd-person distance
    // -----------------------------------------------------------------------
    static constexpr float DeathCamThirdPersonRadius = 200.f;

    // -----------------------------------------------------------------------
    //  Internal helpers
    // -----------------------------------------------------------------------
    EDeathCamMode DetermineDeathCamMode(AActor* Killer) const;

    void StartPreDelay();
    void StartDeathCam();
    void FinishSequence();

    void TickDeathCam(float DeltaTime);

    /**
     * Computes the camera world Location+Rotation for the current frame.
     * Returns false if there is nothing to look at.
     */
    bool ComputeDeathCamTransform(FVector& OutLocation, FRotator& OutRotation) const;

    /**
     * Estimates the killer's transform from the firing submarine's muzzle.
     * Called when the torpedo hasn't spawned or no longer exists.
     */
    bool EstimateKillerTransformFromSub(FVector& OutLocation, FRotator& OutRotation) const;

    /**
     * Computes a fixed 3rd-person position directly behind TargetActor
     * at DeathCamThirdPersonRadius distance.
     */
    static void GetThirdPersonBehind(const FVector& TargetLocation,
        const FRotator& TargetRotation,
        FVector& OutCamLocation,
        FRotator& OutCamRotation);

    /** Directly positions the dead controller's view without possessing anything. */
    void ApplyViewToController(const FVector& Location, const FRotator& Rotation) const;

    const UReplaySettings* GetSettings() const;
};