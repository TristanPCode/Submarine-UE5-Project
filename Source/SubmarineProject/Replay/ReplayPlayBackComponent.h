#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ReplayData.h"
#include "ReplayPlaybackComponent.generated.h"

class UReplaySettings;
class ASubmarinePawn;
class ATorpedoPawn;
class USubmarineTorpedoComponent;
class UNiagaraSystem;

// -----------------------------------------------------------------------
//  One ghost actor entry — one per unique actor name in the replay slice
// -----------------------------------------------------------------------
USTRUCT()
struct FGhostActorEntry
{
    GENERATED_BODY()

    /** Stable identity from GetActorInstanceGuid() -- never recycled by UE. */
    FGuid ActorGuid;

    /** Human-readable name for log messages only. */
    FString DisplayName;

    /**
     * The spawned ghost actor.
     * Has mesh+Niagara components mirroring the real actor.
     * No collision, no physics, no tick.
     */
    UPROPERTY()
    TObjectPtr<AActor> GhostActor = nullptr;

    bool bIsTorpedo = false;

    /** First/last timestamps where this actor was recorded alive */
    float FirstSeenTime = -1.f;
    float LastSeenTime = -1.f;

    /** Transforms at the boundary of the lifetime window */
    FVector  FirstKnownLocation = FVector::ZeroVector;
    FRotator FirstKnownRotation = FRotator::ZeroRotator;
    FVector  LastKnownLocation = FVector::ZeroVector;
    FRotator LastKnownRotation = FRotator::ZeroRotator;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnReplayPlaybackStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnReplayPlaybackFinished);

/**
 * UReplayPlaybackComponent
 *
 * Attach to ASubmarineGameMode alongside UReplayRecorderComponent.
 *
 * During a death replay:
 *
 *  GHOST SPAWNING
 *   For each unique actor name in the replay slice, one ghost AActor is spawned.
 *   If the live actor has a UReplayGhostComponent, CloneComponentsOntoGhost()
 *   is called — this copies StaticMesh + Niagara components with ghost-safe flags.
 *   If no UReplayGhostComponent exists (fallback), StaticMeshComponents are
 *   copied directly so the ghost still looks correct.
 *
 *  GHOST POSITIONING
 *   Each tick, ghost actors are moved to interpolated positions from the
 *   recorded tick frames. Torpedo ghosts are clamped to their recorded
 *   lifetime: held at first-known position before they spawned, frozen at
 *   last-known position after they exploded.
 *
 *  VISIBILITY (dead player only)
 *   Real dynamic actors (submarines, torpedoes, and any actor tagged
 *   "ReplayDynamic" — including standalone Niagara explosion actors)
 *   are hidden from the dead PlayerController via PC->HiddenActors.
 *   Other players are completely unaffected.
 *   All visibility changes are restored on StopPlayback.
 *
 *  CAMERA TARGETING
 *   DeathSequenceComponent calls GetKillerGhost() / GetDeadSubGhost() /
 *   GetKillerTorpedoGhost() to aim the death cam at the correct ghost.
 */
