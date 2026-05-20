#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MatchSettingsDataAsset.h"
#include "HUDContextTypes.h"
#include "RuntimeMatchSettings.generated.h"

class USubmarineHUDSettings;
class UHUDGlobalDefaults;

/**
 * URuntimeMatchSettings
 *
 * Mutable runtime copy of UMatchSettingsDataAsset.
 * Lives on UGameInstance so it persists through level loads.
 *
 * Lifecycle:
 *   1. Created via CreateFromDataAsset() before or during menu flow.
 *   2. Menu system may modify any field freely.
 *   3. USpawnManagerComponent reads this at match start — never mutates it.
 *   4. USubmarineAssetLoader reads this to collect assets to preload.
 *
 * The original UMatchSettingsDataAsset is NEVER touched at runtime.
 *
 * Access pattern:
 *   UGameInstance* GI = GetWorld()->GetGameInstance();
 *   URuntimeMatchSettings* RMS = GI->GetSubsystem<...>() or direct cast.
 *   (In practice: stored as a UPROPERTY on your UGameInstance subclass)
 */
UCLASS(BlueprintType)
class SUBMARINEPROJECT_API URuntimeMatchSettings : public UObject
{
    GENERATED_BODY()

public:

    /**
     * Create a runtime instance by copying all values from a DataAsset.
     * The DataAsset is not referenced after this call — only values are copied.
     */
    UFUNCTION(BlueprintCallable, Category = "MatchSettings",
        meta = (DeterminesOutputType = "Outer"))
    static URuntimeMatchSettings* CreateFromDataAsset(
        UObject* Outer,
        const UMatchSettingsDataAsset* SourceDA);

