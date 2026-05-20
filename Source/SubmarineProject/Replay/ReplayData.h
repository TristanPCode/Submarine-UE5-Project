#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Engine/DataAsset.h"
#include "NiagaraSystem.h"
#include "TorpedoCharacteristics.h"
#include "ReplayData.generated.h"

// ---------------------------------------------------------------------------
//  FReplayActorMeta
//
//  Static identity data for one tracked actor. Recorded ONCE when the actor
//  is first seen. Never changes during a match.
//  Stored in UReplayData::GuidToMeta.
//
//  Used by:
//   - InfoBillboard during replay (replaces live ITrackableSubmarine reads)
//   - Spectator UI (team roster, kills feed)
//   - ReplayPlayback (ghost labeling)
// ---------------------------------------------------------------------------
USTRUCT(BlueprintType)
struct FReplayActorMeta
{
    GENERATED_BODY()

    // -- Common --
    UPROPERTY() FString  DisplayName;
    UPROPERTY() int32    TeamIndex = 0;
    UPROPERTY() FString  TeamName;
    UPROPERTY() FString  FactionName;
    UPROPERTY() FLinearColor TeamColor = FLinearColor::White;
    UPROPERTY() bool     bIsCPU = false;
    UPROPERTY() int32    Level = 1;
    UPROPERTY() bool     bIsTorpedo = false;

    // -- Torpedo-specific (only valid when bIsTorpedo = true) --
    UPROPERTY() ETorpedoType TorpedoType = ETorpedoType::Normal;
    UPROPERTY() float AttackDamage = 0.f;
    UPROPERTY() float MaxLifetime = 0.f;
    UPROPERTY() FString OwnerDisplayName; // display name of the firing submarine
    UPROPERTY() FGuid   OwnerGuid;        // GUID of the firing submarine
};

// ---------------------------------------------------------------------------
//  Per-actor lightweight tick frame  (recorded every 1/RecordTickRate seconds)
// ---------------------------------------------------------------------------
USTRUCT(BlueprintType)
struct FReplayActorFrame
{
    GENERATED_BODY()

    /** World-space position */
    UPROPERTY()
    FVector Location = FVector::ZeroVector;

    /** World-space rotation (pitch, yaw — roll always 0 for submarines) */
    UPROPERTY()
    FRotator Rotation = FRotator::ZeroRotator;

    /** Signed linear speed (cm/s) along the actor's forward axis */
    UPROPERTY()
    float LinearSpeed = 0.f;

    /** Vertical speed (cm/s) */
    UPROPERTY()
    float VerticalSpeed = 0.f;

    /** Yaw angular speed (degrees/s) */
    UPROPERTY()
    float YawSpeed = 0.f;

    /** Health (raw value, not ratio) */
    UPROPERTY()
    float Health = 0.f;

    /** Normal torpedo count (submarines only; 0 for torpedoes) */
    UPROPERTY()
    int32 NormalTorpedoes = 0;

    /** Special torpedo count (submarines only) */
    UPROPERTY()
    int32 SpecialTorpedoes = 0;

    // Dynamic InfoBillboard data -- updated every frame
    UPROPERTY() float    FireCooldownRatio = 1.f;   // 0=cooldown, 1=ready
    UPROPERTY() float    ReloadRatio = 1.f;

    /**
     * Always true while the actor exists in TActorIterator.
     * For submarines: false when health reaches 0.
     * For torpedoes: the actor simply disappears from the recording when
     * it calls Destroy() -- there is never an explicit false frame.
     * Playback uses presence/absence detection to find torpedo death time.
     */
    UPROPERTY()
    bool bAlive = true;
};

// ---------------------------------------------------------------------------
//  Full snapshot — one per actor, taken every FullSnapshotInterval seconds
//  Same fields as FReplayActorFrame but semantically a "keyframe"
// ---------------------------------------------------------------------------
USTRUCT(BlueprintType)
struct FReplayActorSnapshot
{
    GENERATED_BODY()

    UPROPERTY() FVector  Location = FVector::ZeroVector;
    UPROPERTY() FRotator Rotation = FRotator::ZeroRotator;
    UPROPERTY() float    LinearSpeed = 0.f;
    UPROPERTY() float    VerticalSpeed = 0.f;
    UPROPERTY() float    YawSpeed = 0.f;
    UPROPERTY() float    Health = 0.f;
    UPROPERTY() int32    NormalTorpedoes = 0;
    UPROPERTY() int32    SpecialTorpedoes = 0;
    UPROPERTY() float    FireCooldownRatio = 1.f;
    UPROPERTY() float    ReloadRatio = 1.f;
    UPROPERTY() bool     bAlive = true;
};

