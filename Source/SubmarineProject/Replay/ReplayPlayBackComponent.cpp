// Fill out your copyright notice in the Description page of Project Settings.

#include "ReplayPlaybackComponent.h"
#include "ReplaySettings.h"
#include "ReplayHelpers.h"
#include "ReplayGhostComponent.h"
#include "SubmarinePawn.h"
#include "TorpedoPawn.h"
#include "TorpedoCharacteristics.h"
#include "SubmarineTorpedoComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

UReplayPlaybackComponent::UReplayPlaybackComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

// ---------------------------------------------------------------------------
//  BeginPlayback
// ---------------------------------------------------------------------------
void UReplayPlaybackComponent::BeginPlayback(UReplayData* Slice,
    AController* DeadController,
    float        StartTime,
    float        InPlaybackSpeed)
{
    const UReplaySettings* S = GetSettings();
    if (!S) return;

    if (bPlaying)
    {
        if (S->bLogReplayPlayback) {
            UE_LOG(LogTemp, Warning, TEXT("[ReplayPlayback] Already playing — stopping first"));
        }
        StopPlayback();
    }

    if (!Slice || Slice->TickFrames.Num() == 0)
    {
        if (S->bLogReplayPlayback) {
            UE_LOG(LogTemp, Warning, TEXT("[ReplayPlayback] Empty slice — aborting"));
        }
        return;
    }

    ActiveSlice = Slice;
    PlaybackSpeed = FMath::Max(InPlaybackSpeed, 0.1f);
    CachedDeadController = DeadController;
    PlaybackTime = (StartTime >= 0.f) ? StartTime : Slice->RecordStartTime;
    LastVFXCheckTime = PlaybackTime;

    if (S->bLogReplayPlayback) {
        UE_LOG(LogTemp, Log,
            TEXT("[ReplayPlayback] Starting: t=%.2f -> %.2f  speed=%.1f  frames=%d  VFXEvents=%d"),
            PlaybackTime, Slice->RecordEndTime, PlaybackSpeed,
            Slice->TickFrames.Num(), Slice->VFXEvents.Num());
        UE_LOG(LogTemp, Log,
            TEXT("[ReplayPlayback] KillerTorpedoGuid=%s KillerGuid=%s DeadSubGuid=%s"),
            *KillerTorpedoGuid.ToString(), *KillerActorGuid.ToString(), *DeadSubGuid.ToString());
    }

    SpawnGhosts();
    HideRealActorsForDeadPlayer();

    // Register dead player so SpawnGameplayVFXAtLocation hides live VFX from them
    if (APlayerController* DeadPC = Cast<APlayerController>(CachedDeadController.Get()))
        UReplayHelpers::RegisterDeadPlayer(DeadPC);

    bPlaying = true;
    PrimaryComponentTick.SetTickFunctionEnable(true);
    OnPlaybackStarted.Broadcast();
}

// ---------------------------------------------------------------------------
//  StopPlayback
// ---------------------------------------------------------------------------
void UReplayPlaybackComponent::StopPlayback()
{
    const UReplaySettings* S = GetSettings();
    if (!S) return;
    if (S->bLogReplayPlayback) {
        // Log the call stack reason — was it a natural end or an external call ?
        UE_LOG(LogTemp, Warning,
            TEXT("[ReplayPlayback] StopPlayback CALLED — bPlaying=%d PlaybackTime=%.2f EndTime=%.2f"),
            bPlaying ? 1 : 0,
            PlaybackTime,
            ActiveSlice ? ActiveSlice->RecordEndTime : -1.f);
    }

    if (!bPlaying) return;

    bPlaying = false;
    PrimaryComponentTick.SetTickFunctionEnable(false);

    // Unregister dead player before clearing CachedDeadController
    if (APlayerController* DeadPC = Cast<APlayerController>(CachedDeadController.Get()))
        UReplayHelpers::UnregisterDeadPlayer(DeadPC);

    RestoreRealActors();
    DestroyGhosts();

    ActiveSlice = nullptr;
    PlaybackTime = 0.f;
    LastVFXCheckTime = 0.f;
    CachedDeadController = nullptr;

    if (S->bLogReplayPlayback) {
        UE_LOG(LogTemp, Log, TEXT("[ReplayPlayback] Stopped"));
    }
    OnPlaybackFinished.Broadcast();
}

