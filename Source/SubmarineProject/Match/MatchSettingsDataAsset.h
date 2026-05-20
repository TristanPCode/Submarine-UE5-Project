#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HUDContextTypes.h"
#include "SubmarinePawn.h"
#include "MatchSettingsDataAsset.generated.h"

class USubmarineHUDSettings;

// ============================================================================
//  EInputDeviceType
// ============================================================================
UENUM(BlueprintType)
enum class EInputDeviceType : uint8
{
    Keyboard,
    Gamepad0,
    Gamepad1,
    Gamepad2,
    Gamepad3
};

// ============================================================================
//  FPlayerInputMapping
// ============================================================================
USTRUCT(BlueprintType)
struct FPlayerInputMapping
{
    GENERATED_BODY()

    /** Local player index this mapping applies to (0-based). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 LocalPlayerIndex = 0;

    /** Primary input device for this player. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EInputDeviceType DeviceType = EInputDeviceType::Keyboard;
};

// ============================================================================
//  FMatchTeamSettings
// ============================================================================
USTRUCT(BlueprintType)
struct FMatchTeamSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
    FString TeamName = TEXT("Team");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
    FLinearColor TeamColor = FLinearColor::White;

    /** Team emblem / faction icon (future use). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
    TSoftObjectPtr<UTexture2D> TeamIcon;

    /** Faction or emblem name (future use). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
    FString FactionName;
};

// ============================================================================
//  UMatchSettingsDataAsset
//
//  Immutable authored data. NEVER modified at runtime.
//  At match start, URuntimeMatchSettings::CreateFromDataAsset() copies this
//  into a mutable runtime instance. The menu system modifies the runtime copy.
// ============================================================================
UCLASS(BlueprintType)
class SUBMARINEPROJECT_API UMatchSettingsDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:

    // -----------------------------------------------------------------------
    //  Players
    // -----------------------------------------------------------------------

    /** Number of local human players (1 or 2 for now). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Players",
        meta = (ClampMin = "1", ClampMax = "2"))
    int32 LocalPlayerCount = 1;

    /** Number of remote players (always 0 for now — future networking). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Players",
        meta = (ClampMin = "0"))
    int32 RemotePlayerCount = 0;

    /**
     * Display names for local players.
     * Index i = LocalPlayerIndex i. Falls back to "Player N" if too short.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Players")
    TArray<FString> PlayerNames;

    /**
     * Level for each local player (0-based index).
     * Used by InfoBillboard {Level} field.
     * Falls back to 1 if not set.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Players")
    TArray<int32> PlayerLevels;

    /**
     * Explicit team assignment per local player (0-based index).
     * PlayerTeamAssignments[0] = team index for Player 0.
     * If shorter than LocalPlayerCount, falls back to (PlayerIndex % TeamCount).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teams")
    TArray<int32> PlayerTeamAssignments;

    /**
     * Level for each CPU (0-based index).
     * Falls back to 1 if not set.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CPU",
        meta = (EditCondition = "bCPUEnabled"))
    TArray<int32> CPULevels;

    /**
     * Explicit team assignment per CPU (0-based index).
     * CPUTeamAssignments[0] = team index for CPU 0.
     * If shorter than CPUCount, falls back to ((LocalPlayerCount + CPUIndex) % TeamCount).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teams")
    TArray<int32> CPUTeamAssignments;

    /**
     * Submarine Blueprint subclass for each local player.
     * Index i = LocalPlayerIndex i.
     * Falls back to DefaultSubmarineClass if too short or entry is null.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Players")
    TArray<TSubclassOf<ASubmarinePawn>> PlayerSubmarineClasses;

    /** Input device mapping per local player. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Players")
    TArray<FPlayerInputMapping> InputMappings;

    // -----------------------------------------------------------------------
    //  CPU
    // -----------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CPU")
    bool bCPUEnabled = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CPU",
        meta = (ClampMin = "0", EditCondition = "bCPUEnabled"))
    int32 CPUCount = 0;

    /**
     * Display names for CPU submarines.
     * Falls back to "CPU N" if too short.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CPU",
        meta = (EditCondition = "bCPUEnabled"))
    TArray<FString> CPUNames;

    /**
     * Submarine Blueprint subclass for each CPU.
     * Falls back to DefaultSubmarineClass if too short or null.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CPU",
        meta = (EditCondition = "bCPUEnabled"))
    TArray<TSubclassOf<ASubmarinePawn>> CPUSubmarineClasses;

    // -----------------------------------------------------------------------
    //  Default submarine class (fallback for any unspecified entry)
    // -----------------------------------------------------------------------

    /**
     * Fallback submarine class used when PlayerSubmarineClasses or
     * CPUSubmarineClasses entries are missing or null.
     * Must be a Blueprint subclass of ASubmarinePawn.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarines")
    TSubclassOf<ASubmarinePawn> DefaultSubmarineClass;

    // -----------------------------------------------------------------------
    //  Teams
    // -----------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Teams",
        meta = (ClampMin = "1", ClampMax = "8"))
    int32 TeamCount = 1;

    /**
     * Per-team configuration (name, color, icon, etc.).
     * If shorter than TeamCount, missing teams use default FMatchTeamSettings.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Teams")
    TArray<FMatchTeamSettings> Teams;

    // -----------------------------------------------------------------------
    //  Match limits
    // -----------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Match",
        meta = (ClampMin = "1"))
    int32 MaxSubmarineCount = 4;

    // -----------------------------------------------------------------------
    //  Split screen
    // -----------------------------------------------------------------------

    /**
     * Enable split-screen mode (left/right vertical split).
     * Only used when LocalPlayerCount >= 2.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplitScreen")
    bool bSplitScreenEnabled = false;

    // -----------------------------------------------------------------------
    //  HUD context overrides (optional — fall back to UHUDGlobalDefaults)
    // -----------------------------------------------------------------------

    /**
     * Optional per-context HUD DataAsset overrides for this match configuration.
     * Entries here take priority over UHUDGlobalDefaults.
     * Leave empty to use global defaults entirely.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD")
    TMap<EHUDContext, TObjectPtr<USubmarineHUDSettings>> HUDContextOverrides;
};