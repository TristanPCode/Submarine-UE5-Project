// Fill out your copyright notice in the Description page of Project Settings.

#include "HUD/BaseHUDModule.h"
#include "HUD/SubmarineHUDDebugSettings.h"

// ---------------------------------------------------------------------------
//  SetConfig
// ---------------------------------------------------------------------------
void UBaseHUDModule::SetConfig(const FHUDModuleConfig& InConfig)
{
    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleSetConfig : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HUDModule] SetConfig  Module='%s'  Enabled=%d"),
            *InConfig.ModuleName.ToString(), InConfig.bEnabled ? 1 : 0);

    Config = InConfig;
    RefreshVisuals();
}

// ---------------------------------------------------------------------------
//  SetDataSource
// ---------------------------------------------------------------------------
void UBaseHUDModule::SetDataSource(const TScriptInterface<ITrackableSubmarine>& NewSource)
{
    const FString OldName = IsValid(DataSource.GetObject())
        ? DataSource.GetObject()->GetName() : TEXT("None");
    const FString NewName = IsValid(NewSource.GetObject())
        ? NewSource.GetObject()->GetName() : TEXT("None");

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleSetDataSource : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HUDModule] SetDataSource  Module='%s'  Old='%s'  New='%s'"),
            *Config.ModuleName.ToString(), *OldName, *NewName);

    // Step 1 -- unbind from old source
    UnbindFromDataSource();

    // Step 2 -- disable tick (BindToDataSource will re-enable if needed)
    if (bContinuousTickEnabled)
        SetContinuousTickEnabled(false);

    // Step 3 -- store new source
    DataSource = NewSource;

    // Step 4 -- bind to new source
    BindToDataSource();

    // Step 5 -- immediate visual refresh
    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleRefresh : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HUDModule] RefreshVisuals (after SetDataSource)  Module='%s'"),
            *Config.ModuleName.ToString());

    RefreshVisuals();
}

// ---------------------------------------------------------------------------
//  SetContinuousTickEnabled
// ---------------------------------------------------------------------------
void UBaseHUDModule::SetContinuousTickEnabled(bool bEnabled)
{
    bContinuousTickEnabled = bEnabled;

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleTick : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HUDModule] SetContinuousTickEnabled  Module='%s'  Enabled=%d"),
            *Config.ModuleName.ToString(), bEnabled ? 1 : 0);
}

// ---------------------------------------------------------------------------
//  NativeTick
//  Only active when bContinuousTickEnabled is true.
// ---------------------------------------------------------------------------
void UBaseHUDModule::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    if (!bContinuousTickEnabled) return;
    if (!IsValid(DataSource.GetObject())) return;

    // Subclass tick logic goes here via override
}

// ---------------------------------------------------------------------------
//  NativeDestruct
// ---------------------------------------------------------------------------
void UBaseHUDModule::NativeDestruct()
{
    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleDestruct : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HUDModule] NativeDestruct  Module='%s'"),
            *Config.ModuleName.ToString());

    UnbindFromDataSource();
    DataSource = nullptr;

    Super::NativeDestruct();
}