// ---------------------------------------------------------------------------
//  TickComponent
// ---------------------------------------------------------------------------
void UReplayPlaybackComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bPlaying || !ActiveSlice) return;

    PlaybackTime += DeltaTime * PlaybackSpeed;

    const float EndTime = ActiveSlice->RecordEndTime;

    const UReplaySettings* S = GetSettings();
    if (!S) return;
    if (S->bLogReplayPlayback) {
        UE_LOG(LogTemp, Log,
            TEXT("[ReplayPlayback] TICK t=%.2f / end=%.2f  remaining=%.2fs  speed=%.1f"),
            PlaybackTime, EndTime, (EndTime - PlaybackTime) / PlaybackSpeed, PlaybackSpeed);
    }

    TickGhostPositions();
    TickVFXEvents();

    if (PlaybackTime >= EndTime)
    {
        if (S->bLogReplayPlayback) {
            UE_LOG(LogTemp, Log,
                TEXT("[ReplayPlayback] Reached end (t=%.2f >= end=%.2f)  stopping naturally"),
                PlaybackTime, EndTime);
        }
        StopPlayback();
    }
}

// ---------------------------------------------------------------------------
//  GetPlaybackProgress
// ---------------------------------------------------------------------------
float UReplayPlaybackComponent::GetPlaybackProgress() const
{
    if (!ActiveSlice) return 0.f;
    const float Duration = ActiveSlice->GetDuration();
    return Duration > 0.f
        ? FMath::Clamp((PlaybackTime - ActiveSlice->RecordStartTime) / Duration, 0.f, 1.f)
        : 0.f;
}

// ---------------------------------------------------------------------------
//  ResolveNameToGuid
//
//  Reverse lookup: given a display name, find the GUID in the slice registry.
//  If multiple GUIDs share the same name (unlikely but possible with recycled
//  actor names), returns the first match.  This path is only used for backward
//  compatibility with the legacy string-based API.
// ---------------------------------------------------------------------------
FGuid UReplayPlaybackComponent::ResolveNameToGuid(const FString& DisplayName) const
{
    if (!ActiveSlice || DisplayName.IsEmpty()) return FGuid();

    for (const auto& Pair : ActiveSlice->GuidToDisplayName)
        if (Pair.Value == DisplayName) return Pair.Key;

    const UReplaySettings* S = GetSettings();
    if (!S) return FGuid();
    if (S->bLogReplayPlayback) {
        UE_LOG(LogTemp, Warning,
            TEXT("[ReplayPlayback] ResolveNameToGuid: '%s' not found in registry"), *DisplayName);
    }
    return FGuid();
}

// ---------------------------------------------------------------------------
//  Ghost access
// ---------------------------------------------------------------------------
AActor* UReplayPlaybackComponent::GetGhostForGuid(const FGuid& Guid) const
{
    if (!Guid.IsValid()) return nullptr;
    for (const FGhostActorEntry& E : GhostEntries)
        if (E.ActorGuid == Guid && IsValid(E.GhostActor))
            return E.GhostActor;
    return nullptr;
}

