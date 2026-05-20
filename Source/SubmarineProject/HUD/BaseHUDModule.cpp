// Fill out your copyright notice in the Description page of Project Settings.

#include "HUD/BaseHUDModule.h"
#include "SubmarineHUDSettings.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"

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

    // Propagate null source to children so they unbind their own delegates
    PropagateDataSourceToChildren(TScriptInterface<ITrackableSubmarine>());
    ChildModules.Empty();

    UnbindFromDataSource();
    DataSource = nullptr;

    Super::NativeDestruct();
}

// ---------------------------------------------------------------------------
//  CreateChildModule
// ---------------------------------------------------------------------------
UBaseHUDModule* UBaseHUDModule::CreateChildModule(
    TSubclassOf<UBaseHUDModule> ModuleClass,
    UCanvasPanel* TargetCanvas,
    FVector2D NormalizedPos,
    FVector2D NormalizedSize,
    FVector2D CanvasSize,
    const FHUDModuleConfig& ChildConfig)
{
    if (!ModuleClass)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[HUDModule] CreateChildModule: null ModuleClass for parent '%s'"),
            *Config.ModuleName.ToString());
        return nullptr;
    }

    if (!TargetCanvas)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[HUDModule] CreateChildModule: null TargetCanvas for parent '%s'"),
            *Config.ModuleName.ToString());
        return nullptr;
    }

    UBaseHUDModule* Child = CreateWidget<UBaseHUDModule>(
        GetOwningPlayer(), ModuleClass);

    if (!Child)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[HUDModule] CreateChildModule: CreateWidget returned null for class '%s' (parent '%s')"),
            *ModuleClass->GetName(), *Config.ModuleName.ToString());
        return nullptr;
    }

    // Forward debug settings
    Child->DebugSettings = DebugSettings;

    // Add to canvas
    UCanvasPanelSlot* CanvaSlot = TargetCanvas->AddChildToCanvas(Child);
    if (!CanvaSlot)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[HUDModule] CreateChildModule: AddChildToCanvas returned null slot for '%s'"),
            *ChildConfig.ModuleName.ToString());
        return nullptr;
    }

    // Apply layout: convert normalized values to absolute pixels
    const FVector2D AbsPos = NormalizedPos * CanvasSize;
    const FVector2D AbsSize = NormalizedSize * CanvasSize;

    CanvaSlot->SetPosition(AbsPos);
    CanvaSlot->SetSize(AbsSize);
    CanvaSlot->SetAutoSize(false);
    CanvaSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
    CanvaSlot->SetAlignment(FVector2D::ZeroVector);

    // Configure child
    Child->SetConfig(ChildConfig);

    // Register
    ChildModules.Add(Child);

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleCreation : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HUDModule] CreateChildModule: created '%s' at (%.2f,%.2f) size (%.2f,%.2f) (parent '%s')"),
            *ChildConfig.ModuleName.ToString(),
            AbsPos.X, AbsPos.Y, AbsSize.X, AbsSize.Y,
            *Config.ModuleName.ToString());

    return Child;
}

// ---------------------------------------------------------------------------
//  PropagateDataSourceToChildren
// ---------------------------------------------------------------------------
void UBaseHUDModule::PropagateDataSourceToChildren(
    const TScriptInterface<ITrackableSubmarine>& Source)
{
    for (UBaseHUDModule* Child : ChildModules)
    {
        if (IsValid(Child))
            Child->SetDataSource(Source);
    }
}