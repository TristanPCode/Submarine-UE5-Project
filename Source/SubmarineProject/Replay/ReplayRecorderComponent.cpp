// Fill out your copyright notice in the Description page of Project Settings.

#include "ReplayRecorderComponent.h"
#include "ReplaySettings.h"
#include "ReplayData.h"
#include "SubmarinePawn.h"
#include "SubmarineCharacteristics.h"
#include "TorpedoPawn.h"
#include "TorpedoCharacteristics.h"
#include "SubmarineCollisionComponent.h"
#include "SubmarineTorpedoComponent.h"
#include "NiagaraSystem.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/World.h"

UReplayRecorderComponent::UReplayRecorderComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UReplayRecorderComponent::BeginPlay()
{
    Super::BeginPlay();

    // Create the live replay buffer
    LiveReplay = NewObject<UReplayData>(this);
    LiveReplay->RecordStartTime = GetWorld()->GetTimeSeconds();
    LiveReplay->RecordEndTime = LiveReplay->RecordStartTime;

    // Auto-start recording if configured
    if (GetSettings() && GetSettings()->bAutoRecord)
        StartRecording();
}

// ---------------------------------------------------------------------------
//  Tick
// ---------------------------------------------------------------------------
void UReplayRecorderComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bRecording) return;

    const UReplaySettings* S = GetSettings();
    if (!S) return;

    const float Now = GetWorld()->GetTimeSeconds();
    LiveReplay->RecordEndTime = Now;

    TimeSinceLastTickFrame += DeltaTime;
    TimeSinceLastKeyframe += DeltaTime;

    // Refresh tracked actor list every tick (torpedoes spawn/die frequently)
    RefreshTrackedActors();

    // Tick frame
    const float TickInterval = S->GetTickFrameInterval();
    if (TimeSinceLastTickFrame >= TickInterval)
    {
        TimeSinceLastTickFrame -= TickInterval;
        RecordTickFrame(Now);
    }

    // Keyframe
    if (TimeSinceLastKeyframe >= S->FullSnapshotInterval)
    {
        TimeSinceLastKeyframe -= S->FullSnapshotInterval;
        RecordKeyframe(Now);
    }

    // Rolling trim — drop frames older than MaxRecordDuration
    if (S->MaxRecordDuration > 0.f)
    {
        const float Cutoff = Now - S->MaxRecordDuration;
        if (Cutoff > LiveReplay->RecordStartTime)
        {
            LiveReplay->TrimBefore(Cutoff);
            LiveReplay->RecordStartTime = Cutoff;
        }
    }
}

// ---------------------------------------------------------------------------
//  Recording control
// ---------------------------------------------------------------------------
void UReplayRecorderComponent::StartRecording()
{
    if (bRecording) return;

    bRecording = true;
    TimeSinceLastTickFrame = 0.f;
    TimeSinceLastKeyframe = 0.f;

    PreviousFrameGuids.Empty();
    LastSeenLocation.Empty();
    CachedTorpedoVFX.Empty();
    CachedTorpedoVFXScale.Empty();
    DeadSubmarineGuids.Empty();

    // Reset buffer
    if (LiveReplay)
    {
        LiveReplay->TickFrames.Reset();
        LiveReplay->Keyframes.Reset();
        LiveReplay->VFXEvents.Reset();
        LiveReplay->GuidToDisplayName.Empty();
        LiveReplay->RecordStartTime = GetWorld()->GetTimeSeconds();
        LiveReplay->RecordEndTime = LiveReplay->RecordStartTime;
    }

    // Force an immediate keyframe so playback always has a starting point
    RecordKeyframe(GetWorld()->GetTimeSeconds());

    OnRecordingStarted.Broadcast();

    const UReplaySettings* S = GetSettings();
    if (!S) return;
    if (S->bLogReplayRecorder) {
        UE_LOG(LogTemp, Log, TEXT("[ReplayRecorder] Recording started"));
    }
}

void UReplayRecorderComponent::StopRecording()
{
    if (!bRecording) return;
    bRecording = false;
    OnRecordingStopped.Broadcast();

    const UReplaySettings* S = GetSettings();
    if (!S) return;
    if (S->bLogReplayRecorder) {
        UE_LOG(LogTemp, Log, TEXT("[ReplayRecorder] Recording stopped. Duration=%.1fs, TickFrames=%d, Keyframes=%d, VFXEvents=%d"),
            LiveReplay ? LiveReplay->GetDuration() : 0.f,
            LiveReplay ? LiveReplay->TickFrames.Num() : 0,
            LiveReplay ? LiveReplay->Keyframes.Num() : 0,
            LiveReplay ? LiveReplay->VFXEvents.Num() : 0);
    }
}