// ---------------------------------------------------------------------------
//  SpawnGhosts
// ---------------------------------------------------------------------------
void UReplayPlaybackComponent::SpawnGhosts()
{
    UWorld* World = GetWorld();
    if (!World || !ActiveSlice) return;

    // Collect unique GUIDs from all tick frames
    TSet<FGuid> UniqueGuids;
    for (const FReplayTickEntry& Frame : ActiveSlice->TickFrames)
        for (const FGuid& G : Frame.ActorGuids)
            UniqueGuids.Add(G);

    const UReplaySettings* S = GetSettings();
    if (!S) return;
    if (S->bLogReplayPlayback) {
        UE_LOG(LogTemp, Log, TEXT("[ReplayPlayback] Spawning %d ghosts"), UniqueGuids.Num());
    }

    // Build GUID -> live actor map
    TMap<FGuid, AActor*> LiveMap;
    for (TActorIterator<ASubmarinePawn> It(World); It; ++It)
        if (IsValid(*It)) LiveMap.Add((*It)->GetActorInstanceGuid(), *It);
    for (TActorIterator<ATorpedoPawn> It(World); It; ++It)
        if (IsValid(*It)) LiveMap.Add((*It)->GetActorInstanceGuid(), *It);

    // Find torpedo Blueprint class for temp-spawn mesh cloning
    TSubclassOf<ATorpedoPawn> TorpedoBlueprintClass = nullptr;
    UTorpedoCharacteristics* TorpedoDefaultDA = nullptr;

    for (TActorIterator<ASubmarinePawn> It(World); It; ++It)
    {
        if (!IsValid(*It)) continue;
        if (USubmarineTorpedoComponent* TC =
            (*It)->FindComponentByClass<USubmarineTorpedoComponent>())
        {
            if (TC->NormalTorpedoBlueprintClass)
            {
                TorpedoBlueprintClass = TC->NormalTorpedoBlueprintClass;
                TorpedoDefaultDA = TC->NormalTorpedoCharacteristics;
                if (S->bLogReplayPlayback) {
                    UE_LOG(LogTemp, Log,
                        TEXT("[ReplayPlayback] Torpedo BP class '%s' DA='%s'"),
                        *TorpedoBlueprintClass->GetName(),
                        TorpedoDefaultDA ? *TorpedoDefaultDA->GetName() : TEXT("null"));
                }
                break;
            }
        }
    }

    for (const FGuid& Guid : UniqueGuids)
    {
        FGhostActorEntry Entry;
        Entry.ActorGuid = Guid;
        Entry.DisplayName = ActiveSlice->GetDisplayName(Guid);
        Entry.bIsTorpedo = Entry.DisplayName.Contains(TEXT("Torpedo"), ESearchCase::IgnoreCase);

        ComputeActorLifetimeBounds(Guid,
            Entry.FirstSeenTime, Entry.LastSeenTime,
            Entry.FirstKnownLocation, Entry.FirstKnownRotation,
            Entry.LastKnownLocation, Entry.LastKnownRotation);

        if (S->bLogReplayPlayback) {
            UE_LOG(LogTemp, Log,
                TEXT("[ReplayPlayback] Ghost '%s': torpedo=%d firstSeen=%.2f lastSeen=%.2f firstLoc=%s lastLoc=%s"),
                *Entry.DisplayName, Entry.bIsTorpedo ? 1 : 0,
                Entry.FirstSeenTime, Entry.LastSeenTime,
                *Entry.FirstKnownLocation.ToString(), *Entry.LastKnownLocation.ToString());
        }

        const FVector  SpawnLoc = (Entry.FirstSeenTime >= 0.f)
            ? Entry.FirstKnownLocation : FVector::ZeroVector;
        const FRotator SpawnRot = (Entry.FirstSeenTime >= 0.f)
            ? Entry.FirstKnownRotation : FRotator::ZeroRotator;

        AActor** LivePtr = LiveMap.Find(Guid);
        AActor* LiveActor = (LivePtr && IsValid(*LivePtr)) ? *LivePtr : nullptr;

        // If this is a torpedo ghost and the live actor is already destroyed,
        // spawn a temporary instance of the torpedo Blueprint just for mesh cloning,
        // then destroy it immediately. This avoids any dependency on live actors.
        ATorpedoPawn* TempTorpedo = nullptr;
        if (!LiveActor && Entry.bIsTorpedo && TorpedoBlueprintClass)
        {
            FActorSpawnParameters TempParams;
            TempParams.SpawnCollisionHandlingOverride =
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            // Spawn far off-world so it never interacts with gameplay
            TempTorpedo = World->SpawnActor<ATorpedoPawn>(
                TorpedoBlueprintClass,
                FVector(0.f, 0.f, -999999.f), FRotator::ZeroRotator,
                TempParams);
            if (TempTorpedo)
            {
                LiveActor = TempTorpedo;
                // Inject DA so components read correctly
                if (!TempTorpedo->Characteristics && TorpedoDefaultDA)
                    TempTorpedo->SetCharacteristics(TorpedoDefaultDA);

                if (S->bLogReplayPlayback) {
                    UE_LOG(LogTemp, Log,
                        TEXT("[ReplayPlayback] Temp torpedo '%s' for ghost of '%s' | DA=%s"),
                        *TempTorpedo->GetName(), *Entry.DisplayName,
                        TempTorpedo->Characteristics ? *TempTorpedo->Characteristics->GetName() : TEXT("null"));
                }
            }
        }

        if (!LiveActor)
        {
            if (S->bLogReplayPlayback) {
                UE_LOG(LogTemp, Warning,
                    TEXT("[ReplayPlayback] No live actor for ghost '%s' -- skipping"), *Entry.DisplayName);
            }
            continue;
        }

        Entry.GhostActor = SpawnGhostForActor(LiveActor, SpawnLoc, SpawnRot);

        if (TempTorpedo)
            TempTorpedo->Destroy();

        if (!Entry.GhostActor) continue;

        GhostEntries.Add(Entry);
    }
}

