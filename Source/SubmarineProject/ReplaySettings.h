#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DeathSequenceComponent.h"
#include "ReplaySettings.generated.h"

/**
 * UReplaySettings
 *
 * Game-wide DataAsset controlling the recording and playback system.
 * Assign one instance to ASubmarineGameMode::ReplaySettings.
 *
 * Recording architecture:
 *   - Every tick a lightweight FReplayTickFrame is stored per tracked actor
 *     (position, rotation, speeds, health, ammo counts — delta friendly).
 *   - Every FullSnapshotInterval seconds a full FReplaySnapshot "keyframe"
 *     is stored so playback can always find a clean starting point.
 *   - On playback, the system jumps to the nearest earlier keyframe then
 *     re-simulates forward using tick frames until the target timestamp.
 */
UCLASS(BlueprintType)
class SUBMARINEPROJECT_API UReplaySettings : public UDataAsset
{
    GENERATED_BODY()

public:

    // -----------------------------------------------------------------------
    //  Recording
    // -----------------------------------------------------------------------

    /** Enable the recording system at all times (can be toggled at runtime too). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|Recording")
    bool bAutoRecord = true;

    /**
     * Maximum duration of the rolling replay buffer (seconds).
     * Older frames are discarded once the buffer exceeds this length.
     * 0 = unlimited (keep everything until manually stopped/cleared).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|Recording",
        meta = (ClampMin = "0.0"))
    float MaxRecordDuration = 300.f; // 5 minutes

    /**
     * How often a full "keyframe" snapshot is taken (seconds).
     * Smaller = more robust playback but heavier memory use.
     * Recommended: 2–5 s.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|Recording",
        meta = (ClampMin = "0.5"))
    float FullSnapshotInterval = 3.f;

    /**
     * How many tick-frames are recorded per second.
     * Lower = lighter memory, coarser playback (position pops).
     * Higher = smoother playback, heavier memory.
     * Does NOT need to match the game tick rate — recording is
     * throttled to this rate independently.
     * Recommended: 20–30 fps.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|Recording",
        meta = (ClampMin = "1", ClampMax = "120"))
    int32 RecordTickRate = 30;

    // -----------------------------------------------------------------------
    //  Death cam
    // -----------------------------------------------------------------------

    /**
     * Camera mode used during the death sequence.
     * KillerThirdPerson = fixed view behind the killer (recommended default).
     * KillerPOV         = nose-cam / cockpit view of the killer.
     * StaticBehindDead  = fixed view behind the dead submarine.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|DeathCam")
    EDeathCamMode DefaultDeathCamMode = EDeathCamMode::KillerThirdPerson;

    /**
     * How many seconds of death cam to play.
     * Set to 0 to skip entirely.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|DeathCam",
        meta = (ClampMin = "0.0"))
    float DeathReplayDuration = 5.f;

    /**
     * Extra seconds before the death moment included in the replay slice.
     * e.g. 2.0 starts the cam 2s before the killing blow.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|DeathCam",
        meta = (ClampMin = "0.0"))
    float DeathReplayLeadIn = 2.f;

    /** Allow the player to press a key to skip the death cam. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|DeathCam")
    bool bDeathReplaySkippable = true;

    /**
     * Pause in seconds shown before the death cam starts.
     * Good for a "You were destroyed" UI beat.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|DeathCam",
        meta = (ClampMin = "0.0"))
    float DeathPreviewDelay = 1.f;

    // -----------------------------------------------------------------------
    //  Playback
    // -----------------------------------------------------------------------

    /**
     * Playback speed multiplier for the full replay viewer.
     * 1.0 = real-time, 0.5 = half speed, 2.0 = double speed.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|Playback",
        meta = (ClampMin = "0.1", ClampMax = "10.0"))
    float PlaybackSpeed = 1.f;

    // -----------------------------------------------------------------------
    //  Storage
    // -----------------------------------------------------------------------

    /**
     * Slot name used when saving/loading replay data via the SaveGame system.
     * Each unique name is a separate save file on disk.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|Storage")
    FString ReplaySaveSlot = TEXT("SubmarineReplay_01");

    /**
     * User index passed to UGameplayStatics::SaveGameToSlot.
     * Usually 0 for single-player.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|Storage",
        meta = (ClampMin = "0"))
    int32 SaveUserIndex = 0;

    // -----------------------------------------------------------------------
    //  Helpers
    // -----------------------------------------------------------------------

    /** Interval in seconds between recorded tick-frames (reciprocal of RecordTickRate). */
    UFUNCTION(BlueprintPure, Category = "Replay")
    float GetTickFrameInterval() const
    {
        return (RecordTickRate > 0) ? (1.f / static_cast<float>(RecordTickRate)) : (1.f / 30.f);
    }
};