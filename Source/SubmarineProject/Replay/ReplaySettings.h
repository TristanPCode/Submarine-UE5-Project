#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DeathSequenceComponent.h"
#include "ReplaySettings.generated.h"

// -----------------------------------------------------------------------
//  Screen fade settings — one struct reused for all three game phases
// -----------------------------------------------------------------------
USTRUCT(BlueprintType)
struct FScreenFadeSettings
{
    GENERATED_BODY()

    /** Enable a fade-in from black when this phase starts. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade")
    bool bFadeIn = true;

    /** Duration of the fade-in in seconds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade",
        meta = (ClampMin = "0.0", EditCondition = "bFadeIn"))
    float FadeInDuration = 0.5f;

    /** Enable a fade-to-black before this phase ends / transitions out. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade")
    bool bFadeOut = true;

    /** Duration of the fade-out in seconds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade",
        meta = (ClampMin = "0.0", EditCondition = "bFadeOut"))
    float FadeOutDuration = 0.5f;

    /** Colour to fade to/from (default black). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade")
    FLinearColor FadeColor = FLinearColor::Black;

    // ---- Black Screen (instant black hold) --------------------------

    /**
     * If true, snap the screen to solid black BEFORE the fade-in begins.
     * Use this to ensure the screen is definitely black at the start of a phase
     * regardless of what was visible before.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackScreen")
    bool bBlackScreenIn = false;

    /**
     * How long (seconds) to hold the solid black screen BEFORE the fade-in starts.
     * Only used when bBlackScreenIn is true.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackScreen",
        meta = (ClampMin = "0.0", EditCondition = "bBlackScreenIn"))
    float BlackScreenInDuration = 0.5f;

    /**
     * If true, hold the screen at solid black for BlackScreenOutDuration seconds
     * AFTER the fade-out completes.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackScreen")
    bool bBlackScreenOut = false;

    /**
     * How long (seconds) to hold solid black AFTER the fade-out completes.
     * Only used when bBlackScreenOut is true.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackScreen",
        meta = (ClampMin = "0.0", EditCondition = "bBlackScreenOut"))
    float BlackScreenOutDuration = 0.5f;

    /** Colour to hold to during "BlackScreen" (default black). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade")
    FLinearColor BlackScreenColor = FLinearColor::Black;

    // ---- Chaining ---------------------------------------------------

    /**
     * Key of the next fade to chain into after this fade-out completes.
     * Looked up in UScreenFadeComponent::FadeLibrary at runtime.
     *
     * NAME_None = no chain, ClearFade is called at the end as normal.
     *
     * Rules when a next fade is found:
     *   - If it has bFadeIn or bBlackScreenIn -> seamless (no ClearFade injected).
     *   - If it has neither                   -> ClearFade first, then FadeIn.
     *
     * Change at runtime via UScreenFadeComponent::SetNextFade().
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain")
    FName NextFadeName = NAME_None;
};

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
    float DeathPreviewDelay = 3.f;

    /**
     * Distance (cm) behind the tracked ghost actor for 3rd-person death cams.
     * Replaces the old static constexpr in DeathSequenceComponent.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|DeathCam",
        meta = (ClampMin = "100.0"))
    float DeathCamThirdPersonRadius = 800.f;

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

    /**
     * Automatically call SaveReplay() when StopRecording() is called.
     * Enable this to make <Project>/Saved/SaveGames/<ReplaySaveSlot>.sav appear.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|Storage")
    bool bAutoSaveOnStop = false;

    // -----------------------------------------------------------------------
    //  Fade Library
    //
    //  Named collection of FScreenFadeSettings.
    //  Copied into UScreenFadeComponent at BeginPlay and overridable at runtime.
    //
    //  Suggested keys (you can use any FName you like):
    //    "Gameplay"    -- match start fade-in, death fade-out
    //    "DeathReplay" -- death cam fade-in and fade-out
    //    "Spectator"   -- spectator mode fade-in
    //    "Victory"     -- future
    //    "ExitLevel"   -- future
    //
    //  Chain example in the editor:
    //    "Gameplay"    -> NextFadeName = "DeathReplay"
    //    "DeathReplay" -> NextFadeName = "Spectator"
    //    "Spectator"   -> NextFadeName = NAME_None
    // -----------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Fades")
    TMap<FName, FScreenFadeSettings> FadeLibrary;

    // -----------------------------------------------------------------------
    //  Helpers
    // -----------------------------------------------------------------------

    /** Interval in seconds between recorded tick-frames (reciprocal of RecordTickRate). */
    UFUNCTION(BlueprintPure, Category = "Replay")
    float GetTickFrameInterval() const
    {
        return (RecordTickRate > 0) ? (1.f / static_cast<float>(RecordTickRate)) : (1.f / 30.f);
    }

    /**
     * Returns the actual seconds of replay content available after the death moment.
     * = min(DeathReplayDuration, DeathReplayLeadIn + DeathPreviewDelay)
     * Used by the GameMode to determine how long to keep recording after death.
     */
    UFUNCTION(BlueprintPure, Category = "Replay")
    float GetEffectivePostDeathContent() const
    {
        return FMath::Min(DeathReplayDuration, DeathReplayLeadIn + DeathPreviewDelay);
    }

    /**
     * Returns the wall-clock duration of the death cam replay as the player sees it.
     * = GetEffectivePostDeathContent() / PlaybackSpeed
     */
    UFUNCTION(BlueprintPure, Category = "Replay")
    float GetDeathCamWallClockDuration() const
    {
        return (PlaybackSpeed > 0.f)
            ? GetEffectivePostDeathContent() / PlaybackSpeed
            : GetEffectivePostDeathContent();
    }

    // -----------------------------------------------------------------------
    //  Debug / Logging toggles
    //  Turn these off in shipping to silence the Output Log.
    // -----------------------------------------------------------------------

    /** Log [ReplayRecorder] messages (VFX recording, slice extraction, etc.) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|Debug")
    bool bLogReplayRecorder = true;

    /** Log [ReplayPlayback] messages (ghost spawn/hide, VFX spawn, ticks) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|Debug")
    bool bLogReplayPlayback = true;

    /** Log [ReplayGhost] messages (mesh cloning, component copying) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|Debug")
    bool bLogReplayGhost = false;

    /** Log [DeathSeq] messages (death sequence phases, timing) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|Debug")
    bool bLogDeathSeq = true;

    /** Log [DeathCam] messages (camera mode, position, per-tick logs) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|Debug")
    bool bLogDeathCam = true;

    /** Log [ScreenFade] messages (fade start/stop, alpha ticks) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|Debug")
    bool bLogScreenFade = true;

    /** Log [GameMode] messages (death events, slice extraction, spectator) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|Debug")
    bool bLogGameMode = true;
};