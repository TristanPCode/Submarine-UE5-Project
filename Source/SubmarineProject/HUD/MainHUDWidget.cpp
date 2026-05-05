// Fill out your copyright notice in the Description page of Project Settings.

#include "HUD/MainHUDWidget.h"
#include "HUD/BaseHUDModule.h"
#include "HUD/SubmarineHUDSettings.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetTree.h"

// ---------------------------------------------------------------------------
//  InitializeHUD
// ---------------------------------------------------------------------------
void UMainHUDWidget::InitializeHUD(USubmarineHUDSettings* Settings)
{
    if (!Settings)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[MainHUDWidget] InitializeHUD: null Settings -- HUD not built"));
        return;
    }

    if (!RootCanvas)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[MainHUDWidget] InitializeHUD: RootCanvas is null. "
                "Ensure your Blueprint has a UCanvasPanel named 'RootCanvas'."));
        return;
    }

    // Cache debug settings for all subsequent log calls
    DebugSettings = Settings->DebugSettings;

    // Tear down any existing modules
    TearDownModules();

    CachedSettings = Settings;

    if (ShouldLog(DebugSettings->bLogHUDInitialization))
        UE_LOG(LogTemp, Log,
            TEXT("[MainHUDWidget] InitializeHUD: %d module entries in DataAsset"),
            Settings->Modules.Num());

    int32 CreatedCount = 0;
    int32 SkippedCount = 0;
    int32 FailedCount = 0;

    for (const FHUDModuleConfig& ModuleConfig : Settings->Modules)
    {
        if (!ModuleConfig.bEnabled)
        {
            if (ShouldLog(DebugSettings->bLogModuleCreation))
                UE_LOG(LogTemp, Log,
                    TEXT("[MainHUDWidget]   SKIP (disabled): '%s'"),
                    *ModuleConfig.ModuleName.ToString());
            ++SkippedCount;
            continue;
        }

        UBaseHUDModule* Module = CreateAndAddModule(ModuleConfig);
        if (!Module)
        {
            ++FailedCount;
            continue;
        }

        ActiveModules.Add(Module);
        ModuleNames.Add(ModuleConfig.ModuleName);
        ++CreatedCount;
    }

    if (ShouldLog(DebugSettings->bLogHUDInitialization))
        UE_LOG(LogTemp, Log,
            TEXT("[MainHUDWidget] InitializeHUD complete: Created=%d  Skipped=%d  Failed=%d"),
            CreatedCount, SkippedCount, FailedCount);

    // Reapply cached data source if one was set before re-init
    if (CachedDataSource.GetObject())
    {
        if (ShouldLog(DebugSettings->bLogDataSourcePropagation))
            UE_LOG(LogTemp, Log,
                TEXT("[MainHUDWidget] Re-applying cached DataSource '%s' to %d new modules"),
                *CachedDataSource.GetObject()->GetName(), ActiveModules.Num());
        SetDataSource(CachedDataSource);
    }
}

// ---------------------------------------------------------------------------
//  SetDataSource
// ---------------------------------------------------------------------------
void UMainHUDWidget::SetDataSource(const TScriptInterface<ITrackableSubmarine>& NewSource)
{
    CachedDataSource = NewSource;

    const FString SourceName = IsValid(NewSource.GetObject())
        ? NewSource.GetObject()->GetName() : TEXT("None");

    if (ShouldLog(DebugSettings ? DebugSettings->bLogDataSourcePropagation : false))
        UE_LOG(LogTemp, Log,
            TEXT("[MainHUDWidget] SetDataSource '%s' -> propagating to %d modules"),
            *SourceName, ActiveModules.Num());

    for (UBaseHUDModule* Module : ActiveModules)
    {
        if (IsValid(Module))
            Module->SetDataSource(NewSource);
        else if (ShouldLog(DebugSettings ? DebugSettings->bLogDataSourcePropagation : false))
            UE_LOG(LogTemp, Warning,
                TEXT("[MainHUDWidget]   Skipping invalid module during SetDataSource"));
    }
}

