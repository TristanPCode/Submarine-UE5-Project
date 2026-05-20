#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TeamSpawnSettings.generated.h"

// ============================================================================
//  FTeamLocatorGroupMapping
//
//  Defines which spawn locator groups a team may use when a given number of
//  teams is in play.
//
//  Example -- 2-team game, Team 0 mapping:
//    TeamIndex = 0
//    TeamsInPlay = 2
//    LocatorGroups = [0, 2, 4, 5]   (in priority order, highest first)
//
//  Meaning:
//    Team 0 submarines try group 0 first, then group 2, then 4, then 5.
//    Group 0 is the "home" group -- always listed first and always highest
//    priority.
//
//  Groups that appear in MULTIPLE teams' lists are shared.
//    In a 2-team game, if Team 0 lists group 4 and Team 1 also lists group 4:
//    both teams may use locators tagged Group=4, but no two submarines
//    occupy the same locator instance.
//
//  Teams can only use groups with GroupIndex < MaxTeamCount.
//  Rule enforced by UTeamSpawnSettings::GetLocatorGroupsForTeam():
//    groups are clamped/filtered to [0, MaxTeamCount-1].
//  This prevents a 2-team game from using groups 4,5,6,7 that are reserved
//  for 5-8 team games.
//  Exception: "shared overflow" groups (GroupIndex >= MaxTeamCount) may be
//  explicitly listed and are not filtered -- see bAllowOverflowGroups below.
// ============================================================================
USTRUCT(BlueprintType)
struct FTeamLocatorGroupMapping
{
    GENERATED_BODY()

    /** Team index (0-based) this mapping applies to. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnMapping",
        meta = (ClampMin = "0", ClampMax = "7"))
    int32 TeamIndex = 0;

    /** Number of active teams this mapping applies to (2-8). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnMapping",
        meta = (ClampMin = "2", ClampMax = "8"))
    int32 TeamsInPlay = 2;

    /**
     * Locator group indices this team may use, in priority order.
     * Index 0 = highest priority (always the team's "home" group).
     * Groups are tried in order until a valid unoccupied locator is found.
     *
     * Convention:
     *   Group 0..7 = one dedicated group per team (group N = Team N's home)
     *   Group 8+   = shared/overflow groups (any team may use them)
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnMapping")
    TArray<int32> LocatorGroups;

    /**
     * Priority tier per group entry (parallel to LocatorGroups, same length).
     * Lower value = tried first. Groups sharing a priority tier are tried together.
     *
     * Recommended convention: [0, 1, 1, ..., 1]
     *   Tier 0 = home group (tried first, exclusively).
     *   Tier 1 = all other accessible groups (tried only if tier 0 exhausted).
     *
     * If shorter than LocatorGroups, missing entries default to 1.
     * If empty, all groups are treated as equal priority (one big pass).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnMapping")
    TArray<int32> GroupPriorities;

    int32 GetGroupPriority(int32 SlotIdx) const
    {
        return GroupPriorities.IsValidIndex(SlotIdx)
            ? GroupPriorities[SlotIdx] : 1;
    }
};

// ============================================================================
//  UTeamSpawnSettings
//
//  DataAsset assigned per map (or globally as a default).
//  Defines the complete team-to-locator-group mapping for all team counts.
//
//  This asset is map-specific because different maps may have different
//  spawn locator layouts.
//
//  Typical setup for an 8-team-capable map:
//    8 dedicated areas (groups 0-7), one per team
//    + optional shared areas (groups 8+) used as overflow
//
//  When fewer teams are in play, unused team groups become available to
//  active teams according to the mapping below.
// ============================================================================
UCLASS(BlueprintType)
class SUBMARINEPROJECT_API UTeamSpawnSettings : public UDataAsset
{
    GENERATED_BODY()

public:

    /**
     * Maximum number of teams this map supports.
     * Locator groups >= MaxTeamCount are filtered out unless
     * they are explicitly listed in overflow mappings.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpawnMapping",
        meta = (ClampMin = "2", ClampMax = "8"))
    int32 MaxTeamCount = 8;

    /**
     * Complete mapping table.
     * One entry per (TeamIndex, TeamsInPlay) combination.
     *
     * Example entries:
     *   (Team=0, TeamsInPlay=2) : [0, 2, 4, 5]
     *   (Team=1, TeamsInPlay=2) : [1, 3, 6, 7]
     *   (Team=0, TeamsInPlay=4) : [0, 4, 5]
     *   (Team=1, TeamsInPlay=4) : [1, 4, 6]
     *   (Team=0, TeamsInPlay=8) : [0]
     *   etc.
     *
     * If an entry is missing, GetLocatorGroupsForTeam() falls back to
     * [TeamIndex] (home group only).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnMapping")
    TArray<FTeamLocatorGroupMapping> Mappings;

    // -----------------------------------------------------------------------
    //  Runtime API
    // -----------------------------------------------------------------------

    /**
     * Get the ordered list of locator groups for a given team.
     *
     * @param TeamIndex   Team to query (0-based)
     * @param TeamsInPlay Number of active teams in this match
     * @return            Locator group indices in priority order (highest first)
     *                    Falls back to [TeamIndex] if no mapping found.
     */
    UFUNCTION(BlueprintPure, Category = "SpawnMapping")
    TArray<int32> GetLocatorGroupsForTeam(int32 TeamIndex, int32 TeamsInPlay) const
    {
        for (const FTeamLocatorGroupMapping& M : Mappings)
        {
            if (M.TeamIndex == TeamIndex && M.TeamsInPlay == TeamsInPlay)
                return M.LocatorGroups;
        }

        // Fallback: home group only
        TArray<int32> Fallback;
        Fallback.Add(TeamIndex);
        UE_LOG(LogTemp, Verbose,
            TEXT("[TeamSpawnSettings] No mapping for Team=%d TeamsInPlay=%d. "
                "Using home group only."),
            TeamIndex, TeamsInPlay);
        return Fallback;
    }

    /* Find Mapping Based on the TeamIndex and the number of TeamsInPlay*/
    const FTeamLocatorGroupMapping* FindMapping(int32 TeamIndex, int32 TeamsInPlay) const
    {
        for (const FTeamLocatorGroupMapping& M : Mappings)
            if (M.TeamIndex == TeamIndex && M.TeamsInPlay == TeamsInPlay)
                return &M;
        return nullptr;
    }

    // -----------------------------------------------------------------------
    //  Editor helpers
    // -----------------------------------------------------------------------

    /**
     * Generate default mappings for all team counts (2-8).
     * Fills in sensible defaults using the standard split pattern:
     *
     *   2 teams:  Team N uses groups [N, N+2, N+4, N+6] (mod MaxTeamCount)
     *   4 teams:  Team N uses groups [N, N+4] (mod MaxTeamCount)
     *   8 teams:  Team N uses group [N] only
     *
     * Call from the editor (Blueprint callable) to auto-populate.
     * Safe to call on an empty asset.
     */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "SpawnMapping")
    void GenerateDefaultMappings();
};