// Fill out your copyright notice in the Description page of Project Settings.

#include "Match/RuntimeMatchSettings.h"
#include "HUD/HUDGlobalDefaults.h"

// ---------------------------------------------------------------------------
//  CreateFromDataAsset
// ---------------------------------------------------------------------------
URuntimeMatchSettings* URuntimeMatchSettings::CreateFromDataAsset(
    UObject* Outer,
    const UMatchSettingsDataAsset* SourceDA)
{
    URuntimeMatchSettings* RMS = NewObject<URuntimeMatchSettings>(Outer);

    if (!SourceDA)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[RuntimeMatchSettings] CreateFromDataAsset: SourceDA is null. "
                "Using all-default settings."));
        return RMS;
    }

    RMS->LocalPlayerCount = SourceDA->LocalPlayerCount;
    RMS->RemotePlayerCount = SourceDA->RemotePlayerCount;
    RMS->PlayerNames = SourceDA->PlayerNames;
    RMS->PlayerLevels = SourceDA->PlayerLevels;
    RMS->PlayerSubmarineClasses = SourceDA->PlayerSubmarineClasses;
    RMS->InputMappings = SourceDA->InputMappings;
    RMS->bCPUEnabled = SourceDA->bCPUEnabled;
    RMS->CPUCount = SourceDA->CPUCount;
    RMS->CPUNames = SourceDA->CPUNames;
    RMS->CPULevels = SourceDA->CPULevels;
    RMS->CPUSubmarineClasses = SourceDA->CPUSubmarineClasses;
    RMS->DefaultSubmarineClass = SourceDA->DefaultSubmarineClass;
    RMS->TeamCount = SourceDA->TeamCount;
    RMS->Teams = SourceDA->Teams;
    RMS->PlayerTeamAssignments = SourceDA->PlayerTeamAssignments;
    RMS->CPUTeamAssignments = SourceDA->CPUTeamAssignments;
    RMS->MaxSubmarineCount = SourceDA->MaxSubmarineCount;
    RMS->bSplitScreenEnabled = SourceDA->bSplitScreenEnabled;
    RMS->HUDContextOverrides = SourceDA->HUDContextOverrides;

    return RMS;
}

// ---------------------------------------------------------------------------
//  ResolveHUDSettings
// ---------------------------------------------------------------------------
USubmarineHUDSettings* URuntimeMatchSettings::ResolveHUDSettings(
    EHUDContext Context,
    const UHUDGlobalDefaults* GlobalDefaults) const
{
    // 1. Match-specific override
    const TObjectPtr<USubmarineHUDSettings>* Override =
        HUDContextOverrides.Find(Context);
    if (Override && Override->Get())
        return Override->Get();

    // 2. Global project default
    if (GlobalDefaults)
    {
        USubmarineHUDSettings* Global = GlobalDefaults->Resolve(Context);
        if (Global) return Global;
    }

    // 3. Nothing registered — caller handles nullptr (valid for Cinematic etc.)
    return nullptr;
}

// ---------------------------------------------------------------------------
//  GetPlayerSubmarineClass
// ---------------------------------------------------------------------------
TSubclassOf<ASubmarinePawn> URuntimeMatchSettings::GetPlayerSubmarineClass(
    int32 LocalPlayerIndex) const
{
    if (PlayerSubmarineClasses.IsValidIndex(LocalPlayerIndex)
        && PlayerSubmarineClasses[LocalPlayerIndex])
        return PlayerSubmarineClasses[LocalPlayerIndex];

    if (DefaultSubmarineClass)
        return DefaultSubmarineClass;

    UE_LOG(LogTemp, Warning,
        TEXT("[RuntimeMatchSettings] GetPlayerSubmarineClass: no class for "
            "LocalPlayerIndex=%d and no DefaultSubmarineClass set."),
        LocalPlayerIndex);
    return nullptr;
}

// ---------------------------------------------------------------------------
//  GetCPUSubmarineClass
// ---------------------------------------------------------------------------
TSubclassOf<ASubmarinePawn> URuntimeMatchSettings::GetCPUSubmarineClass(
    int32 CPUIndex) const
{
    if (CPUSubmarineClasses.IsValidIndex(CPUIndex)
        && CPUSubmarineClasses[CPUIndex])
        return CPUSubmarineClasses[CPUIndex];

    if (DefaultSubmarineClass)
        return DefaultSubmarineClass;

    UE_LOG(LogTemp, Warning,
        TEXT("[RuntimeMatchSettings] GetCPUSubmarineClass: no class for "
            "CPUIndex=%d and no DefaultSubmarineClass set."),
        CPUIndex);
    return nullptr;
}

// ---------------------------------------------------------------------------
//  GetPlayerName
// ---------------------------------------------------------------------------
FString URuntimeMatchSettings::GetPlayerName(int32 LocalPlayerIndex) const
{
    if (PlayerNames.IsValidIndex(LocalPlayerIndex)
        && !PlayerNames[LocalPlayerIndex].IsEmpty())
        return PlayerNames[LocalPlayerIndex];

    return FString::Printf(TEXT("Player %d"), LocalPlayerIndex + 1);
}

// ---------------------------------------------------------------------------
//  GetCPUName
// ---------------------------------------------------------------------------
FString URuntimeMatchSettings::GetCPUName(int32 CPUIndex) const
{
    if (CPUNames.IsValidIndex(CPUIndex) && !CPUNames[CPUIndex].IsEmpty())
        return CPUNames[CPUIndex];

    return FString::Printf(TEXT("CPU %d"), CPUIndex + 1);
}

// ---------------------------------------------------------------------------
//  GetTeamSettings
// ---------------------------------------------------------------------------
FMatchTeamSettings URuntimeMatchSettings::GetTeamSettings(int32 TeamIndex) const
{
    if (Teams.IsValidIndex(TeamIndex))
        return Teams[TeamIndex];

    FMatchTeamSettings Default;
    Default.TeamName = FString::Printf(TEXT("Team %d"), TeamIndex + 1);
    return Default;
}

// ---------------------------------------------------------------------------
//  GetTotalSubmarineCount
// ---------------------------------------------------------------------------
int32 URuntimeMatchSettings::GetTotalSubmarineCount() const
{
    const int32 Total = LocalPlayerCount
        + RemotePlayerCount
        + (bCPUEnabled ? CPUCount : 0);
    return FMath::Min(Total, MaxSubmarineCount);
}