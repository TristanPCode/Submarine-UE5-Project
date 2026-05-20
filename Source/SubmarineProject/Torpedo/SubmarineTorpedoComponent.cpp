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
    // Required to receive InitializeComponent() call
    bWantsInitializeComponent = true;
}

// -----------------------------------------------------------------------------
//  InitializeComponent
//  Fires after all Blueprint CDO/default overrides are applied.
//  Safe to read capacity values here.
// -----------------------------------------------------------------------------
void USubmarineTorpedoComponent::InitializeComponent()
{
    Super::InitializeComponent();

    // Skip CDO pass -- GetOwner() is null on CDO, only initialize real instances.
    if (!GetOwner()) return;

    // Guard against double-call (UE can call this twice on some init paths).
    if (bAmmoInitialized) return;
    bAmmoInitialized = true;

    // Initialize current ammo from capacity now that all BP defaults are applied.
    CurrentNormalTorpedoes = NormalTorpedoCapacity;
    CurrentSpecialTorpedoes = SpecialTorpedoCapacity;
    UE_LOG(LogTemp, Log, TEXT("[TorpedoComp] InitializeComponent: SP: %d N: %d"),
        SpecialTorpedoCapacity, NormalTorpedoCapacity);
}

// -----------------------------------------------------------------------------
//  BeginPlay
// -----------------------------------------------------------------------------
void USubmarineTorpedoComponent::BeginPlay()
{
    Super::BeginPlay();

    // Ammo counts initialized in InitializeComponent (after BP defaults applied).
    // Clamp here as a safety net in case of late initialization.
    CurrentNormalTorpedoes = FMath::Clamp(CurrentNormalTorpedoes, 0, NormalTorpedoCapacity);
    CurrentSpecialTorpedoes = FMath::Clamp(CurrentSpecialTorpedoes, 0, SpecialTorpedoCapacity);
    UE_LOG(LogTemp, Log, TEXT("[TorpedoComp] InitializeComponent: SP: %d N: %d"),
        SpecialTorpedoCapacity, NormalTorpedoCapacity);

    bWasOnCooldown = false;

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

    // -- Clean up destroyed torpedoes from active list ---------------------
    ActiveTorpedoes.RemoveAll([](const TObjectPtr<ATorpedoPawn>& T)
        {
            return !IsValid(T);
        });

    // -- Fire cooldown -----------------------------------------------------
    if (FireCooldownRemaining > 0.f)
    {
        bWasOnCooldown = true;
        FireCooldownRemaining = FMath::Max(0.f, FireCooldownRemaining - DeltaTime);

        if (FireCooldownRemaining <= 0.f)
        {
            // Edge: cooldown just expired this tick
            OnFireCooldownComplete.Broadcast();

            if (HasNormalTorpedo() || HasSpecialTorpedo())
                OnReadyToFire.Broadcast();

            bWasOnCooldown = false;
        }
    }

    // -- Reload ------------------------------------------------------------
    switch (ReloadMode)
    {
    case ETorpedoReloadMode::Progressive:
        TickProgressiveReload(DeltaTime);
        break;

    case ETorpedoReloadMode::Full:
        TickFullReload(DeltaTime);
        break;
    }
}
// -----------------------------------------------------------------------------
//  Progressive reload — one torpedo at a time, runs while below capacity
// -----------------------------------------------------------------------------
void USubmarineTorpedoComponent::TickProgressiveReload(float DeltaTime)
{
    if (CurrentNormalTorpedoes >= NormalTorpedoCapacity)
    {
        bReloading = false;
        ReloadTimeRemaining = 0.f;
        return;
    }

    if (!bReloading)
    {
        bReloading = true;
        ReloadTimeRemaining = ProgressiveReloadCooldown;
    }
    else
    {
        ReloadTimeRemaining -= DeltaTime;
        if (ReloadTimeRemaining <= 0.f)
        {
            CurrentNormalTorpedoes = FMath::Min(CurrentNormalTorpedoes + 1, NormalTorpedoCapacity);
            bReloading = false;
            ReloadTimeRemaining = 0.f;

            OnAmmoChanged.Broadcast(CurrentNormalTorpedoes, CurrentSpecialTorpedoes);
            OnProgressiveReloadComplete.Broadcast();

            // If we're now at capacity, also fire OnFullReloadComplete for convenience
            if (CurrentNormalTorpedoes >= NormalTorpedoCapacity)
                OnFullReloadComplete.Broadcast();

            // If fire cooldown is also done, we're ready
            if (CanFire())
                OnReadyToFire.Broadcast();

            if (bDebugReloadLogs) {
                UE_LOG(LogTemp, Log, TEXT("[TorpedoComp] Progressive reload: %d/%d"),
                    CurrentNormalTorpedoes, NormalTorpedoCapacity);
            }
        }
    }
}