// ---------------------------------------------------------------------------
//  Save / Load
// ---------------------------------------------------------------------------
bool UReplayRecorderComponent::SaveReplay(const FString& Label)
{
    if (!LiveReplay) return false;

    const UReplaySettings* S = GetSettings();
    if (!S) return false;

    LiveReplay->Label = Label.IsEmpty()
        ? FString::Printf(TEXT("Replay_%s"), *FDateTime::Now().ToString())
        : Label;

    const bool bOk = UGameplayStatics::SaveGameToSlot(
        LiveReplay, S->ReplaySaveSlot, S->SaveUserIndex);

    if (S->bLogReplayRecorder) {
        UE_LOG(LogTemp, Log, TEXT("[ReplayRecorder] SaveReplay %s — slot=%s"),
            bOk ? TEXT("OK") : TEXT("FAILED"), *S->ReplaySaveSlot);
    }

    return bOk;
}

bool UReplayRecorderComponent::LoadReplay()
{
    const UReplaySettings* S = GetSettings();
    if (!S) return false;

    LoadedReplay = Cast<UReplayData>(
        UGameplayStatics::LoadGameFromSlot(S->ReplaySaveSlot, S->SaveUserIndex));

    if (!LoadedReplay)
    {
        if (S->bLogReplayRecorder) {
            UE_LOG(LogTemp, Warning, TEXT("[ReplayRecorder] LoadReplay failed — slot=%s"), *S->ReplaySaveSlot);
        }
        return false;
    }

    if (S->bLogReplayRecorder) {
        UE_LOG(LogTemp, Log, TEXT("[ReplayRecorder] LoadReplay OK — duration=%.1fs, TickFrames=%d"),
            LoadedReplay->GetDuration(), LoadedReplay->TickFrames.Num());
    }
    return true;
}

bool UReplayRecorderComponent::SaveFullMatchReplay(const FString& Label)
{
    if (!LiveReplay) return false;
    const UReplaySettings* S = GetSettings();
    if (!S) return false;

    LiveReplay->Label = Label.IsEmpty()
        ? FString::Printf(TEXT("FullMatch_%s"), *FDateTime::Now().ToString())
        : Label;

    LiveReplay->RecordEndTime = GetWorld()->GetTimeSeconds();

    const bool bOk = UGameplayStatics::SaveGameToSlot(
        LiveReplay, S->DeathReplaySaveSlot, S->SaveUserIndex);

    if (S->bLogReplaySave)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[ReplayRecorder] SaveFullMatchReplay %s  slot='%s'  "
                "duration=%.1fs  TickFrames=%d  Keyframes=%d  VFXEvents=%d  MatchEvents=%d"),
            bOk ? TEXT("OK") : TEXT("FAILED"),
            *S->DeathReplaySaveSlot,
            LiveReplay->GetDuration(),
            LiveReplay->TickFrames.Num(),
            LiveReplay->Keyframes.Num(),
            LiveReplay->VFXEvents.Num(),
            LiveReplay->MatchEvents.Num());
    }

    return bOk;
}

