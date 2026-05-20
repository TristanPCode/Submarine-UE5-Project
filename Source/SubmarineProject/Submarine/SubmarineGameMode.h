// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SubmarineSpectatorPawn.h"
#include "DeathSequenceComponent.h"
#include "ReplaySettings.h"
#include "MatchSettingsDataAsset.h"
#include "RuntimeMatchSettings.h"
#include "SpawnManagerComponent.h"
#include "HUD/HUDGlobalDefaults.h"
#include "Load/SubmarineAssetLoader.h"
#include "Load/LoadingScreenSettings.h"
#include "SubmarineGameMode.generated.h"

class ASubmarinePawn;
class ASubmarineSpectatorPawn;
class UReplayRecorderComponent;
class UReplayPlaybackComponent;
class UReplaySettings;
class UCameraBlendSettings;
class USubmarineLoadingScreen;

/**
 * Caches killer identity at the moment of death.
 * We cannot keep a weak pointer to the torpedo — it destroys itself
 * within the same frame or shortly after. Instead we store the name,
 * class, and the firing submarine pointer (submarines persist).
 */
USTRUCT()
struct FKillerInfo
{
    GENERATED_BODY()

    /** Actor name of the killer (torpedo or submarine). Empty = environmental. */
    FString ActorName;

    /**
     * Stable GUID of the killer actor -- survives even after the actor is Destroy()ed
     * because FGuid is a plain value type. Used to identify the ghost in the replay.
     */
    FGuid ActorGuid;

    /** True if the killer was a torpedo. */
    bool bWasTorpedo = false;

    /**
     * The submarine that fired the killing torpedo, or the killer submarine
     * itself. This pointer stays valid after the torpedo is destroyed.
     */
    TWeakObjectPtr<ASubmarinePawn> KillerSubmarine;

    /**
     * The torpedo actor, valid only at the moment of death.
     * Will be null by the time OnPostDeathRecordingComplete fires.
     * Cached here just to extract its name before it dies.
     */
    TWeakObjectPtr<AActor> TorpedoActor;

    void Clear()
    {
        ActorName.Empty();
        ActorGuid.Invalidate();
        bWasTorpedo = false;
        KillerSubmarine = nullptr;
        TorpedoActor = nullptr;
    }
};

/**
 * ASubmarineGameMode
 *
 * Manages:
 *   - Submarine death flow (death sequence -> spectator transition)
 *   - Replay recording (via UReplayRecorderComponent)
 *   - Camera blend settings (via UCameraBlendSettings DA)
 */