// ---------------------------------------------------------------------------
//  SpawnGhostForActor
// ---------------------------------------------------------------------------
AActor* UReplayPlaybackComponent::SpawnGhostForActor(AActor* RealActor,
    const FVector& SpawnLoc, const FRotator& SpawnRot)
{
    UWorld* World = GetWorld();
    if (!World) return nullptr;

    FActorSpawnParameters P;
    P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* Ghost = World->SpawnActor<AActor>(
        AActor::StaticClass(), SpawnLoc, SpawnRot, P);
    if (!Ghost) return nullptr;

    //Ghost->SetActorTickEnabled(false);
    Ghost->Tags.Add(UReplayHelpers::Tag_ReplayGhost);

    // Hide ghost from all LIVE players (non-dead PlayerControllers).
    // The dead player's PC is stored in CachedDeadController -- all others are live.
    // This is a no-op in single-player but correct for multiplayer.
    {
        APlayerController* DeadPC = Cast<APlayerController>(CachedDeadController.Get());
        for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
        {
            APlayerController* LivePC = Cast<APlayerController>(It->Get());
            if (LivePC && LivePC != DeadPC)
                LivePC->HiddenActors.Add(Ghost);
        }
    }

    if (!RealActor) return Ghost; // no mesh to copy — positional anchor only

    // Prefer UReplayGhostComponent for future-proof cloning
    UReplayGhostComponent* GhostComp =
        RealActor->FindComponentByClass<UReplayGhostComponent>();

    if (GhostComp)
    {
        GhostComp->CloneComponentsOntoGhost(Ghost);
        GhostComp->Settings = Settings;
    }
    else
    {
        // Fallback: copy StaticMeshComponents directly
        const UReplaySettings* S = GetSettings();
        if (!S) return nullptr;
        if (S->bLogReplayPlayback) {
            UE_LOG(LogTemp, Verbose,
                TEXT("[ReplayPlayback] No ReplayGhostComponent on '%s' — using fallback mesh copy"),
                *RealActor->GetName());
        }
        FallbackCopyStaticMeshes(RealActor, Ghost);
    }

    return Ghost;
}

// ---------------------------------------------------------------------------
//  FallbackCopyStaticMeshes
//  Used when the actor has no UReplayGhostComponent.
//  Copies StaticMeshComponents only (no Niagara).
// ---------------------------------------------------------------------------
void UReplayPlaybackComponent::FallbackCopyStaticMeshes(AActor* Source, AActor* Target)
{
    if (!Source || !Target) return;

    TArray<UStaticMeshComponent*> Meshes;
    Source->GetComponents<UStaticMeshComponent>(Meshes);

    USceneComponent* Root = Target->GetRootComponent();
    const FTransform SrcTransform = Source->GetActorTransform();

    for (UStaticMeshComponent* Src : Meshes)
    {
        if (!Src || !Src->GetStaticMesh()) continue;

        // Skip camera proxy meshes
        if (Src->GetName().StartsWith(TEXT("CameraProxyMeshComponent"))) continue;
        if (Src->GetStaticMesh()->GetName() == TEXT("MatineeCam_SM")) continue;

        UStaticMeshComponent* Dst = NewObject<UStaticMeshComponent>(Target);
        if (!Dst) continue;

        if (!Root)
        {
            Dst->RegisterComponent();
            Target->SetRootComponent(Dst);
            Root = Dst;
            Dst->SetRelativeTransform(FTransform::Identity);
        }
        else
        {
            Dst->SetupAttachment(Root);
            Dst->RegisterComponent();
            const FTransform Rel = Src->GetComponentTransform().GetRelativeTransform(SrcTransform);
            Dst->SetRelativeTransform(Rel);
        }

        Dst->SetStaticMesh(Src->GetStaticMesh());
        for (int32 i = 0; i < Src->GetNumMaterials(); ++i)
            if (UMaterialInterface* Mat = Src->GetMaterial(i))
                Dst->SetMaterial(i, Mat);

        Dst->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Dst->SetCollisionProfileName(TEXT("NoCollision"));
        Dst->SetSimulatePhysics(false);
        Dst->SetCastShadow(false);
        Dst->SetGenerateOverlapEvents(false);
        Dst->SetHiddenInGame(false);
        Dst->SetVisibility(true);
        Target->AddInstanceComponent(Dst);
    }
}