// ---------------------------------------------------------------------------
//  Extract slice
// ---------------------------------------------------------------------------
UReplayData* UReplayRecorderComponent::ExtractSlice(float StartTime, float EndTime)
{
    if (!LiveReplay) return nullptr;

    UReplayData* Slice = NewObject<UReplayData>(GetTransientPackage());
    Slice->RecordStartTime = StartTime;
    Slice->RecordEndTime = EndTime;
    Slice->Label = TEXT("DeathReplaySlice");
    Slice->GuidToDisplayName = LiveReplay->GuidToDisplayName;

    // Copy relevant keyframes
    for (const FReplayKeyframe& KF : LiveReplay->Keyframes)
    {
        if (KF.Timestamp >= StartTime && KF.Timestamp <= EndTime)
            Slice->Keyframes.Add(KF);
    }
    // Also include the last keyframe BEFORE StartTime (needed as starting point)
    const int32 LeadKF = LiveReplay->FindKeyframeIndexBefore(StartTime);
    if (LeadKF != INDEX_NONE)
        Slice->Keyframes.Insert(LiveReplay->Keyframes[LeadKF], 0);

    // Copy relevant tick frames
    for (const FReplayTickEntry& TF : LiveReplay->TickFrames)
    {
        if (TF.Timestamp >= StartTime && TF.Timestamp <= EndTime)
            Slice->TickFrames.Add(TF);
    }

    // Copy VFX Event
    for (const FReplayVFXEvent& VFX : LiveReplay->VFXEvents)
        if (VFX.Timestamp >= StartTime && VFX.Timestamp <= EndTime)
            Slice->VFXEvents.Add(VFX);

    const UReplaySettings* S = GetSettings();
    if (!S) return nullptr;
    if (S->bLogReplayRecorder) {
        UE_LOG(LogTemp, Log,
            TEXT("[ReplayRecorder] Slice %.2f-%.2f: %d frames, %d VFX events"),
            StartTime, EndTime, Slice->TickFrames.Num(), Slice->VFXEvents.Num());
    }

    return Slice;
}

// ---------------------------------------------------------------------------
//  Actor tracking
// ---------------------------------------------------------------------------
void UReplayRecorderComponent::RefreshTrackedActors()
{
    TrackedActors.Reset();

    UWorld* World = GetWorld();
    if (!World) return;

    for (TActorIterator<ASubmarinePawn> It(World); It; ++It)
    {
        if (IsValid(*It))
            TrackedActors.Add(*It);
    }
    for (TActorIterator<ATorpedoPawn> It(World); It; ++It)
    {
        if (IsValid(*It))
            TrackedActors.Add(*It);
    }
}