UCLASS()
class SUBMARINEPROJECT_API ASubmarineGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ASubmarineGameMode();
    virtual void BeginPlay() override;

    // -----------------------------------------------------------------------
    //  Death flow
    //  Call this from your submarine Blueprint or from OnDied delegate
    // -----------------------------------------------------------------------

    /**
     * Central entry point for submarine death.
     * Runs the death sequence (delay -> death cam -> spectator).
     *
     * @param DeadSubmarine  The submarine that just died.
     * @param DeadController The controller that was possessing it.
     * @param Killer         The actor that dealt the killing blow (may be nullptr).
     */
    UFUNCTION(BlueprintCallable, Category = "GameMode")
    void OnSubmarineDied(ASubmarinePawn* DeadSubmarine,
        AController* DeadController,
        AActor* Killer = nullptr);

    // -----------------------------------------------------------------------
    //  Replay API (forwarded from the recorder component)
    // -----------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "GameMode|Replay")
    void StartRecording();

    UFUNCTION(BlueprintCallable, Category = "GameMode|Replay")
    void StopRecording();

    UFUNCTION(BlueprintCallable, Category = "GameMode|Replay")
    bool SaveReplay(const FString& Label = TEXT(""));

    UFUNCTION(BlueprintCallable, Category = "GameMode|Replay")
    bool LoadReplay();

    UFUNCTION(BlueprintPure, Category = "GameMode|Replay")
    bool IsRecording() const;

    // -----------------------------------------------------------------------
    //  Assets — assign in editor
    // -----------------------------------------------------------------------

    /** Spectator pawn class to spawn when a submarine dies. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameMode")
    TSubclassOf<ASubmarineSpectatorPawn> SpectatorPawnClass;

    /** Replay system parameters. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameMode|Replay")
    TObjectPtr<UReplaySettings> ReplaySettings;

    /** Camera blend settings (shared game-wide). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameMode|Camera")
    TObjectPtr<UCameraBlendSettings> CameraBlendSettings;

    /** Default match settings DataAsset (assigned in Blueprint editor).
      * Copied into RuntimeMatchSettings at BeginPlay. Never mutated. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match")
    TObjectPtr<UMatchSettingsDataAsset> DefaultMatchSettings;

    /** Global HUD defaults registry (assigned in Blueprint editor). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
    TObjectPtr<UHUDGlobalDefaults> HUDGlobalDefaults;

    // -----------------------------------------------------------------------
    //  Loading screen
    // -----------------------------------------------------------------------

    /**
     * Widget class for the loading screen.
     * Create BP_LoadingScreen inheriting from USubmarineLoadingScreen.
     * Leave null to skip the loading screen entirely.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loading")
    TSubclassOf<USubmarineLoadingScreen> LoadingScreenClass;

    /**
     * Per-level loading screen artwork and configuration.
     * Each level/map can have its own DA with different background images.
     * Leave null for a plain black loading screen.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loading")
    TObjectPtr<ULoadingScreenSettings> LoadingScreenSettings;

    /** Minimum time the loading screen is shown (seconds). Useful for testing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loading")
    float MinLoadingScreenTime = 0.f;

    // -----------------------------------------------------------------------
    //  Components (visible in editor for easy setup)
    // -----------------------------------------------------------------------

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UReplayRecorderComponent> ReplayRecorder;

    /** Drives ghost actors from recorded data during the death cam. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UReplayPlaybackComponent> ReplayPlayback;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UDeathSequenceComponent> DeathSequence;

    /** Spawn manager component. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<USpawnManagerComponent> SpawnManager;

    // -----------------------------------------------------------------------
    //  Match settings
    // -----------------------------------------------------------------------

    // Resolved at BeginPlay, used by OnAssetsPreloaded.
    // Stored here temporarily until a menu/GameInstance flow exists.
    UPROPERTY()
    TObjectPtr<URuntimeMatchSettings> ActiveRMS;
    /**
     * If true, BeginPlay skips normal gameplay spawn and instead loads
     * the saved replay from DeathReplaySaveSlot.
     * Set this in BP_SubmarineGameInstance OR toggle here directly for testing.
     * Will be set to false by BeginPlay after it's consumed.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    bool bIsReplayMode = false;

private:

    /**
     * Called at the beginning of BeginPlay before FadeIn.
     */
    UFUNCTION()
    void OnAssetsPreloaded();


    /**
     * Called by DeathSequenceComponent when the sequence is fully complete.
     * Spawns the spectator pawn and destroys the dead submarine mesh.
     */
    UFUNCTION()
    void OnDeathSequenceComplete();

    /**
     * Fired by PostDeathRecordingTimer after DeathPreviewDelay seconds.
     * At this point the explosion VFX has been spawned and the recording
     * has captured it, so we can safely extract the slice and begin playback.
     */
    void OnPostDeathRecordingComplete();

    void ShowLoadingScreen();
    void HideLoadingScreen();

    UFUNCTION()
    void TickLoadingProgress();

    // Kept alive across the death sequence so the handler has valid data
    TWeakObjectPtr<ASubmarinePawn> PendingDeadSub;
    TWeakObjectPtr<AController>    PendingDeadController;

    /**
     * Killer info cached at death time — survives the DeathPreviewDelay
     * even after the torpedo is destroyed.
     */
    FKillerInfo PendingKillerInfo;

    /** Timer that fires OnPostDeathRecordingComplete after DeathPreviewDelay. */
    FTimerHandle PostDeathRecordingTimer;


    FTimerHandle LoadingProgressTimer;

    UPROPERTY()
    TObjectPtr<USubmarineLoadingScreen> ActiveLoadingScreen;

    void ExecutePostLoad();
    float LoadingScreenStartTime = 0.f;

    /* Replay Mode */

    /** Enter replay mode: load replay from disk, skip spawning, set HUD context. */
    void EnterReplayMode();
    void ExecuteReplayLoad();
    void FinishEnterReplayMode(UReplayData* FullReplay);
};