// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarineTorpedoComponent.h"
#include "TorpedoPawn.h"
#include "TorpedoCharacteristics.h"
#include "SubmarineCharacteristics.h"
#include "SubmarinePawn.h"
#include "Engine/World.h"

USubmarineTorpedoComponent::USubmarineTorpedoComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

// -----------------------------------------------------------------------------
//  BeginPlay
// -----------------------------------------------------------------------------
void USubmarineTorpedoComponent::BeginPlay()
{
    Super::BeginPlay();

    // Fill ammo to capacity on start
    CurrentNormalTorpedoes = NormalTorpedoCapacity;
    CurrentSpecialTorpedoes = SpecialTorpedoCapacity;

    // Broadcast initial state
    OnAmmoChanged.Broadcast(CurrentNormalTorpedoes, CurrentSpecialTorpedoes);
}

// -----------------------------------------------------------------------------
//  Tick — handle shared cooldown + reload
// -----------------------------------------------------------------------------
void USubmarineTorpedoComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // -- Shared fire cooldown ----------------------------------------------
    if (FireCooldownRemaining > 0.f)
        FireCooldownRemaining = FMath::Max(0.f, FireCooldownRemaining - DeltaTime);

    // -- Normal torpedo reload ---------------------------------------------
    // Reload one torpedo at a time. Timer starts automatically when below capacity.
    if (CurrentNormalTorpedoes < NormalTorpedoCapacity)
    {
        if (!bReloading)
        {
            // Begin a new reload cycle
            bReloading = true;
            ReloadTimeRemaining = ReloadNormalTorpedoCooldown;
        }
        else
        {
            ReloadTimeRemaining -= DeltaTime;
            if (ReloadTimeRemaining <= 0.f)
            {
                CurrentNormalTorpedoes = FMath::Min(CurrentNormalTorpedoes + 1,
                    NormalTorpedoCapacity);
                bReloading = false;
                ReloadTimeRemaining = 0.f;

                OnAmmoChanged.Broadcast(CurrentNormalTorpedoes, CurrentSpecialTorpedoes);
                OnReloadComplete.Broadcast();

                UE_LOG(LogTemp, Log, TEXT("[TorpedoComp] Reload complete. Normal=%d/%d"),
                    CurrentNormalTorpedoes, NormalTorpedoCapacity);
            }
        }
    }
    else
    {
        // Full — reset reload state
        bReloading = false;
        ReloadTimeRemaining = 0.f;
    }
}

// -----------------------------------------------------------------------------
//  FireNormalTorpedo
// -----------------------------------------------------------------------------
ATorpedoPawn* USubmarineTorpedoComponent::FireNormalTorpedo()
{
    if (!CanFire())
    {
        UE_LOG(LogTemp, Warning, TEXT("[TorpedoComp] Cannot fire — cooldown %.2fs remaining"),
            FireCooldownRemaining);
        return nullptr;
    }
    if (CurrentNormalTorpedoes <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TorpedoComp] Cannot fire — no normal torpedoes"));
        return nullptr;
    }
    if (!NormalTorpedoBlueprintClass)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[TorpedoComp] NormalTorpedoBlueprintClass not set! Assign it in the submarine Blueprint."));
        return nullptr;
    }

    ATorpedoPawn* Torpedo = SpawnTorpedo(NormalTorpedoBlueprintClass, NormalTorpedoCharacteristics);
    if (!Torpedo) return nullptr;

    --CurrentNormalTorpedoes;
    FireCooldownRemaining = FireCooldown;

    OnAmmoChanged.Broadcast(CurrentNormalTorpedoes, CurrentSpecialTorpedoes);
    OnTorpedoFired.Broadcast(
        NormalTorpedoCharacteristics ? NormalTorpedoCharacteristics->TorpedoType : ETorpedoType::Normal,
        Torpedo);

    UE_LOG(LogTemp, Log, TEXT("[TorpedoComp] Normal torpedo fired. Remaining: %d/%d"),
        CurrentNormalTorpedoes, NormalTorpedoCapacity);

    return Torpedo;
}