// ---------------------------------------------------------------------------
//  One frame of the whole game world (all tracked actors at one timestamp)
// ---------------------------------------------------------------------------
USTRUCT(BlueprintType)
struct FReplayTickEntry
{
    GENERATED_BODY()

    /** Game time (seconds, from GetWorld()->GetTimeSeconds()) */
    UPROPERTY()
    float Timestamp = 0.f;

    /** Stable per-instance GUIDs -- index matches ActorFrames. */
    UPROPERTY() TArray<FGuid> ActorGuids;

    /** Per-actor transform + state data -- index matches ActorGuids. */
    UPROPERTY() TArray<FReplayActorFrame> ActorFrames;
};

// ---------------------------------------------------------------------------
//  Full-world keyframe snapshot
// ---------------------------------------------------------------------------
USTRUCT(BlueprintType)
struct FReplayKeyframe
{
    GENERATED_BODY()

    UPROPERTY()
    float Timestamp = 0.f;

    /** Stable per-instance GUIDs -- index matches ActorSnapshots. */
    UPROPERTY() TArray<FGuid> ActorGuids;

    UPROPERTY()
    TArray<FReplayActorSnapshot> ActorSnapshots;
};

// ---------------------------------------------------------------------------
//  VFX event -- a Niagara effect that should play at a specific moment
//  during replay.  Captured by the recorder when Explode() fires; played
//  back by the playback component at the matching timestamp.
//
//  Using TSoftObjectPtr so the asset reference survives serialisation without
//  forcing a hard load at record time.
// ---------------------------------------------------------------------------
USTRUCT(BlueprintType)
struct FReplayVFXEvent
{
    GENERATED_BODY()

    /** World time when this effect should fire during playback */
    UPROPERTY() float Timestamp = 0.f;

    UPROPERTY() FVector   Location = FVector::ZeroVector;
    UPROPERTY() FRotator  Rotation = FRotator::ZeroRotator;
    UPROPERTY() FVector   Scale = FVector::OneVector;

    /** The Niagara system asset to spawn */
    UPROPERTY() TSoftObjectPtr<UNiagaraSystem> NiagaraAsset;
};

// ---------------------------------------------------------------------------
//  Team snapshot -- one entry per team, updated each keyframe.
//  Used by spectator/replay UI to show team rosters and kill feeds.
// ---------------------------------------------------------------------------
USTRUCT(BlueprintType)
struct FReplayTeamEntry
{
    GENERATED_BODY()

    UPROPERTY() int32    TeamIndex = 0;
    UPROPERTY() FString  TeamName;
    UPROPERTY() FLinearColor TeamColor = FLinearColor::White;
    UPROPERTY() FString  FactionName;

    /** GUIDs of all submarines belonging to this team (alive or dead). */
    UPROPERTY() TArray<FGuid> SubmarineGuids;
};

// ---------------------------------------------------------------------------
//  Match event -- discrete events (kill, torpedo fired, etc.)
//  Stored in UReplayData::MatchEvents, sorted by timestamp.
//  Future use: kill feed UI, match timeline scrubbing.
// ---------------------------------------------------------------------------
UENUM(BlueprintType)
enum class EReplayMatchEventType : uint8
{
    SubmarineDied,
    TorpedoFired,
    TorpedoImpact,
    RadarScan,
    MatchStart,
    MatchEnd
};

USTRUCT(BlueprintType)
struct FReplayMatchEvent
{
    GENERATED_BODY()

    UPROPERTY() float                 Timestamp = 0.f;
    UPROPERTY() EReplayMatchEventType EventType = EReplayMatchEventType::SubmarineDied;
    UPROPERTY() FGuid                 ActorGuid;        // primary actor (victim, torpedo, etc.)
    UPROPERTY() FGuid                 InstigatorGuid;   // killer, firer, etc.
    UPROPERTY() FString               Description;      // human-readable for debug
};

// ---------------------------------------------------------------------------
//  UReplayData (The SaveGame object that holds the entire recording)
// ---------------------------------------------------------------------------
UCLASS(BlueprintType)
class SUBMARINEPROJECT_API UReplayData : public USaveGame
{
    GENERATED_BODY()

public:

    // -----------------------------------------------------------------------
    //  Metadata
    // -----------------------------------------------------------------------

    /** World time when recording started */
    UPROPERTY(BlueprintReadOnly, Category = "Replay")
    float RecordStartTime = 0.f;

