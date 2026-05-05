#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ReplayData.h"
#include "ReplayRecorderComponent.generated.h"

class UReplaySettings;
class ASubmarinePawn;
class ATorpedoPawn;

// Fired when recording starts / stops
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnReplayRecordingChanged);

/**
 * UReplayRecorderComponent
 *
 * Attach to ASubmarineGameMode.
 *
 * Automatically discovers all ASubmarinePawn and ATorpedoPawn actors in the
 * level each tick and records their state into a rolling UReplayData buffer.
 *
 * Recording can be started/stopped at any time via StartRecording() /
 * StopRecording().  The buffer rolls over MaxRecordDuration seconds
 * (configured in UReplaySettings).
 *
 * Save to disk:  SaveReplay()
 * Load from disk: LoadReplay()
 */
UCLASS(ClassGroup = (Replay), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API UReplayRecorderComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UReplayRecorderComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // -----------------------------------------------------------------------
    //  Configuration
    // -----------------------------------------------------------------------

    /** Assign the game-wide ReplaySettings DataAsset here in the editor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    TObjectPtr<UReplaySettings> Settings;

    // -----------------------------------------------------------------------
    //  Recording API
    // -----------------------------------------------------------------------

    /** Start recording (clears the current buffer). */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    void StartRecording();

    /** Stop recording (buffer is preserved, can be saved or replayed). */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    void StopRecording();

    /** True while recording is active. */
    UFUNCTION(BlueprintPure, Category = "Replay")
    bool IsRecording() const { return bRecording; }

    // -----------------------------------------------------------------------
    //  Save / Load
    // -----------------------------------------------------------------------

    /** Save the current replay buffer to disk (async-friendly, blocking for now). */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    bool SaveReplay(const FString& Label = TEXT(""));

    /** Load a previously saved replay from disk into LoadedReplay. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    bool LoadReplay();

    // -----------------------------------------------------------------------
    //  Data access
    // -----------------------------------------------------------------------

    /**
     * Returns a copy of the recording buffer sliced to [StartTime, EndTime].
     * Used by the death-replay system to extract the last N seconds.
     * Returns nullptr if the buffer is empty or no data in range.
     */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    UReplayData* ExtractSlice(float StartTime, float EndTime);

    /** The live rolling recording buffer. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Replay")
    TObjectPtr<UReplayData> LiveReplay;

    /** The last loaded replay (from SaveReplay / LoadReplay). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Replay")
    TObjectPtr<UReplayData> LoadedReplay;

    /** Record a VFX event. Called automatically; also callable from Blueprint. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    void RecordVFXEvent(UNiagaraSystem* Asset, const FVector& Location,
        const FRotator& Rotation, const FVector& Scale);

    // -----------------------------------------------------------------------
    //  Delegates
    // -----------------------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "Replay|Events")
    FOnReplayRecordingChanged OnRecordingStarted;

    UPROPERTY(BlueprintAssignable, Category = "Replay|Events")
    FOnReplayRecordingChanged OnRecordingStopped;

private:
    bool  bRecording = false;
    float TimeSinceLastTickFrame = 0.f;
    float TimeSinceLastKeyframe = 0.f;

    /** Cached list of actors being tracked this tick. Refreshed each tick. */
    TArray<TWeakObjectPtr<AActor>> TrackedActors;

    /** GUIDs present last tick -- for torpedo disappearance detection. */
    TSet<FGuid> PreviousFrameGuids;

    /** Last recorded world location per GUID -- for placing VFX on disappearance. */
    TMap<FGuid, FVector> LastSeenLocation;

    /** Cached Niagara VFX asset per torpedo GUID -- populated when torpedo first seen,
     *  remains valid even after the torpedo is Destroy()ed. */
    TMap<FGuid, TObjectPtr<UNiagaraSystem>> CachedTorpedoVFX;
    TMap<FGuid, float>                      CachedTorpedoVFXScale;

    /** Submarines that already triggered their death VFX -- avoids double-recording. */
    TSet<FGuid> DeadSubmarineGuids;

    void RefreshTrackedActors();
    void DetectAndRecordVFXEvents(float WorldTime);

    void RecordTickFrame(float WorldTime);
    void RecordKeyframe(float WorldTime);

    FReplayActorFrame     BuildActorFrame(AActor* Actor) const;
    FReplayActorSnapshot  BuildActorSnapshot(AActor* Actor) const;

    const UReplaySettings* GetSettings() const;
};