// ---------------------------------------------------------------------------
//  FindModule
// ---------------------------------------------------------------------------
UBaseHUDModule* UMainHUDWidget::FindModule(FName ModuleName) const
{
    for (int32 i = 0; i < ModuleNames.Num(); ++i)
    {
        if (ModuleNames[i] == ModuleName && ActiveModules.IsValidIndex(i))
            return ActiveModules[i];
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
//  NativeDestruct
// ---------------------------------------------------------------------------
void UMainHUDWidget::NativeDestruct()
{
    TearDownModules();
    Super::NativeDestruct();
}

// ---------------------------------------------------------------------------
//  CreateAndAddModule
// ---------------------------------------------------------------------------
UBaseHUDModule* UMainHUDWidget::CreateAndAddModule(const FHUDModuleConfig& ModuleConfig)
{
    if (!ModuleConfig.ModuleClass)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[MainHUDWidget] CreateAndAddModule: ModuleClass is null for '%s' -- skipping"),
            *ModuleConfig.ModuleName.ToString());
        return nullptr;
    }

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleCreation : false))
        UE_LOG(LogTemp, Log,
            TEXT("[MainHUDWidget]   Creating module '%s' (class: %s)"),
            *ModuleConfig.ModuleName.ToString(),
            *ModuleConfig.ModuleClass->GetName());

    UBaseHUDModule* Module = CreateWidget<UBaseHUDModule>(
        GetOwningPlayer(), ModuleConfig.ModuleClass);

    if (!Module)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[MainHUDWidget]   CreateWidget returned null for '%s'"),
            *ModuleConfig.ModuleName.ToString());
        return nullptr;
    }

    // Pass debug settings down to the module before anything else
    Module->DebugSettings = DebugSettings;

    UCanvasPanelSlot* CanvasSlot = RootCanvas->AddChildToCanvas(Module);
    if (!CanvasSlot)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[MainHUDWidget]   AddChildToCanvas returned null slot for '%s'"),
            *ModuleConfig.ModuleName.ToString());
        return nullptr;
    }

    ApplyLayoutToSlot(CanvasSlot, ModuleConfig);

    // SetConfig triggers RefreshVisuals with no data source yet (safe)
    Module->SetConfig(ModuleConfig);

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleCreation : false))
        UE_LOG(LogTemp, Log,
            TEXT("[MainHUDWidget]   Module '%s' created and added to canvas successfully"),
            *ModuleConfig.ModuleName.ToString());

    return Module;
}

// ---------------------------------------------------------------------------
//  ApplyLayoutToSlot
// ---------------------------------------------------------------------------
void UMainHUDWidget::ApplyLayoutToSlot(UCanvasPanelSlot* CanvasSlot,
    const FHUDModuleConfig& Config)
{
    if (!CanvasSlot) return;

    CanvasSlot->SetAnchors(Config.Anchors);
    CanvasSlot->SetAlignment(Config.Pivot);
    CanvasSlot->SetPosition(Config.PositionOffset);

    const bool bAutoSize = Config.SizeOverride.IsNearlyZero();
    CanvasSlot->SetAutoSize(bAutoSize);
    if (!bAutoSize)
        CanvasSlot->SetSize(Config.SizeOverride);
}

// ---------------------------------------------------------------------------
//  TearDownModules
// ---------------------------------------------------------------------------
void UMainHUDWidget::TearDownModules()
{
    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleTearDown : false))
        UE_LOG(LogTemp, Log,
            TEXT("[MainHUDWidget] TearDownModules: destroying %d modules"),
            ActiveModules.Num());

    // Disconnect data source from all modules (triggers UnbindFromDataSource)
    for (UBaseHUDModule* Module : ActiveModules)
    {
        if (IsValid(Module))
            Module->SetDataSource(TScriptInterface<ITrackableSubmarine>());
    }

    // Remove from canvas
    if (RootCanvas)
    {
        for (UBaseHUDModule* Module : ActiveModules)
        {
            if (IsValid(Module))
                RootCanvas->RemoveChild(Module);
        }
    }

    ActiveModules.Empty();
    ModuleNames.Empty();
}