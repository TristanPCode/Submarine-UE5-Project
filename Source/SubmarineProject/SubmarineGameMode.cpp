// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarineGameMode.h"
#include "SubmarinePawn.h"
#include "SubmarineCollisionComponent.h"
#include "ReplayRecorderComponent.h"
#include "ReplaySettings.h"
#include "ReplayData.h"
#include "CameraBlendSettings.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

ASubmarineGameMode::ASubmarineGameMode()
{
    DefaultPawnClass = ASubmarinePawn::StaticClass();

    ReplayRecorder = CreateDefaultSubobject<UReplayRecorderComponent>(TEXT("ReplayRecorder"));
    DeathSequence = CreateDefaultSubobject<UDeathSequenceComponent>(TEXT("DeathSequence"));
}

// ---------------------------------------------------------------------------
//  BeginPlay — wire delegates and propagate settings
// ---------------------------------------------------------------------------
void ASubmarineGameMode::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTemp, Log, TEXT("[GameMode] BeginPlay START"));

    // Forward settings to components
    if (ReplayRecorder)
    {
        ReplayRecorder->Settings = ReplaySettings;
        UE_LOG(LogTemp, Log, TEXT("[GameMode] ReplayRecorder settings: %s"),
            ReplaySettings ? *ReplaySettings->GetName() : TEXT("NONE - using CDO defaults"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[GameMode] ReplayRecorder component is NULL!"));
    }

    if (DeathSequence)
    {
        DeathSequence->ReplaySettings = ReplaySettings;

        // Bind the completion delegate — clear first to avoid double-binding
        // if BeginPlay is called more than once (e.g. PIE restart)
        DeathSequence->OnDeathSequenceComplete.RemoveDynamic(
            this, &ASubmarineGameMode::OnDeathSequenceComplete);
        DeathSequence->OnDeathSequenceComplete.AddDynamic(
            this, &ASubmarineGameMode::OnDeathSequenceComplete);

        UE_LOG(LogTemp, Log, TEXT("[GameMode] DeathSequence delegate bound OK"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[GameMode] DeathSequence component is NULL!"));
    }

    UE_LOG(LogTemp, Log, TEXT("[GameMode] BeginPlay complete. Recording: %s"),
        IsRecording() ? TEXT("YES") : TEXT("NO"));
}

// ---------------------------------------------------------------------------
//  OnSubmarineDied
// ---------------------------------------------------------------------------
void ASubmarineGameMode::OnSubmarineDied(ASubmarinePawn* DeadSubmarine,
    AController* DeadController,
    AActor* Killer)
{
    UE_LOG(LogTemp, Log, TEXT("[GameMode] OnSubmarineDied called: Sub='%s' Controller='%s' Killer='%s'"),
        DeadSubmarine ? *DeadSubmarine->GetName() : TEXT("NULL"),
        DeadController ? *DeadController->GetName() : TEXT("NULL"),
        Killer ? *Killer->GetName() : TEXT("NULL"));

    if (!DeadSubmarine || !DeadController || !GetWorld())
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] OnSubmarineDied — invalid params, aborting"));
        return;
    }

    // Ensure DeathSequence delegate is bound even if BeginPlay was missed
    if (DeathSequence &&
        !DeathSequence->OnDeathSequenceComplete.IsBound())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[GameMode] OnDeathSequenceComplete was not bound — binding now (BeginPlay may have been skipped)"));
        DeathSequence->OnDeathSequenceComplete.AddDynamic(
            this, &ASubmarineGameMode::OnDeathSequenceComplete);
    }

    PendingDeadSub = DeadSubmarine;
    PendingDeadController = DeadController;

    // Freeze the submarine immediately (hides mesh, disables input,
    // unpossesses controller so death cam can take over the view).
    DeadSubmarine->FreezeOnDeath();

    // --- Extract the last N seconds of replay as a death slice ---------------
    UReplayData* Slice = nullptr;
    const UReplaySettings* RS = ReplaySettings ? ReplaySettings.Get()
        : GetDefault<UReplaySettings>();
    if (ReplayRecorder && RS->DeathReplayDuration > 0.f)
    {
        const float Now = GetWorld()->GetTimeSeconds();
        const float Start = Now - (RS->DeathReplayDuration + RS->DeathReplayLeadIn);
        Slice = ReplayRecorder->ExtractSlice(Start, Now);
    }

    // --- Start the death sequence ---------------------------------------------
    if (DeathSequence)
    {
        UE_LOG(LogTemp, Log, TEXT("[GameMode] Calling BeginDeathSequence"));
        DeathSequence->BeginDeathSequence(DeadSubmarine, DeadController, Killer, Slice);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] DeathSequence is null — going straight to spectator"));
        OnDeathSequenceComplete();
    }
}