// -----------------------------------------------------------------------------
//  FireSpecialTorpedo
// -----------------------------------------------------------------------------
ATorpedoPawn* USubmarineTorpedoComponent::FireSpecialTorpedo()
{
    if (!CanFire())
    {
        UE_LOG(LogTemp, Warning, TEXT("[TorpedoComp] Cannot fire — cooldown %.2fs remaining"),
            FireCooldownRemaining);
        return nullptr;
    }
    if (CurrentSpecialTorpedoes <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TorpedoComp] Cannot fire — no special torpedoes"));
        return nullptr;
    }
    if (!SpecialTorpedoBlueprintClass)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[TorpedoComp] SpecialTorpedoBlueprintClass not set! Assign it in the submarine Blueprint."));
        return nullptr;
    }

    ATorpedoPawn* Torpedo = SpawnTorpedo(SpecialTorpedoBlueprintClass, SpecialTorpedoCharacteristics);
    if (!Torpedo) return nullptr;

    --CurrentSpecialTorpedoes;
    FireCooldownRemaining = FireCooldown;

    OnAmmoChanged.Broadcast(CurrentNormalTorpedoes, CurrentSpecialTorpedoes);
    OnTorpedoFired.Broadcast(
        SpecialTorpedoCharacteristics ? SpecialTorpedoCharacteristics->TorpedoType : ETorpedoType::Heavy,
        Torpedo);

    UE_LOG(LogTemp, Log, TEXT("[TorpedoComp] Special torpedo fired. Remaining: %d/%d"),
        CurrentSpecialTorpedoes, SpecialTorpedoCapacity);

    return Torpedo;
}

// -----------------------------------------------------------------------------
//  SpawnTorpedo — shared deferred spawn logic
// -----------------------------------------------------------------------------
ATorpedoPawn* USubmarineTorpedoComponent::SpawnTorpedo(
    TSubclassOf<ATorpedoPawn> BlueprintClass,
    UTorpedoCharacteristics* TorpedoDA)
{
    UWorld* World = GetWorld();
    if (!World) return nullptr;

    ASubmarinePawn* OwnerSub = Cast<ASubmarinePawn>(GetOwner());
    if (!OwnerSub) return nullptr;

    const USubmarineCharacteristics* SubStats = GetSubStats();

    // -- Compute spawn transform -------------------------------------------
    // Spawn position: submarine location + TorpedoSpawnOffset rotated into world space
    const FVector SpawnOffset = SubStats
        ? SubStats->TorpedoSpawnOffset
        : FVector(300.f, 0.f, 0.f); // fallback: 3m ahead

    const FVector WorldOffset =
        OwnerSub->GetActorTransform().TransformVector(SpawnOffset);
    const FVector SpawnLocation = OwnerSub->GetActorLocation() + WorldOffset;
    const FRotator SpawnRotation = OwnerSub->GetActorRotation();

    // -- Deferred spawn so we can configure before BeginPlay ---------------
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    SpawnParams.Owner = OwnerSub;

    ATorpedoPawn* Torpedo = World->SpawnActorDeferred<ATorpedoPawn>(
        BlueprintClass, FTransform(SpawnRotation, SpawnLocation), OwnerSub,
        nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);

    if (!Torpedo)
    {
        UE_LOG(LogTemp, Error, TEXT("[TorpedoComp] SpawnActorDeferred returned nullptr!"));
        return nullptr;
    }

    // -- Hand over the DA before BeginPlay runs ----------------------------
    Torpedo->SetCharacteristics(TorpedoDA);

    // -- Compute initial world velocity ------------------------------------
    // Base: submarine's current linear speed along its forward axis
    const FVector SubForwardVel =
        OwnerSub->GetActorForwardVector() * OwnerSub->CurrentLinearSpeed;

    // Add the torpedo's own InitialSpeedOffset along the submarine's forward vector
    const float SpeedOffset = TorpedoDA ? TorpedoDA->InitialSpeedOffset : 1500.f;
    const FVector TorpedoKick = OwnerSub->GetActorForwardVector() * SpeedOffset;

    const FVector InitialVelocity = SubForwardVel + TorpedoKick;
    Torpedo->SetInitialVelocity(InitialVelocity);

    // -- Associate the owner so the torpedo ignores it
    //  The torpedo checks if it's its owner
    Torpedo->FiringShooter = OwnerSub;

    // -- Finish deferred spawn ---------------------------------------------
    Torpedo->FinishSpawning(FTransform(SpawnRotation, SpawnLocation));

    return Torpedo;
}

// -----------------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------------
const USubmarineCharacteristics* USubmarineTorpedoComponent::GetSubStats() const
{
    if (const ASubmarinePawn* OwnerSub = Cast<ASubmarinePawn>(GetOwner()))
    {
        return OwnerSub->Characteristics
            ? OwnerSub->Characteristics.Get()
            : GetDefault<USubmarineCharacteristics>();
    }
    return GetDefault<USubmarineCharacteristics>();
}