// ---------------------------------------------------------------------------
//  DetectAndRecordVFXEvents
//
//  Called immediately after RecordTickFrame writes the current frame.
//  Compares the current set of GUIDs to the previous set:
//
//  TORPEDO DEATH: was in PreviousFrameGuids, not in CurrentGuids.
//    -> Record VFX at its last known location using its Characteristics DA.
//
//  SUBMARINE DEATH: bAlive just flipped to false in the frame we just wrote.
//    -> Record VFX at its current location using its Characteristics DA.
//    -> Store GUID in DeadSubmarineGuids so we don't double-record.
//
//  We detect submarine death from the just-recorded frame rather than from
//  a delegate so no binding is needed and the logic is self-contained.
// ---------------------------------------------------------------------------
void UReplayRecorderComponent::DetectAndRecordVFXEvents(float WorldTime)
{
    if (!LiveReplay || LiveReplay->TickFrames.Num() == 0) return;

    const UReplaySettings* S = GetSettings();
    if (!S) return;

    // The frame we just recorded
    const FReplayTickEntry& CurrentFrame = LiveReplay->TickFrames.Last();

    // Build current GUID set and update per-actor caches
    TSet<FGuid> CurrentGuids;
    for (int32 i = 0; i < CurrentFrame.ActorGuids.Num(); ++i)
    {
        const FGuid& Guid = CurrentFrame.ActorGuids[i];
        CurrentGuids.Add(Guid);

        // Update last-seen location
        LastSeenLocation.FindOrAdd(Guid) = CurrentFrame.ActorFrames[i].Location;

        // Check submarine death (bAlive just became false)
        if (!CurrentFrame.ActorFrames[i].bAlive && !DeadSubmarineGuids.Contains(Guid))
        {
            // This is a submarine (torpedoes always have bAlive=true in our recorder)
            DeadSubmarineGuids.Add(Guid);

            // Find the live actor to read its DA
            for (TActorIterator<ASubmarinePawn> It(GetWorld()); It; ++It)
            {
                ASubmarinePawn* Sub = *It;
                if (!IsValid(Sub) || Sub->GetActorInstanceGuid() != Guid) continue;

                const USubmarineCharacteristics* Stats = Sub->Characteristics
                    ? Sub->Characteristics.Get()
                    : GetDefault<USubmarineCharacteristics>();

                if (Stats && Stats->DeathExplosionEffect)
                {
                    RecordVFXEvent(
                        Stats->DeathExplosionEffect.Get(),
                        CurrentFrame.ActorFrames[i].Location,
                        FRotator::ZeroRotator,
                        FVector(Stats->DeathExplosionEffectScale));

                    UE_LOG(LogTemp, Log,
                        TEXT("[ReplayRecorder] Submarine death VFX recorded for '%s'"),
                        *LiveReplay->GetDisplayName(Guid));
                }
                break;
            }
        }
    }

    // Detect torpedo disappearances: was present last frame, gone now
    for (const FGuid& PrevGuid : PreviousFrameGuids)
    {
        if (CurrentGuids.Contains(PrevGuid)) continue;

        // This actor was present last tick but is gone now -- it died.
        // Only torpedoes are tracked in CachedTorpedoVFX.
        TObjectPtr<UNiagaraSystem>* CachedFXPtr = CachedTorpedoVFX.Find(PrevGuid);
        if (!CachedFXPtr) continue;

        UNiagaraSystem* CachedFX = CachedFXPtr->Get();

        const FVector* LastLoc = LastSeenLocation.Find(PrevGuid);
        if (!LastLoc) continue;

        float* CachedScale = CachedTorpedoVFXScale.Find(PrevGuid);
        const float Scale = CachedScale ? *CachedScale : 1.f;

        if (CachedFX)
        {
            RecordVFXEvent(CachedFX, *LastLoc, FRotator::ZeroRotator, FVector(Scale));

            if (S->bLogReplayRecorder) {
                UE_LOG(LogTemp, Log,
                    TEXT("[ReplayRecorder] Torpedo death VFX recorded for '%s' at %s"),
                    *LiveReplay->GetDisplayName(PrevGuid), *LastLoc->ToString());
            }
        }
        else
        {
            if (S->bLogReplayRecorder) {
                UE_LOG(LogTemp, Warning,
                    TEXT("[ReplayRecorder] Torpedo '%s' disappeared but had no ExplosionEffect DA"),
                    *LiveReplay->GetDisplayName(PrevGuid));
            }
        }

        // Clean up caches for this now-dead torpedo
        CachedTorpedoVFX.Remove(PrevGuid);
        CachedTorpedoVFXScale.Remove(PrevGuid);
        LastSeenLocation.Remove(PrevGuid);
    }

    // Cache torpedo VFX assets for newly-seen torpedoes.
    // We cache the asset directly (not a pointer to the actor) so it remains
    // accessible even after the torpedo is Destroy()ed.
    for (const TWeakObjectPtr<AActor>& Weak : TrackedActors)
    {
        ATorpedoPawn* Torp = Cast<ATorpedoPawn>(Weak.Get());
        if (!Torp) continue;
        const FGuid Guid = Torp->GetActorInstanceGuid();
        if (!CachedTorpedoVFX.Contains(Guid))
        {
            UTorpedoCharacteristics* DA = Torp->Characteristics.Get();
            UNiagaraSystem* FX = DA ? DA->ExplosionEffect.Get() : nullptr;
            float           Scale = DA ? DA->ExplosionEffectScale : 1.f;
            CachedTorpedoVFX.Add(Guid, FX);
            CachedTorpedoVFXScale.Add(Guid, Scale);
            if (!FX && S->bLogReplayRecorder)
                UE_LOG(LogTemp, Warning,
                    TEXT("[ReplayRecorder] Torpedo '%s' has no ExplosionEffect -- will be skipped if it dies"),
                    *Torp->GetName());
        }
    }

    PreviousFrameGuids = MoveTemp(CurrentGuids);
}

// ---------------------------------------------------------------------------
//  RecordVFXEvent
// ---------------------------------------------------------------------------
void UReplayRecorderComponent::RecordVFXEvent(UNiagaraSystem* Asset,
    const FVector& Location, const FRotator& Rotation, const FVector& Scale)
{
    if (!bRecording || !LiveReplay || !Asset) return;

    FReplayVFXEvent Event;
    Event.Timestamp = GetWorld()->GetTimeSeconds();
    Event.Location = Location;
    Event.Rotation = Rotation;
    Event.Scale = Scale;
    Event.NiagaraAsset = Asset;

    LiveReplay->VFXEvents.Add(Event);

    const UReplaySettings* S = GetSettings();
    if (!S) return;
    if (S->bLogReplayRecorder) {
        UE_LOG(LogTemp, Log, TEXT("[ReplayRecorder] VFX '%s' at %s t=%.2f"),
            *Asset->GetName(), *Location.ToString(), Event.Timestamp);
    }
}