    // -----------------------------------------------------------------------
    //  All fields mirror UMatchSettingsDataAsset but are mutable
    // -----------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Players",
        meta = (ClampMin = "1", ClampMax = "2"))
    int32 LocalPlayerCount = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Players")
    int32 RemotePlayerCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Players")
    TArray<FString> PlayerNames;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Players")
    TArray<int32> PlayerLevels;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Players")
    TArray<TSubclassOf<ASubmarinePawn>> PlayerSubmarineClasses;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Players")
    TArray<FPlayerInputMapping> InputMappings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CPU")
    bool bCPUEnabled = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CPU")
    int32 CPUCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CPU")
    TArray<FString> CPUNames;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CPU")
    TArray<int32> CPULevels;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CPU")
    TArray<TSubclassOf<ASubmarinePawn>> CPUSubmarineClasses;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Submarines")
    TSubclassOf<ASubmarinePawn> DefaultSubmarineClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teams")
    int32 TeamCount = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teams")
    TArray<FMatchTeamSettings> Teams;

    /**
     * Explicit team assignment per local player (0-based index).
     * PlayerTeamAssignments[0] = team index for Player 0.
     * If shorter than LocalPlayerCount, falls back to (PlayerIndex % TeamCount).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teams")
    TArray<int32> PlayerTeamAssignments;

    /**
     * Explicit team assignment per CPU (0-based index).
     * CPUTeamAssignments[0] = team index for CPU 0.
     * If shorter than CPUCount, falls back to ((LocalPlayerCount + CPUIndex) % TeamCount).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teams")
    TArray<int32> CPUTeamAssignments;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match")
    int32 MaxSubmarineCount = 4;

    /** Camera FOV for solo play. Default 90. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
    float SoloCameraFOV = 90.f;

    /** Camera FOV when split-screen is active (wider to compensate for smaller viewport). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
    float SplitScreenCameraFOV = 110.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplitScreen")
    bool bSplitScreenEnabled = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
    TMap<EHUDContext, TObjectPtr<USubmarineHUDSettings>> HUDContextOverrides;

    // -----------------------------------------------------------------------
    //  HUD resolution helper
    // -----------------------------------------------------------------------

    /**
     * Resolve the HUD settings for a given context.
     * Resolution order:
     *   1. HUDContextOverrides[Context]        (this runtime settings)
     *   2. GlobalDefaults->Resolve(Context)     (global default registry)
     *   3. nullptr                              (no HUD for this context)
     *
     * @param Context        The desired HUD context
     * @param GlobalDefaults The project-wide default registry (from GameMode)
     */
    UFUNCTION(BlueprintPure, Category = "MatchSettings")
    USubmarineHUDSettings* ResolveHUDSettings(
        EHUDContext Context,
        const UHUDGlobalDefaults* GlobalDefaults) const;

    // -----------------------------------------------------------------------
    //  Submarine class resolution helpers
    // -----------------------------------------------------------------------

    /**
     * Resolve the submarine class for a local player.
     * Falls back to DefaultSubmarineClass if index out of range or entry null.
     */
    UFUNCTION(BlueprintPure, Category = "MatchSettings")
    TSubclassOf<ASubmarinePawn> GetPlayerSubmarineClass(int32 LocalPlayerIndex) const;

    /**
     * Resolve the submarine class for a CPU entry.
     * Falls back to DefaultSubmarineClass if index out of range or entry null.
     */
    UFUNCTION(BlueprintPure, Category = "MatchSettings")
    TSubclassOf<ASubmarinePawn> GetCPUSubmarineClass(int32 CPUIndex) const;

    /**
     * Resolve the display name for a local player.
     * Falls back to "Player N" (1-based) if not set.
     */
    UFUNCTION(BlueprintPure, Category = "MatchSettings")
    FString GetPlayerName(int32 LocalPlayerIndex) const;

    /**
     * Resolve the display name for a CPU.
     * Falls back to "CPU N" (1-based) if not set.
     */
    UFUNCTION(BlueprintPure, Category = "MatchSettings")
    FString GetCPUName(int32 CPUIndex) const;

    /** Get the level for a local player. Falls back to 1. */
    UFUNCTION(BlueprintPure, Category = "MatchSettings")
    int32 GetPlayerLevel(int32 LocalPlayerIndex) const
    {
        return PlayerLevels.IsValidIndex(LocalPlayerIndex)
            ? FMath::Max(1, PlayerLevels[LocalPlayerIndex]) : 1;
    }

    /** Get the level for a CPU. Falls back to 1. */
    UFUNCTION(BlueprintPure, Category = "MatchSettings")
    int32 GetCPULevel(int32 CPUIndex) const
    {
        return CPULevels.IsValidIndex(CPUIndex)
            ? FMath::Max(1, CPULevels[CPUIndex]) : 1;
    }

    /**
     * Resolve team settings for a team index.
     * Falls back to default FMatchTeamSettings if index out of range.
     */
    UFUNCTION(BlueprintPure, Category = "MatchSettings")
    FMatchTeamSettings GetTeamSettings(int32 TeamIndex) const;

    UFUNCTION(BlueprintPure, Category = "MatchSettings")
    int32 GetPlayerTeamIndex(int32 LocalPlayerIndex) const
    {
        if (PlayerTeamAssignments.IsValidIndex(LocalPlayerIndex))
            return PlayerTeamAssignments[LocalPlayerIndex];
        return (TeamCount > 0) ? (LocalPlayerIndex % TeamCount) : 0;
    }

    UFUNCTION(BlueprintPure, Category = "MatchSettings")
    int32 GetCPUTeamIndex(int32 CPUIndex) const
    {
        if (CPUTeamAssignments.IsValidIndex(CPUIndex))
            return CPUTeamAssignments[CPUIndex];
        return (TeamCount > 0) ? ((LocalPlayerCount + CPUIndex) % TeamCount) : 0;
    }

    /**
     * Total number of submarines in this match
     * (LocalPlayerCount + RemotePlayerCount + CPUCount, clamped to MaxSubmarineCount).
     */
    UFUNCTION(BlueprintPure, Category = "MatchSettings")
    int32 GetTotalSubmarineCount() const;
};