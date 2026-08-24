#include "TorpedoWakeComponent.h"
#include "OceanWakeRegistry.h"
#include "TorpedoPhysicsComponent.h"
#include "OceanWakeRegistry.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

UTorpedoWakeComponent::UTorpedoWakeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

// ---------------------------------------------------------------------------
//  BeginPlay
// ---------------------------------------------------------------------------
void UTorpedoWakeComponent::BeginPlay()
{
    Super::BeginPlay();

    AActor* Owner = GetOwner();
    if (!Owner) return;

    PhysicsComp = Owner->FindComponentByClass<UTorpedoPhysicsComponent>();
    if (!PhysicsComp)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[TorpedoWakeComponent] '%s': No UTorpedoPhysicsComponent found. "
                "Wake will not activate."),
            *Owner->GetName());
    }

    WakeFX = CreateAttachedFX(TorpedoWakeSystem, TEXT("TorpedoWakeFX"));
    SetWakeLOD(EWakeLOD::Off);

    // Register with the registry. The registry enforces the torpedo budget.
    UWorld* World = GetWorld();
    if (World)
    {
        UOceanWakeRegistry* Registry = World->GetSubsystem<UOceanWakeRegistry>();
        if (Registry)
        {
            Registry->RegisterTorpedoWake(this);
        }
    }
}

// ---------------------------------------------------------------------------
//  EndPlay
// ---------------------------------------------------------------------------
void UTorpedoWakeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UWorld* World = GetWorld();
    if (World)
    {
        UOceanWakeRegistry* Registry = World->GetSubsystem<UOceanWakeRegistry>();
        if (Registry)
        {
            Registry->UnregisterTorpedoWake(this);
        }
    }

    Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
//  TickComponent
// ---------------------------------------------------------------------------
void UTorpedoWakeComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (CurrentLOD == EWakeLOD::Off) return;

    // Frame skipping.
    ++FrameSkipCounter;
    const int32 RequiredInterval = (CurrentLOD == EWakeLOD::Full) ? 1
        : (CurrentLOD == EWakeLOD::Reduced) ? 2 : 4;
    if (FrameSkipCounter < RequiredInterval) return;
    FrameSkipCounter = 0;

    if (!PhysicsComp || !WakeFX) return;

    // Wake only visible near the surface. UTorpedoPhysicsComponent computes
    // bIsNearSurface each tick so we just read it.
    if (!PhysicsComp->bIsNearSurface)
    {
        // Stop spawning new particles but let existing ones age out.
        WakeFX->SetVariableFloat(TEXT("TorpedoSpeed"), 0.f);
        return;
    }

    const float Speed = PhysicsComp->PhysicsVelocity.Size();
    WakeFX->SetVariableFloat(TEXT("TorpedoSpeed"), Speed);
    WakeFX->SetVariableInt(TEXT("WakeLOD"), (int32)CurrentLOD);
}

// ---------------------------------------------------------------------------
//  SetWakeLOD
// ---------------------------------------------------------------------------
void UTorpedoWakeComponent::SetWakeLOD(EWakeLOD NewLOD)
{
    if (CurrentLOD == NewLOD) return;
    CurrentLOD = NewLOD;
    ApplyLODToFX();
}

// ---------------------------------------------------------------------------
//  ApplyLODToFX
// ---------------------------------------------------------------------------
void UTorpedoWakeComponent::ApplyLODToFX()
{
    if (!WakeFX) return;

    const bool bActive = (CurrentLOD != EWakeLOD::Off);
    if (bActive && !WakeFX->IsActive())
        WakeFX->Activate();
    else if (!bActive && WakeFX->IsActive())
        WakeFX->Deactivate();

    WakeFX->SetVariableInt(TEXT("WakeLOD"), (int32)CurrentLOD);
}

// ---------------------------------------------------------------------------
//  CreateAttachedFX
// ---------------------------------------------------------------------------
UNiagaraComponent* UTorpedoWakeComponent::CreateAttachedFX(UNiagaraSystem* System,
    FName ComponentName)
{
    if (!System) return nullptr;

    AActor* Owner = GetOwner();
    if (!Owner) return nullptr;

    UNiagaraComponent* FXComp = NewObject<UNiagaraComponent>(Owner, ComponentName);
    FXComp->SetAsset(System);
    FXComp->bAutoActivate = false;
    FXComp->SetupAttachment(Owner->GetRootComponent());
    FXComp->RegisterComponent();

    return FXComp;
}