// ---------------------------------------------------------------------------
//  DestroyGhosts
// ---------------------------------------------------------------------------
void UReplayPlaybackComponent::DestroyGhosts()
{
    for (FGhostActorEntry& E : GhostEntries)
        if (IsValid(E.GhostActor))
            E.GhostActor->Destroy();

    GhostEntries.Empty();
    const UReplaySettings* S = GetSettings();
    if (!S) return;
    if (S->bLogReplayPlayback) {
        UE_LOG(LogTemp, Log, TEXT("[ReplayPlayback] Ghosts destroyed"));
    }
}

// ---------------------------------------------------------------------------
//  HideRealActorsForDeadPlayer
//
//  Uses APlayerController::HiddenActors — a per-player TSet built into UE.
//  Actors added here are skipped during rendering for that one PC only.
//  Other players see everything normally.
//
//  Categories hidden:
//    1. All ASubmarinePawn actors
//    2. All ATorpedoPawn actors
//    3. Any AActor tagged "ReplayDynamic" (standalone Niagara explosion actors
//       spawned via UReplayHelpers::SpawnReplayTaggedNiagaraAtLocation)
//
//  Ghost actors (tagged "ReplayGhost") are explicitly excluded.
// ---------------------------------------------------------------------------
void UReplayPlaybackComponent::HideRealActorsForDeadPlayer()
{
    const UReplaySettings* S = GetSettings();
    if (!S) return;

    APlayerController* PC = Cast<APlayerController>(CachedDeadController.Get());
    if (!PC && S->bLogReplayPlayback)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ReplayPlayback] HideRealActors — no PlayerController, skipping"));
        return;
    }

    UWorld* World = GetWorld();
    if (!World) return;

    HiddenActors.Empty();
    int32 Count = 0;

    auto Hide = [&](AActor* A)
        {
            if (!A || !IsValid(A)) return;
            if (A->ActorHasTag(UReplayHelpers::Tag_ReplayGhost))
            {
                if (S->bLogReplayPlayback) {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[ReplayPlayback] SKIP ghost '%s' from hiding"), *A->GetName());
                }
                return;
            }
            PC->HiddenActors.Add(A);
            HiddenActors.Add(A);
            ++Count;
            if (S->bLogReplayPlayback) {
                UE_LOG(LogTemp, Warning,
                    TEXT("[ReplayPlayback] Hiding '%s' from dead player"), *A->GetName());
            }
        };

    for (TActorIterator<ASubmarinePawn> It(World); It; ++It) Hide(*It);
    for (TActorIterator<ATorpedoPawn> It(World); It; ++It) Hide(*It);

    if (S->bLogReplayPlayback) {
        UE_LOG(LogTemp, Log,
            TEXT("[ReplayPlayback] Hidden %d real dynamic actors from dead player"), Count);
    }
}

// ---------------------------------------------------------------------------
//  RestoreRealActors
// ---------------------------------------------------------------------------
void UReplayPlaybackComponent::RestoreRealActors()
{
    if (APlayerController* PC = Cast<APlayerController>(CachedDeadController.Get()))
        PC->HiddenActors.Empty();

    HiddenActors.Empty();
    const UReplaySettings* S = GetSettings();
    if (!S) return;
    if (S->bLogReplayPlayback) {
        UE_LOG(LogTemp, Log, TEXT("[ReplayPlayback] Real actor visibility restored"));
    }
}