// -----------------------------------------------------------------------------
//  Full reload — starts ONLY at 0, restores all at once
// -----------------------------------------------------------------------------
void USubmarineTorpedoComponent::TickFullReload(float DeltaTime)
{
    // Only start reload when completely empty
    if (CurrentNormalTorpedoes > 0)
    {
        bReloading = false;
        ReloadTimeRemaining = 0.f;
        return;
    }

    if (!bReloading)
    {
        bReloading = true;
        ReloadTimeRemaining = FullReloadCooldown;
        if (bDebugReloadLogs) {
            UE_LOG(LogTemp, Log, TEXT("[TorpedoComp] Full reload started (%.1fs)"), FullReloadCooldown);
        }
    }
    else
    {
        ReloadTimeRemaining -= DeltaTime;
        if (ReloadTimeRemaining <= 0.f)
        {
            CurrentNormalTorpedoes = NormalTorpedoCapacity;
            bReloading = false;
            ReloadTimeRemaining = 0.f;

            OnAmmoChanged.Broadcast(CurrentNormalTorpedoes, CurrentSpecialTorpedoes);
            OnFullReloadComplete.Broadcast();

            if (CanFire())
                OnReadyToFire.Broadcast();

            if (bDebugReloadLogs) {
                UE_LOG(LogTemp, Log, TEXT("[TorpedoComp] Full reload complete: %d/%d"),
                    CurrentNormalTorpedoes, NormalTorpedoCapacity);
            }
        }
    }
}

// -----------------------------------------------------------------------------
//  GetReloadRatio
// -----------------------------------------------------------------------------
float USubmarineTorpedoComponent::GetReloadRatio() const
{
    if (!bReloading) return 1.f;

    const float Total = (ReloadMode == ETorpedoReloadMode::Progressive)
        ? ProgressiveReloadCooldown
        : FullReloadCooldown;

    return (Total > 0.f)
        ? FMath::Clamp(1.f - ReloadTimeRemaining / Total, 0.f, 1.f)
        : 1.f;
}

// -----------------------------------------------------------------------------
//  FireNormalTorpedo
// -----------------------------------------------------------------------------
ATorpedoPawn* USubmarineTorpedoComponent::FireNormalTorpedo()
{
    // If submarine is dead, do not fire
    if (ASubmarinePawn* OwnerSub = Cast<ASubmarinePawn>(GetOwner()))
        if (OwnerSub->bDead) return nullptr;

    if (!CanFire())
    {
        if (bDebugCooldownLogs) {
            UE_LOG(LogTemp, Warning, TEXT("[TorpedoComp] Cannot fire — cooldown %.2fs remaining"),
                FireCooldownRemaining);
        }
        return nullptr;
    }
    if (CurrentNormalTorpedoes <= 0)
    {
        if (bDebugCooldownLogs) {
            UE_LOG(LogTemp, Warning, TEXT("[TorpedoComp] Cannot fire — no normal torpedoes"));
        }
        return nullptr;
    }
    if (!NormalTorpedoBlueprintClass)
    {
        if (bDebugCooldownLogs) {
            UE_LOG(LogTemp, Error,
                TEXT("[TorpedoComp] NormalTorpedoBlueprintClass not set! Assign it in the submarine Blueprint."));
        }
        return nullptr;
    }

    ATorpedoPawn* Torpedo = SpawnTorpedo(NormalTorpedoBlueprintClass, NormalTorpedoCharacteristics);
    if (!Torpedo) return nullptr;

    --CurrentNormalTorpedoes;
    FireCooldownRemaining = FireCooldown;
    bWasOnCooldown = true;

    ActiveTorpedoes.Add(Torpedo);

    OnAmmoChanged.Broadcast(CurrentNormalTorpedoes, CurrentSpecialTorpedoes);
    OnTorpedoFired.Broadcast(
        NormalTorpedoCharacteristics ? NormalTorpedoCharacteristics->TorpedoType : ETorpedoType::Normal,
        Torpedo);

    // In Full mode: start reload immediately if now at 0
    if (ReloadMode == ETorpedoReloadMode::Full && CurrentNormalTorpedoes == 0)
    {
        bReloading = false; // will be picked up next tick
    }

    if (bDebugCooldownLogs) {
        UE_LOG(LogTemp, Log, TEXT("[TorpedoComp] Normal fired. Remaining: %d/%d"),
            CurrentNormalTorpedoes, NormalTorpedoCapacity);
    }

    return Torpedo;
}



