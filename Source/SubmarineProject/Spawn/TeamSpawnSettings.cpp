// Fill out your copyright notice in the Description page of Project Settings.

#include "Spawn/TeamSpawnSettings.h"

// ---------------------------------------------------------------------------
//  GenerateDefaultMappings
// ---------------------------------------------------------------------------
void UTeamSpawnSettings::GenerateDefaultMappings()
{
    Mappings.Empty();

    // For each supported team count (2..MaxTeamCount):
    for (int32 TeamsInPlay = 2; TeamsInPlay <= MaxTeamCount; ++TeamsInPlay)
    {
        for (int32 TeamIdx = 0; TeamIdx < TeamsInPlay; ++TeamIdx)
        {
            FTeamLocatorGroupMapping Entry;
            Entry.TeamIndex = TeamIdx;
            Entry.TeamsInPlay = TeamsInPlay;

            // Home group is always first (highest priority)
            Entry.LocatorGroups.Add(TeamIdx);
            Entry.GroupPriorities.Add(0);   // tier 0 = home, highest priority

            // Add other team groups that are "unused" in this team count.
            // Pattern: interleave groups from the upper half of [0, MaxTeamCount).
            // For each step size = TeamsInPlay:
            //   add groups at TeamIdx + TeamsInPlay, TeamIdx + 2*TeamsInPlay, ...
            //   (all mod MaxTeamCount, skip if already added)
            for (int32 Step = TeamsInPlay; Step < MaxTeamCount; Step += TeamsInPlay)
            {
                const int32 Group = (TeamIdx + Step) % MaxTeamCount;
                if (!Entry.LocatorGroups.Contains(Group))
                {
                    Entry.LocatorGroups.Add(Group);
                    Entry.GroupPriorities.Add(1);   // tier 1 = fallback
                }
            }

            Mappings.Add(Entry);
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[TeamSpawnSettings] GenerateDefaultMappings: generated %d entries "
            "for MaxTeamCount=%d"),
        Mappings.Num(), MaxTeamCount);

#if WITH_EDITOR
    MarkPackageDirty();
#endif
}