// ---------------------------------------------------------------------------
//  TickGhostPositions
// ---------------------------------------------------------------------------
void UReplayPlaybackComponent::TickGhostPositions()
{
    const UReplaySettings* S = GetSettings();
    if (!S) return;
    // Auto-hide any new torpedoes spawned by live submarines during replay
    if (APlayerController* PC = Cast<APlayerController>(CachedDeadController.Get()))
    {
        for (TActorIterator<ATorpedoPawn> It(GetWorld()); It; ++It)
        {
            ATorpedoPawn* T = *It;
            if (!IsValid(T)) continue;
            if (T->ActorHasTag(UReplayHelpers::Tag_ReplayGhost)) continue;
            if (!PC->HiddenActors.Contains(T))
            {
                PC->HiddenActors.Add(T);
                HiddenActors.Add(T);
                if (S->bLogReplayPlayback) {
                    UE_LOG(LogTemp, Log,
                        TEXT("[ReplayPlayback] Auto-hiding new torpedo '%s'"), *T->GetName());
                }
            }
        }
    }

    // Position each ghost
    for (FGhostActorEntry& E : GhostEntries)
    {
        if (!IsValid(E.GhostActor)) continue;

        const bool bBeforeLife = (E.FirstSeenTime >= 0.f && PlaybackTime < E.FirstSeenTime);
        const bool bAfterLife = (E.LastSeenTime >= 0.f && PlaybackTime > E.LastSeenTime);

        if (bBeforeLife)
        {
            E.GhostActor->SetActorLocationAndRotation(
                E.FirstKnownLocation, E.FirstKnownRotation);

            // Hide torpedo ghosts before they existed in the recording
            if (E.bIsTorpedo && !E.GhostActor->IsHidden())
                E.GhostActor->SetActorHiddenInGame(true);
            continue;
        }

        if (bAfterLife)
        {
            E.GhostActor->SetActorLocationAndRotation(E.LastKnownLocation, E.LastKnownRotation);
            if (!E.GhostActor->IsHidden())
            {
                E.GhostActor->SetActorHiddenInGame(true);
                if (S->bLogReplayPlayback) {
                    UE_LOG(LogTemp, Log,
                        TEXT("[ReplayPlayback] Ghost '%s' HIDDEN at t=%.2f (lastSeen=%.2f) loc=%s"),
                        *E.DisplayName, PlaybackTime, E.LastSeenTime, *E.LastKnownLocation.ToString());
                }
            }
            continue;
        }

        // Active -- interpolate position
        FVector  Loc;
        FRotator Rot;
        if (InterpolateActorTransform(E.ActorGuid, E.LastSeenTime, Loc, Rot))
        {
            E.GhostActor->SetActorLocationAndRotation(Loc, Rot);
            E.LastKnownLocation = Loc;
            E.LastKnownRotation = Rot;

            // Make visible when entering active window
            if (E.GhostActor->IsHidden())
            {
                E.GhostActor->SetActorHiddenInGame(false);
                if (S->bLogReplayPlayback) {
                    UE_LOG(LogTemp, Log,
                        TEXT("[ReplayPlayback] Ghost '%s' VISIBLE at t=%.2f (firstSeen=%.2f)"),
                        *E.DisplayName, PlaybackTime, E.FirstSeenTime);
                }
            }
        }
        else
        {
            // No frame at this time — hold last known
            E.GhostActor->SetActorLocationAndRotation(E.LastKnownLocation, E.LastKnownRotation);
        }
    }
}