UCLASS(ClassGroup = (Replay), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API UReplayPlaybackComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UReplayPlaybackComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // -----------------------------------------------------------------------
    //  Configuration
    // -----------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplayPlayback")
    TObjectPtr<UReplaySettings> Settings;

    // -----------------------------------------------------------------------
    //  Playback API
    // -----------------------------------------------------------------------

    /**
     * Begin replaying the given slice.
     *
     * @param Slice             Recorded data slice from ReplayRecorderComponent::ExtractSlice.
     * @param DeadController    Dead player's controller. Real actors hidden for this PC only.
     * @param StartTime         Timestamp to start from within the slice (-1 = slice start).
     * @param InPlaybackSpeed   Speed multiplier (1.0 = real time).
     */
    UFUNCTION(BlueprintCallable, Category = "ReplayPlayback")
    void BeginPlayback(UReplayData* Slice,
        AController* DeadController,
        float        StartTime = -1.f,
        float        InPlaybackSpeed = 1.f);

    /** Stop playback, destroy ghosts, restore real actor visibility. */
    UFUNCTION(BlueprintCallable, Category = "ReplayPlayback")
    void StopPlayback();

    UFUNCTION(BlueprintPure, Category = "ReplayPlayback")
    bool IsPlaying() const { return bPlaying; }

    /** 0..1 progress through the replay slice. */
    UFUNCTION(BlueprintPure, Category = "ReplayPlayback")
    float GetPlaybackProgress() const;

    // -----------------------------------------------------------------------
    //  Ghost actor access (used by DeathSequenceComponent)
    // -----------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "ReplayPlayback")
    /** Returns the ghost actor for the given GUID, or nullptr. */
    AActor* GetGhostForGuid(const FGuid& Guid) const;

    UFUNCTION(BlueprintPure, Category = "ReplayPlayback")
    AActor* GetKillerGhost()        const { return GetGhostForGuid(KillerActorGuid); }

    UFUNCTION(BlueprintPure, Category = "ReplayPlayback")
    AActor* GetDeadSubGhost()       const { return GetGhostForGuid(DeadSubGuid); }

    UFUNCTION(BlueprintPure, Category = "ReplayPlayback")
    AActor* GetKillerTorpedoGhost() const { return GetGhostForGuid(KillerTorpedoGuid); }

    // -----------------------------------------------------------------------
    //  Context — set by DeathSequenceComponent before calling BeginPlayback
    // -----------------------------------------------------------------------

    /** GUID of the killer submarine (or firing submarine if killed by torpedo). */
    UPROPERTY(BlueprintReadWrite, Category = "ReplayPlayback")
    FGuid KillerActorGuid;

    /** GUID of the killing torpedo. Invalid if killed by sub directly or environment. */
    UPROPERTY(BlueprintReadWrite, Category = "ReplayPlayback")
    FGuid KillerTorpedoGuid;

    /** GUID of the dead submarine. */
    UPROPERTY(BlueprintReadWrite, Category = "ReplayPlayback")
    FGuid DeadSubGuid;

    // -----------------------------------------------------------------------
    //  Delegates
    // -----------------------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "ReplayPlayback|Events")
    FOnReplayPlaybackStarted  OnPlaybackStarted;

    UPROPERTY(BlueprintAssignable, Category = "ReplayPlayback|Events")
    FOnReplayPlaybackFinished OnPlaybackFinished;

private:
    bool  bPlaying = false;
    float PlaybackSpeed = 1.f;
    float PlaybackTime = 0.f;
    float LastVFXCheckTime = 0.f;

    UPROPERTY()
    TObjectPtr<UReplayData> ActiveSlice;

    TWeakObjectPtr<AController> CachedDeadController;

    TArray<FGhostActorEntry> GhostEntries;

    /** Actors added to PC->HiddenActors — stored for restoration. */
    UPROPERTY()
    TArray<TObjectPtr<AActor>> HiddenActors;

    // Ghost management
    void SpawnGhosts();
    void DestroyGhosts();

    /**
     * Spawns a ghost AActor for the given live actor.
     * Prefers UReplayGhostComponent::CloneComponentsOntoGhost if present.
     * Falls back to direct StaticMeshComponent copy otherwise.
     */
    AActor* SpawnGhostForActor(AActor* RealActor, const FVector& SpawnLoc, const FRotator& SpawnRot);

    /** Fallback mesh copy used when no UReplayGhostComponent is present. */
    void FallbackCopyStaticMeshes(AActor* Source, AActor* Target);

    // Visibility
    void HideRealActorsForDeadPlayer();
    void RestoreRealActors();

    // Tick
    void TickGhostPositions();
    void TickVFXEvents();

    // Position interpolation -- pure GUID lookup, no special cases
    bool InterpolateActorTransform(const FGuid& ActorGuid,
        float ClampBeforeTime,   // don't use NextIdx frames after this time
        FVector& OutLocation,
        FRotator& OutRotation) const;

    void ComputeActorLifetimeBounds(const FGuid& ActorGuid,
        float& OutFirstTime,
        float& OutLastTime,
        FVector& OutFirstLoc,
        FRotator& OutFirstRot,
        FVector& OutLastLoc,
        FRotator& OutLastRot) const;

    // Resolve a display name to a GUID via the slice registry (for legacy string fields)
    FGuid ResolveNameToGuid(const FString& DisplayName) const;
    
    const UReplaySettings* GetSettings() const;
};