// ---------------------------------------------------------------------------
//  OnDeathSequenceComplete  (called when delay + death cam are done)
// ---------------------------------------------------------------------------
void ASubmarineGameMode::OnDeathSequenceComplete()
{
    UE_LOG(LogTemp, Log, TEXT("[GameMode] OnDeathSequenceComplete fired"));

    ASubmarinePawn* DeadSub = PendingDeadSub.Get();
    AController* DC = PendingDeadController.Get();

    if (!DC || !GetWorld())
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] OnDeathSequenceComplete — controller invalid, skipping spectator spawn"));
        return;
    }

    if (!SpectatorPawnClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] SpectatorPawnClass not set!"));
        return;
    }

    // Collect live submarines (exclude the dead one)
    TArray<ASubmarinePawn*> AllSubs;
    for (TActorIterator<ASubmarinePawn> It(GetWorld()); It; ++It)
    {
        if (IsValid(*It) && *It != DeadSub)
            AllSubs.Add(*It);
    }
    UE_LOG(LogTemp, Log, TEXT("[GameMode] Live submarines for spectator: %d"), AllSubs.Num());

    // Spawn spectator at the dead sub's last known location
    FTransform SpawnTransform = DeadSub
        ? DeadSub->GetActorTransform()
        : FTransform::Identity;

    FActorSpawnParameters Params;
    Params.Owner = DC;

    ASubmarineSpectatorPawn* Spectator = GetWorld()->SpawnActor<ASubmarineSpectatorPawn>(
        SpectatorPawnClass, SpawnTransform, Params);

    if (!Spectator)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] Failed to spawn SpectatorPawn!"));
        return;
    }

    DC->Possess(Spectator);
    Spectator->InitSpectator(AllSubs, true);

    UE_LOG(LogTemp, Log, TEXT("[GameMode] Spectator possessed and initialised"));

    // Destroy the dead submarine mesh (VFX already fired from CollisionComponent)
    if (IsValid(DeadSub))
    {
        DeadSub->Destroy();
        UE_LOG(LogTemp, Log, TEXT("[GameMode] Dead submarine destroyed"));
    }

    // Reset pending references
    PendingDeadSub = nullptr;
    PendingDeadController = nullptr;
}

// ---------------------------------------------------------------------------
//  Replay forwarding
// ---------------------------------------------------------------------------
void ASubmarineGameMode::StartRecording()
{
    if (ReplayRecorder) ReplayRecorder->StartRecording();
}

void ASubmarineGameMode::StopRecording()
{
    if (ReplayRecorder) ReplayRecorder->StopRecording();
}

bool ASubmarineGameMode::SaveReplay(const FString& Label)
{
    return ReplayRecorder ? ReplayRecorder->SaveReplay(Label) : false;
}

bool ASubmarineGameMode::LoadReplay()
{
    return ReplayRecorder ? ReplayRecorder->LoadReplay() : false;
}

bool ASubmarineGameMode::IsRecording() const
{
    return ReplayRecorder ? ReplayRecorder->IsRecording() : false;
}