// ---------------------------------------------------------------------------
//  TickVFXEvents
//
//  Checks for VFX events whose timestamp falls in [LastVFXCheckTime, PlaybackTime].
//  Spawns them tagged ReplayDynamic so only the dead player's camera sees them.
// ---------------------------------------------------------------------------
void UReplayPlaybackComponent::TickVFXEvents()
{
    if (!ActiveSlice) return;

    const UReplaySettings* S = GetSettings();
    if (!S) return;

    for (const FReplayVFXEvent& Event : ActiveSlice->VFXEvents)
    {
        // Fire events that fall in the window we just advanced through
        if (Event.Timestamp > LastVFXCheckTime && Event.Timestamp <= PlaybackTime)
        {
            if (S->bLogReplayPlayback) {
                UE_LOG(LogTemp, Log,
                    TEXT("[ReplayPlayback] VFX attempt: t=%.2f  loc=%s  asset=%s"),
                    Event.Timestamp, *Event.Location.ToString(),
                    Event.NiagaraAsset.IsValid() ? TEXT("soft-ref valid") : TEXT("soft-ref NULL"));
            }

            UNiagaraSystem* Asset = Event.NiagaraAsset.LoadSynchronous();
            if (!Asset)
            {
                if (S->bLogReplayPlayback) {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[ReplayPlayback] VFX event at t=%.2f has invalid asset"), Event.Timestamp);
                }
                continue;
            }
            if (S->bLogReplayPlayback) {
                UE_LOG(LogTemp, Log,
                    TEXT("[ReplayPlayback] VFX asset loaded OK: '%s'  spawning..."),
                    *Asset->GetName());
            }

            // SpawnReplayTaggedNiagaraAtLocation tags the spawned actor ReplayDynamic.
            // HideRealActorsForDeadPlayer hides ReplayDynamic actors from live players.
            // The dead player IS the one watching the replay, so they see these VFX.
            APlayerController* DeadPC = Cast<APlayerController>(CachedDeadController.Get());

            // SpawnReplayVFXAtLocation creates a dedicated holder actor so we
            // control ownership. This avoids the WorldSettings problem and gives
            // us proper per-player visibility: DeadPC sees it, live players do not.
            UNiagaraComponent* NC = UReplayHelpers::SpawnReplayVFXAtLocation(
                GetWorld(),
                Asset,
                Event.Location,
                Event.Rotation,
                Event.Scale,
                DeadPC,
                /*bAutoDestroy=*/true,
                /*bAutoActivate=*/true,
                /*PoolingMethod=*/ENCPoolMethod::None);

            if (S->bLogReplayPlayback)
            {
                if (!NC)
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[ReplayPlayback] VFX SPAWN FAILED: SpawnReplayVFXAtLocation returned NULL  asset='%s'  t=%.2f"),
                        *Asset->GetName(), Event.Timestamp);
                }
                else
                {
                    AActor* Holder = NC->GetOwner();
                    const bool bDeadCanSee = !DeadPC || !Holder
                        || !DeadPC->HiddenActors.Contains(Holder);
                    UE_LOG(LogTemp, Log,
                        TEXT("[ReplayPlayback] VFX spawned OK  holder='%s'  deadCanSee=%d  loc=%s  t=%.2f"),
                        Holder ? *Holder->GetName() : TEXT("NULL"),
                        bDeadCanSee ? 1 : 0,
                        *Event.Location.ToString(), Event.Timestamp);
                }
            }
        }
    }

    LastVFXCheckTime = PlaybackTime;
}

// ---------------------------------------------------------------------------
//  ComputeActorLifetimeBounds
//
//  Scans all tick frames for the given GUID.
//  First frame containing it  -> OutFirstTime / OutFirstLoc / OutFirstRot
//  Last frame containing it   -> OutLastTime  / OutLastLoc  / OutLastRot
//
//  That's it. No special cases, no bSawDeath, no bWasPresent, no name checks.
//  Two actors with the same display name are different GUIDs and thus
//  completely separate ghost entries -- no contamination possible.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
void UReplayPlaybackComponent::ComputeActorLifetimeBounds(const FGuid& ActorGuid,
    float& OutFirstTime, float& OutLastTime,
    FVector& OutFirstLoc, FRotator& OutFirstRot,
    FVector& OutLastLoc, FRotator& OutLastRot) const
{
    OutFirstTime = -1.f;
    OutLastTime = -1.f;
    OutFirstLoc = FVector::ZeroVector;
    OutFirstRot = FRotator::ZeroRotator;
    OutLastLoc = FVector::ZeroVector;
    OutLastRot = FRotator::ZeroRotator;

    if (!ActiveSlice) return;

    // For submarines: stop tracking once bAlive=false is recorded.
    // Torpedoes never record bAlive=false -- they just vanish from the recording.
    bool bSubmarineDied = false;

    for (const FReplayTickEntry& Frame : ActiveSlice->TickFrames)
    {
        for (int32 i = 0; i < Frame.ActorGuids.Num(); ++i)
        {
            if (Frame.ActorGuids[i] != ActorGuid) continue;
            if (!Frame.ActorFrames.IsValidIndex(i)) break;

            const FReplayActorFrame& AF = Frame.ActorFrames[i];

            if (OutFirstTime < 0.f)
            {
                OutFirstTime = Frame.Timestamp;
                OutFirstLoc = AF.Location;
                OutFirstRot = AF.Rotation;
            }

            if (AF.bAlive)
            {
                // Actor is alive -- update last seen time
                OutLastTime = Frame.Timestamp;
                OutLastLoc = AF.Location;
                OutLastRot = AF.Rotation;
            }
            else
            {
                // bAlive=false: only submarines record this (torpedoes just vanish).
                // Record this exact frame as LastSeen and stop scanning.
                OutLastTime = Frame.Timestamp;
                OutLastLoc = AF.Location;
                OutLastRot = AF.Rotation;
                bSubmarineDied = true;
            }

            break; // found in this frame, done searching
        }
        
        if (bSubmarineDied) break;
    }

    // If actor was present throughout the entire slice (submarine that never died),
    // its LastSeenTime is the last frame, which is correct. No adjustment needed.
}


