// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SubmarineSpectatorPawn.h"
#include "DeathSequenceComponent.h"
#include "SubmarineGameMode.generated.h"

class ASubmarinePawn;
class UReplayRecorderComponent;
class UReplaySettings;
class UCameraBlendSettings;

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

    // -----------------------------------------------------------------------
    //  Components (visible in editor for easy setup)
    // -----------------------------------------------------------------------

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UReplayRecorderComponent> ReplayRecorder;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UDeathSequenceComponent> DeathSequence;

private:
    /**
     * Called by DeathSequenceComponent when the sequence is fully complete.
     * Spawns the spectator pawn and destroys the dead submarine mesh.
     */
    UFUNCTION()
    void OnDeathSequenceComplete();

    // Kept alive across the death sequence so the handler has valid data
    TWeakObjectPtr<ASubmarinePawn> PendingDeadSub;
    TWeakObjectPtr<AController>    PendingDeadController;
};