// Fill out your copyright notice in the Description page of Project Settings.

#include "ReplayHelpers.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "GameFramework/Actor.h"

const FName UReplayHelpers::Tag_ReplayDynamic(TEXT("ReplayDynamic"));
const FName UReplayHelpers::Tag_ReplayGhost(TEXT("ReplayGhost"));

// Static registry storage
TSet<TWeakObjectPtr<APlayerController>> UReplayHelpers::ActiveDeadPlayers;

void UReplayHelpers::RegisterDeadPlayer(APlayerController* PC)
{
    if (PC) ActiveDeadPlayers.Add(TWeakObjectPtr<APlayerController>(PC));
}

void UReplayHelpers::UnregisterDeadPlayer(APlayerController* PC)
{
    if (PC) ActiveDeadPlayers.Remove(TWeakObjectPtr<APlayerController>(PC));
}

bool UReplayHelpers::IsDeadPlayer(APlayerController* PC)
{
    return PC && ActiveDeadPlayers.Contains(TWeakObjectPtr<APlayerController>(PC));
}

// ---------------------------------------------------------------------------
//  SpawnGameplayVFXAtLocation
//
//  Live gameplay effects (torpedo/sub explosions during real play).
//  No per-player visibility restrictions -- all players see it.
//  Call this from ATorpedoPawn::Explode, TriggerDeathExplosion, etc.
// ---------------------------------------------------------------------------
UNiagaraComponent* UReplayHelpers::SpawnGameplayVFXAtLocation(
    const UObject* WorldContextObject,
    UNiagaraSystem* SystemTemplate,
    FVector         Location,
    FRotator        Rotation,
    FVector         Scale,
    bool            bAutoDestroy,
    bool            bAutoActivate,
    ENCPoolMethod   PoolingMethod,
    bool            bPreCullCheck)
{
    if (!SystemTemplate) return nullptr;

    UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
        WorldContextObject,
        SystemTemplate,
        Location, Rotation, Scale,
        bAutoDestroy, bAutoActivate,
        PoolingMethod, bPreCullCheck);

    // Hide live gameplay VFX from any PlayerController currently watching a replay.
    // The VFX owner (WorldSettings in UE5) is added to their HiddenActors so they
    // only see the recorded ghost VFX from TickVFXEvents, not real-world explosions.
    // NOTE: This hides WorldSettings from dead players -- acceptable because
    // WorldSettings itself has no visual representation.
    if (NC)
    {
        if (AActor* VFXOwner = NC->GetOwner())
        {
            UWorld* World = GEngine->GetWorldFromContextObject(
                WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
            if (World)
            {
                for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
                {
                    APlayerController* PC = Cast<APlayerController>(It->Get());
                    if (PC && IsDeadPlayer(PC))
                        PC->HiddenActors.Add(VFXOwner);
                }
            }
        }
    }

    return NC;
}

// ---------------------------------------------------------------------------
//  SpawnReplayVFXAtLocation
//
//  VFX events played back during a death replay (called from TickVFXEvents).
//
//  Spawns a dedicated lightweight holder AActor as the Niagara owner.
//  This solves the WorldSettings ownership problem: we control the holder
//  so we can add it to specific PlayerControllers' HiddenActors.
//
//  Visibility rules:
//    DeadPC (player watching the replay) : SEES the effect.
//    All other live PlayerControllers    : effect is HIDDEN from them.
//
//  The holder auto-destroys shortly after the Niagara system finishes.
// ---------------------------------------------------------------------------
UNiagaraComponent* UReplayHelpers::SpawnReplayVFXAtLocation(
    const UObject* WorldContextObject,
    UNiagaraSystem* SystemTemplate,
    FVector             Location,
    FRotator            Rotation,
    FVector             Scale,
    APlayerController* DeadPC,
    bool                bAutoDestroy,
    bool                bAutoActivate,
    ENCPoolMethod       PoolingMethod)
{
    if (!SystemTemplate) return nullptr;

    UWorld* World = GEngine->GetWorldFromContextObject(
        WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World) return nullptr;

    // ------------------------------------------------------------------
    //  1. Spawn a dedicated holder actor so we own the VFX hierarchy.
    //     Using WorldSettings as owner (as SpawnSystemAtLocation does) 
    //     prevents per-player visibility control.
    // ------------------------------------------------------------------
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.ObjectFlags = RF_Transient;

    AActor* Holder = World->SpawnActor<AActor>(
        AActor::StaticClass(), Location, Rotation, SpawnParams);
    if (!Holder) return nullptr;

    Holder->Tags.Add(Tag_ReplayDynamic);
    Holder->SetActorTickEnabled(false);

    USceneComponent* Root = NewObject<USceneComponent>(Holder);
    Root->RegisterComponent();
    Holder->SetRootComponent(Root);

    // ------------------------------------------------------------------
    //  2. Attach the Niagara system to the holder, not WorldSettings.
    //     SpawnSystemAttached uses the component's parent as owner.
    // ------------------------------------------------------------------
    UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAttached(
        SystemTemplate,
        Holder->GetRootComponent(),
        NAME_None,
        Location,
        Rotation,
        EAttachLocation::KeepWorldPosition,
        bAutoDestroy,
        bAutoActivate,
        PoolingMethod,
        /*bPreCullCheck=*/false);  // Never cull -- replay cam has non-standard view path

    if (!NC)
    {
        Holder->Destroy();
        return nullptr;
    }

    NC->SetWorldScale3D(Scale);

    // ------------------------------------------------------------------
    //  3. Per-player visibility.
    //     DeadPC sees it. Every other PlayerController does not.
    // ------------------------------------------------------------------
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = Cast<APlayerController>(It->Get());
        if (PC && PC != DeadPC)
            PC->HiddenActors.Add(Holder);
    }

    // ------------------------------------------------------------------
    //  4. Auto-destroy the holder shortly after the Niagara system ends
    //     to avoid littering the world with empty actors.
    // ------------------------------------------------------------------
    FTimerHandle CleanupHandle;

    TWeakObjectPtr<AActor> WeakHolder = Holder;
    TWeakObjectPtr<UNiagaraComponent> WeakNC = NC;

    World->GetTimerManager().SetTimer(
        CleanupHandle,
        [WeakHolder, WeakNC]()
        {
            if (!WeakHolder.IsValid())
                return;

            // Niagara Destroyed/Finished
            if (!WeakNC.IsValid())
            {
                WeakHolder->Destroy();
            }
        },
        0.25f,   // Check 4x/s
        true
    );

    // Fallback hard cleanup
    Holder->SetLifeSpan(15.f);

    return NC;
}