// ---------------------------------------------------------------------------
//  InterpolateActorTransform
// ---------------------------------------------------------------------------
bool UReplayPlaybackComponent::InterpolateActorTransform(const FGuid& ActorGuid,
    float ClampBeforeTime, FVector& OutLocation, FRotator& OutRotation) const
{
    if (!ActiveSlice) return false;

    const TArray<FReplayTickEntry>& Frames = ActiveSlice->TickFrames;
    if (Frames.Num() == 0) return false;

    int32 PrevIdx = INDEX_NONE;
    int32 NextIdx = INDEX_NONE;

    for (int32 i = 0; i < Frames.Num(); ++i)
    {
        if (Frames[i].Timestamp <= PlaybackTime)
            PrevIdx = i;
        else
        {
            NextIdx = i;
            break;
        }
    }

    // Safety: don't lerp into frames past the actor's recorded lifetime
    if (ClampBeforeTime >= 0.f && NextIdx != INDEX_NONE
        && Frames[NextIdx].Timestamp > ClampBeforeTime)
    {
        NextIdx = INDEX_NONE;
    }

    auto FindIdx = [&](int32 FIdx) -> int32
        {
            if (FIdx == INDEX_NONE) return INDEX_NONE;
            const TArray<FGuid>& Guids = Frames[FIdx].ActorGuids;
            for (int32 j = 0; j < Guids.Num(); ++j)
                if (Guids[j] == ActorGuid) return j;
            return INDEX_NONE;
        };

    const int32 PA = FindIdx(PrevIdx);
    const int32 NA = FindIdx(NextIdx);

    if (PA == INDEX_NONE && NA == INDEX_NONE) return false;

    if (PA == INDEX_NONE)
    {
        OutLocation = Frames[NextIdx].ActorFrames[NA].Location;
        OutRotation = Frames[NextIdx].ActorFrames[NA].Rotation;
        return true;
    }

    if (NA == INDEX_NONE)
    {
        OutLocation = Frames[PrevIdx].ActorFrames[PA].Location;
        OutRotation = Frames[PrevIdx].ActorFrames[PA].Rotation;
        return true;
    }

    const float PrevT = Frames[PrevIdx].Timestamp;
    const float NextT = Frames[NextIdx].Timestamp;
    const float Range = NextT - PrevT;
    const float Alpha = (Range > 0.f)
        ? FMath::Clamp((PlaybackTime - PrevT) / Range, 0.f, 1.f)
        : 0.f;

    const FReplayActorFrame& PF = Frames[PrevIdx].ActorFrames[PA];
    const FReplayActorFrame& NF = Frames[NextIdx].ActorFrames[NA];

    OutLocation = FMath::Lerp(PF.Location, NF.Location, Alpha);
    OutRotation = FMath::Lerp(PF.Rotation, NF.Rotation, Alpha);
    return true;
}

// ---------------------------------------------------------------------------
//  GetSettings
// ---------------------------------------------------------------------------
const UReplaySettings* UReplayPlaybackComponent::GetSettings() const
{
    if (Settings) return Settings;
    return GetDefault<UReplaySettings>();
}