    /** World time when recording stopped (or last frame time if still recording) */
    UPROPERTY(BlueprintReadOnly, Category = "Replay")
    float RecordEndTime = 0.f;

    /** Human-readable label (e.g. "Match 1 - 2026-04-21") */
    UPROPERTY(BlueprintReadWrite, Category = "Replay")
    FString Label;

    // -----------------------------------------------------------------------
    //  GUID -> display name registry
    //  Populated once per actor when first seen by the recorder.
    //  Read-only by the playback side (used only for log messages).
    // -----------------------------------------------------------------------
    UPROPERTY() TMap<FGuid, FString> GuidToDisplayName;

    /**
     * Full actor metadata per GUID.
     * Populated when the recorder first sees each actor.
     * Used by InfoBillboard during replay and spectator UI.
     */
    UPROPERTY() TMap<FGuid, FReplayActorMeta> GuidToMeta;

    // -- Team info (set once at match start) --
    UPROPERTY() TArray<FReplayTeamEntry> Teams;

    // -----------------------------------------------------------------------
    //  Recording data
    // -----------------------------------------------------------------------

    /**
     * Ordered tick frames (one entry every 1/RecordTickRate seconds).
     * Entries are sorted ascending by Timestamp.
     */
    UPROPERTY()
    TArray<FReplayTickEntry> TickFrames;

    /**
     * Full-world keyframes (one every FullSnapshotInterval seconds).
     * Used as playback starting points to avoid replaying the entire buffer.
     */
    UPROPERTY()
    TArray<FReplayKeyframe> Keyframes;

    /**
     * All VFX events captured during recording (torpedo explosions,
     * submarine death explosions, etc.).  Sorted ascending by Timestamp.
     * Playback spawns these at the matching world time.
     */
    UPROPERTY() TArray<FReplayVFXEvent> VFXEvents;

    // -- Match events (discrete, for kill feed / timeline) --
    UPROPERTY() TArray<FReplayMatchEvent> MatchEvents;

    // -----------------------------------------------------------------------
    //  Helpers
    // -----------------------------------------------------------------------

    /** Total recorded duration in seconds */
    UFUNCTION(BlueprintPure, Category = "Replay")
    float GetDuration() const { return RecordEndTime - RecordStartTime; }

    /** Display name for a GUID, or "Unknown" if not registered. */
    FString GetDisplayName(const FGuid& Guid) const
    {
        const FReplayActorMeta* Meta = GuidToMeta.Find(Guid);
        if (Meta) return Meta->DisplayName;
        const FString* Legacy = GuidToDisplayName.Find(Guid);
        return Legacy ? *Legacy : FString(TEXT("Unknown"));
    }

    const FReplayActorMeta* GetMeta(const FGuid& Guid) const
    {
        return GuidToMeta.Find(Guid);
    }

    void RecordMatchEvent(float Time, EReplayMatchEventType Type,
        const FGuid& Actor, const FGuid& Instigator = FGuid(),
        const FString& Desc = FString())
    {
        FReplayMatchEvent Ev;
        Ev.Timestamp = Time;
        Ev.EventType = Type;
        Ev.ActorGuid = Actor;
        Ev.InstigatorGuid = Instigator;
        Ev.Description = Desc;
        MatchEvents.Add(Ev);
    }

    /**
     * Returns the index of the last keyframe whose timestamp is <= TargetTime.
     * Returns INDEX_NONE if no keyframe exists before TargetTime.
     */
    int32 FindKeyframeIndexBefore(float TargetTime) const
    {
        int32 Best = INDEX_NONE;
        for (int32 i = 0; i < Keyframes.Num(); ++i)
        {
            if (Keyframes[i].Timestamp <= TargetTime)
                Best = i;
            else
                break;
        }
        return Best;
    }

    /**
     * Returns the index of the first tick frame whose timestamp is >= StartTime.
     * Returns INDEX_NONE if none found.
     */
    int32 FindFirstTickFrameAtOrAfter(float StartTime) const
    {
        for (int32 i = 0; i < TickFrames.Num(); ++i)
        {
            if (TickFrames[i].Timestamp >= StartTime)
                return i;
        }
        return INDEX_NONE;
    }

    /**
     * Trims tick frames and keyframes older than CutoffTime.
     * Called by the recorder to enforce MaxRecordDuration.
     */
    void TrimBefore(float CutoffTime)
    {
        TickFrames.RemoveAll([CutoffTime](const FReplayTickEntry& E)
            { return E.Timestamp < CutoffTime; });
        Keyframes.RemoveAll([CutoffTime](const FReplayKeyframe& K)
            { return K.Timestamp < CutoffTime; });
    }
};