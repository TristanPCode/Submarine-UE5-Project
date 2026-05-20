// Fill out your copyright notice in the Description page of Project Settings.

#include "HUD/SpectatorTrackerComponent.h"
#include "HUD/SubmarineHUDComponent.h"
#include "SubmarinePawn.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

USpectatorTrackerComponent::USpectatorTrackerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

// ---------------------------------------------------------------------------
//  AutoSelectTarget
// ---------------------------------------------------------------------------
ASubmarinePawn* USpectatorTrackerComponent::AutoSelectTarget()
{
    TArray<ASubmarinePawn*> Available = CollectAvailableTargets();
    if (Available.IsEmpty()) return nullptr;

    ApplyTarget(Available[0]);
    return Available[0];
}

// ---------------------------------------------------------------------------
//  NextTarget
// ---------------------------------------------------------------------------
ASubmarinePawn* USpectatorTrackerComponent::NextTarget()
{
    TArray<ASubmarinePawn*> Available = CollectAvailableTargets();
    if (Available.IsEmpty()) return nullptr;

    if (!CurrentTarget.IsValid())
    {
        ApplyTarget(Available[0]);
        return Available[0];
    }

    int32 CurrentIdx = Available.IndexOfByKey(CurrentTarget.Get());
    const int32 NextIdx = (CurrentIdx + 1) % Available.Num();
    ApplyTarget(Available[NextIdx]);
    return Available[NextIdx];
}

// ---------------------------------------------------------------------------
//  PreviousTarget
// ---------------------------------------------------------------------------
ASubmarinePawn* USpectatorTrackerComponent::PreviousTarget()
{
    TArray<ASubmarinePawn*> Available = CollectAvailableTargets();
    if (Available.IsEmpty()) return nullptr;

    if (!CurrentTarget.IsValid())
    {
        ApplyTarget(Available[0]);
        return Available[0];
    }

    int32 CurrentIdx = Available.IndexOfByKey(CurrentTarget.Get());
    const int32 PrevIdx = (CurrentIdx - 1 + Available.Num()) % Available.Num();
    ApplyTarget(Available[PrevIdx]);
    return Available[PrevIdx];
}

// ---------------------------------------------------------------------------
//  CollectAvailableTargets
//
//  Resolution order:
//    1. This local player's own possessed pawn (if ASubmarinePawn)
//    2. Other local players' submarine pawns
//    3. All remaining human-controlled submarine pawns
//    4. All CPU-controlled submarine pawns
// ---------------------------------------------------------------------------
TArray<ASubmarinePawn*> USpectatorTrackerComponent::CollectAvailableTargets() const
{
    UWorld* World = GetWorld();
    if (!World) return {};

    APlayerController* OwnerPC = Cast<APlayerController>(GetOwner());

    TArray<ASubmarinePawn*> Priority1;   // own pawn
    TArray<ASubmarinePawn*> Priority2;   // other local players
    TArray<ASubmarinePawn*> Priority3;   // other human controllers
    TArray<ASubmarinePawn*> Priority4;   // CPU submarines

    for (TActorIterator<ASubmarinePawn> It(World); It; ++It)
    {
        ASubmarinePawn* Sub = *It;
        if (!IsValid(Sub)) continue;

        AController* Controller = Sub->GetController();
        APlayerController* PC = Cast<APlayerController>(Controller);

        if (OwnerPC && Sub == OwnerPC->GetPawn())
        {
            Priority1.Add(Sub);
        }
        else if (PC && PC->IsLocalPlayerController())
        {
            Priority2.Add(Sub);
        }
        else if (PC)
        {
            Priority3.Add(Sub);
        }
        else
        {
            Priority4.Add(Sub);
        }
    }

    TArray<ASubmarinePawn*> Result;
    Result.Append(Priority1);
    Result.Append(Priority2);
    Result.Append(Priority3);
    Result.Append(Priority4);
    return Result;
}

// ---------------------------------------------------------------------------
//  ApplyTarget
// ---------------------------------------------------------------------------
void USpectatorTrackerComponent::ApplyTarget(ASubmarinePawn* Target)
{
    CurrentTarget = Target;

    AActor* Owner = GetOwner();
    if (!Owner) return;

    USubmarineHUDComponent* HUDComp =
        Owner->FindComponentByClass<USubmarineHUDComponent>();
    if (HUDComp)
        HUDComp->SetTrackedSubmarine(Target);

    UE_LOG(LogTemp, Log,
        TEXT("[SpectatorTracker] Tracking '%s'"),
        Target ? *Target->GetName() : TEXT("None"));
}