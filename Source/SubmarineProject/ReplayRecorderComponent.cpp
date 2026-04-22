// Fill out your copyright notice in the Description page of Project Settings.

#include "ReplayRecorderComponent.h"
#include "ReplaySettings.h"
#include "ReplayData.h"
#include "SubmarinePawn.h"
#include "TorpedoPawn.h"
#include "SubmarineCollisionComponent.h"
#include "SubmarineTorpedoComponent.h"
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

    // Reset buffer
    if (LiveReplay)
    {
        LiveReplay->TickFrames.Reset();
        LiveReplay->Keyframes.Reset();
        LiveReplay->RecordStartTime = GetWorld()->GetTimeSeconds();
        LiveReplay->RecordEndTime = LiveReplay->RecordStartTime;
    }

    // Force an immediate keyframe so playback always has a starting point
    RecordKeyframe(GetWorld()->GetTimeSeconds());

    OnRecordingStarted.Broadcast();
    UE_LOG(LogTemp, Log, TEXT("[ReplayRecorder] Recording started"));
}

void UReplayRecorderComponent::StopRecording()
{
    if (!bRecording) return;
    bRecording = false;
    OnRecordingStopped.Broadcast();
    UE_LOG(LogTemp, Log, TEXT("[ReplayRecorder] Recording stopped. Duration=%.1fs, TickFrames=%d, Keyframes=%d"),
        LiveReplay ? LiveReplay->GetDuration() : 0.f,
        LiveReplay ? LiveReplay->TickFrames.Num() : 0,
        LiveReplay ? LiveReplay->Keyframes.Num() : 0);
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

    UE_LOG(LogTemp, Log, TEXT("[ReplayRecorder] SaveReplay %s — slot=%s"),
        bOk ? TEXT("OK") : TEXT("FAILED"), *S->ReplaySaveSlot);

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
        UE_LOG(LogTemp, Warning, TEXT("[ReplayRecorder] LoadReplay failed — slot=%s"), *S->ReplaySaveSlot);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[ReplayRecorder] LoadReplay OK — duration=%.1fs, TickFrames=%d"),
        LoadedReplay->GetDuration(), LoadedReplay->TickFrames.Num());
    return true;
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

        Entry.ActorNames.Add(Actor->GetName());
        Entry.ActorFrames.Add(BuildActorFrame(Actor));
    }

    if (LiveReplay)
        LiveReplay->TickFrames.Add(Entry);
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

        KF.ActorNames.Add(Actor->GetName());
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