// -----------------------------------------------------------------------------
//  FireSpecialTorpedo
// -----------------------------------------------------------------------------
ATorpedoPawn* USubmarineTorpedoComponent::FireSpecialTorpedo()
{
    // If submarine is dead, do not fire
    if (ASubmarinePawn* OwnerSub = Cast<ASubmarinePawn>(GetOwner()))
        if (OwnerSub->bDead) return nullptr;

    if (!CanFire())
    {
        if (bDebugCooldownLogs) {
            UE_LOG(LogTemp, Warning, TEXT("[TorpedoComp] Cannot fire — cooldown %.2fs remaining"),
                FireCooldownRemaining);
        }
        return nullptr;
    }
    if (CurrentSpecialTorpedoes <= 0)
    {
        if (bDebugCooldownLogs) {
            UE_LOG(LogTemp, Warning, TEXT("[TorpedoComp] Cannot fire — no special torpedoes"));
        }
        return nullptr;
    }
    if (!SpecialTorpedoBlueprintClass)
    {
        if (bDebugCooldownLogs) {
            UE_LOG(LogTemp, Error,
                TEXT("[TorpedoComp] SpecialTorpedoBlueprintClass not set! Assign it in the submarine Blueprint."));
        }
        return nullptr;
    }

    ATorpedoPawn* Torpedo = SpawnTorpedo(SpecialTorpedoBlueprintClass, SpecialTorpedoCharacteristics);
    if (!Torpedo) return nullptr;

    --CurrentSpecialTorpedoes;
    FireCooldownRemaining = FireCooldown;
    bWasOnCooldown = true;

    ActiveTorpedoes.Add(Torpedo);

    OnAmmoChanged.Broadcast(CurrentNormalTorpedoes, CurrentSpecialTorpedoes);
    OnTorpedoFired.Broadcast(
        SpecialTorpedoCharacteristics ? SpecialTorpedoCharacteristics->TorpedoType : ETorpedoType::Heavy,
        Torpedo);

    if (bDebugCooldownLogs) {
        UE_LOG(LogTemp, Log, TEXT("[TorpedoComp] Special fired. Remaining: %d/%d"),
            CurrentSpecialTorpedoes, SpecialTorpedoCapacity);
    }

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
        if (bDebugMainMessages) {
            UE_LOG(LogTemp, Error, TEXT("[TorpedoComp] SpawnActorDeferred returned nullptr!"));
        }
        return nullptr;
    }

    // -- Hand over the DA before BeginPlay runs ----------------------------
    Torpedo->SetCharacteristics(TorpedoDA);

    // -- Associate the owner so the torpedo ignores it
    //  The torpedo checks if it's its owner
    Torpedo->FiringShooter = OwnerSub;

    // -- Initial velocity --------------------------------------------------
    // We use ONLY the forward axis speed scalar, not the full 3D velocity.
    // This avoids the diagonal bleed when the submarine is pitched/yawed.
    //
    // CurrentLinearSpeed is the signed scalar along the submarine's forward axis:
    //   > 0 = moving forward
    //   < 0 = moving backward (torpedo will initially travel backward,
    //          then its engine accelerates it forward naturally)
    //
    // The torpedo always FACES forward (SpawnRotation = OwnerSub->GetActorRotation()),
    // so negative initial speed means it briefly flies tail-first before the
    // engine wins — exactly the behaviour you asked for.

    const float SpeedOffset = TorpedoDA ? TorpedoDA->InitialSpeedOffset : 1500.f;

    // Pure forward-axis scalar contribution from submarine movement
    const float SubLinearContribution = OwnerSub->CurrentLinearSpeed;

    // Total initial speed along the torpedo's (= submarine's) forward vector
    const float TotalInitialSpeed = SubLinearContribution + SpeedOffset;

    // Apply purely along forward — no lateral/vertical bleed
    const FVector InitialVelocity = OwnerSub->GetActorForwardVector() * TotalInitialSpeed;

    Torpedo->SetInitialVelocity(InitialVelocity);

    // -- Finish deferred spawn ---------------------------------------------

    // Set FiringSubmarine BEFORE FinishSpawning so BeginPlay sees it
    Torpedo->FiringShooter = OwnerSub;

    // Set ignore before FinishSpawning triggers BeginPlay and the first tick
    if (UStaticMeshComponent* TBody = Torpedo->GetTorpedoBody())
        TBody->IgnoreActorWhenMoving(OwnerSub, true);

    // Owner ignores the torpedo for movement sweeps
    TArray<UPrimitiveComponent*> OwnerPrims;
    OwnerSub->GetComponents<UPrimitiveComponent>(OwnerPrims);
    for (UPrimitiveComponent* Prim : OwnerPrims)
        Prim->IgnoreActorWhenMoving(Torpedo, true);

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