// ---------------------------------------------------------------------------
//  RecordTickFrame
// ---------------------------------------------------------------------------
void UReplayRecorderComponent::RecordTickFrame(float WorldTime)
{
    FReplayTickEntry Entry;
    Entry.Timestamp = WorldTime;

    for (const TWeakObjectPtr<AActor>& Weak : TrackedActors)
    {
        AActor* Actor = Weak.Get();
        if (!IsValid(Actor)) continue;

        const FGuid Guid = Actor->GetActorInstanceGuid();

        // Register display name on first encounter
        if (!LiveReplay->GuidToDisplayName.Contains(Guid))
            LiveReplay->GuidToDisplayName.Add(Guid, Actor->GetName());

        Entry.ActorGuids.Add(Guid);
        Entry.ActorFrames.Add(BuildActorFrame(Actor));
    }

    LiveReplay->TickFrames.Add(Entry);

    // Detect deaths and record any VFX events they generate
    DetectAndRecordVFXEvents(WorldTime);
}

// ---------------------------------------------------------------------------
//  RecordKeyframe
// ---------------------------------------------------------------------------
void UReplayRecorderComponent::RecordKeyframe(float WorldTime)
{
    FReplayKeyframe KF;
    KF.Timestamp = WorldTime;

    for (const TWeakObjectPtr<AActor>& Weak : TrackedActors)
    {
        AActor* Actor = Weak.Get();
        if (!IsValid(Actor)) continue;

        const FGuid Guid = Actor->GetActorInstanceGuid();
        if (!LiveReplay->GuidToDisplayName.Contains(Guid))
            LiveReplay->GuidToDisplayName.Add(Guid, Actor->GetName());

        KF.ActorGuids.Add(Guid);
        KF.ActorSnapshots.Add(BuildActorSnapshot(Actor));
    }

    if (LiveReplay)
        LiveReplay->Keyframes.Add(KF);
}

// ---------------------------------------------------------------------------
//  Build helpers
// ---------------------------------------------------------------------------
FReplayActorFrame UReplayRecorderComponent::BuildActorFrame(AActor* Actor) const
{
    FReplayActorFrame F;
    F.Location = Actor->GetActorLocation();
    F.Rotation = Actor->GetActorRotation();
    F.bAlive = true;

    if (ASubmarinePawn* Sub = Cast<ASubmarinePawn>(Actor))
    {
        F.LinearSpeed = Sub->CurrentLinearSpeed;
        F.VerticalSpeed = Sub->CurrentVerticalSpeed;
        F.YawSpeed = Sub->CurrentYawSpeed;

        if (USubmarineCollisionComponent* Col =
            Sub->FindComponentByClass<USubmarineCollisionComponent>())
        {
            F.Health = Col->CurrentHealth;
            F.bAlive = !(Col->GetHealthRatio() <= 0.f); // alive if health > 0
        }

        if (USubmarineTorpedoComponent* Torp =
            Sub->FindComponentByClass<USubmarineTorpedoComponent>())
        {
            F.NormalTorpedoes = Torp->CurrentNormalTorpedoes;
            F.SpecialTorpedoes = Torp->CurrentSpecialTorpedoes;
        }
    }
    else if (ATorpedoPawn* Torp = Cast<ATorpedoPawn>(Actor))
    {
        // Torpedoes: forward speed only
        F.LinearSpeed = Torp->GetCurrentSpeed();
    }

    return F;
}

FReplayActorSnapshot UReplayRecorderComponent::BuildActorSnapshot(AActor* Actor) const
{
    // Snapshot has the same fields as Frame — copy from frame
    const FReplayActorFrame F = BuildActorFrame(Actor);
    FReplayActorSnapshot S;
    S.Location = F.Location;
    S.Rotation = F.Rotation;
    S.LinearSpeed = F.LinearSpeed;
    S.VerticalSpeed = F.VerticalSpeed;
    S.YawSpeed = F.YawSpeed;
    S.Health = F.Health;
    S.NormalTorpedoes = F.NormalTorpedoes;
    S.SpecialTorpedoes = F.SpecialTorpedoes;
    S.bAlive = F.bAlive;
    return S;
}

// ---------------------------------------------------------------------------
//  Settings helper
// ---------------------------------------------------------------------------
const UReplaySettings* UReplayRecorderComponent::GetSettings() const
{
    if (Settings) return Settings;
    return GetDefault<UReplaySettings>();
}