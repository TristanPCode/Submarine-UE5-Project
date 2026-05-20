// Fill out your copyright notice in the Description page of Project Settings.

#include "NormalAmmoModule.h"
#include "NumericDisplayModule.h"
#include "TorpedoIconModule.h"
#include "HUD/SubmarineHUDSettings.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "Components/CanvasPanel.h"

// ---------------------------------------------------------------------------
//  NativeOnInitialized
//  Called after the Blueprint hierarchy is constructed and the first layout
//  pass has run -- earliest safe point to read canvas geometry.
// ---------------------------------------------------------------------------
void UNormalAmmoModule::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogNormalAmmo) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NormalAmmo] NativeOnInitialized  Module='%s'"),
            *Config.ModuleName.ToString());
}

// ---------------------------------------------------------------------------
//  BindToDataSource
// ---------------------------------------------------------------------------
void UNormalAmmoModule::BindToDataSource()
{
    UObject* Obj = DataSource.GetObject();
    if (!IsValid(Obj))
    {
        if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogNormalAmmo) : false))
            UE_LOG(LogTemp, Warning,
                TEXT("[NormalAmmo] BindToDataSource: invalid DataSource"));
        return;
    }

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogNormalAmmo) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NormalAmmo] BindToDataSource: '%s'"), *Obj->GetName());

    // This module has no direct delegate bindings of its own --
    // children own their subscriptions.
    PropagateDataSourceToChildren(DataSource);
}

// ---------------------------------------------------------------------------
//  UnbindFromDataSource
// ---------------------------------------------------------------------------
void UNormalAmmoModule::UnbindFromDataSource()
{
    UObject* Obj = DataSource.GetObject();

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogNormalAmmo) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NormalAmmo] UnbindFromDataSource: '%s'"),
            IsValid(Obj) ? *Obj->GetName() : TEXT("None"));

    // Propagate null source -- children will unbind their own delegates
    PropagateDataSourceToChildren(TScriptInterface<ITrackableSubmarine>());
}

// ---------------------------------------------------------------------------
//  RefreshVisuals_Implementation
// ---------------------------------------------------------------------------
void UNormalAmmoModule::RefreshVisuals_Implementation()
{
    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogNormalAmmo) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NormalAmmo] RefreshVisuals: module='%s'  bChildrenCreated=%d  DataSource=%s  AmmoCanvas=%s"),
            *Config.ModuleName.ToString(), bChildrenCreated ? 1 : 0,
            IsValid(DataSource.GetObject()) ? *DataSource.GetObject()->GetName() : TEXT("None"),
            AmmoCanvas ? TEXT("OK") : TEXT("NULL"));

    // RefreshVisuals fires after SetConfig when Config is valid.
    // This is the correct place to create child modules.
    if (!bChildrenCreated)
        CreateChildModules();

    // Propagate data source to children if already bound
    if (bChildrenCreated && IsValid(DataSource.GetObject()))
        PropagateDataSourceToChildren(DataSource);
}

// ---------------------------------------------------------------------------
//  CreateChildModules
// ---------------------------------------------------------------------------
void UNormalAmmoModule::CreateChildModules()
{
    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogNormalAmmo) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NormalAmmo] CreateChildModules ENTER: module='%s'  bChildrenCreated=%d  AmmoCanvas=%s"),
            *Config.ModuleName.ToString(), bChildrenCreated ? 1 : 0,
            AmmoCanvas ? TEXT("OK") : TEXT("NULL"));

    if (bChildrenCreated) return;
    if (!AmmoCanvas)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[NormalAmmo] CreateChildModules: AmmoCanvas is null for '%s'. "
                "Ensure Blueprint has a UCanvasPanel named 'AmmoCanvas'."),
            *Config.ModuleName.ToString());
        return;
    }

    const FVector2D CanvasSize = AmmoCanvas->GetCachedGeometry().GetLocalSize();
    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleBinding && DebugSettings->bLogNormalAmmo) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NormalAmmo] CreateChildModules: AmmoCanvas size=(%.1f,%.1f)"),
            CanvasSize.X, CanvasSize.Y);

    if (CanvasSize.IsNearlyZero())
    {
        // Layout not ready yet -- NativeOnInitialized will call us again
        UE_LOG(LogTemp, Warning,
            TEXT("[NormalAmmo] CreateChildModules: canvas size is ZERO -- layout not ready yet, will retry on RefreshVisuals"));
        return;
    }

    // -----------------------------------------------------------------------
    //  Read normalized layout from config
    // -----------------------------------------------------------------------
    const FVector2D CounterPos = FVector2D(
        Config.GetFloat(HUDConfigKeys::CounterPosX, 0.f),
        Config.GetFloat(HUDConfigKeys::CounterPosY, 0.f));
    const FVector2D CounterSize = FVector2D(
        Config.GetFloat(HUDConfigKeys::CounterSizeX, 0.45f),
        Config.GetFloat(HUDConfigKeys::CounterSizeY, 0.83f));

    const FVector2D TorpedoPos = FVector2D(
        Config.GetFloat(HUDConfigKeys::TorpedoPosX, 0.45f),
        Config.GetFloat(HUDConfigKeys::TorpedoPosY, 0.f));
    const FVector2D TorpedoSize = FVector2D(
        Config.GetFloat(HUDConfigKeys::TorpedoSizeX, 0.55f),
        Config.GetFloat(HUDConfigKeys::TorpedoSizeY, 1.f));

    // -----------------------------------------------------------------------
    //  Determine child classes
    //  Counter uses NumericDisplayModule by default.
    //  Icon uses TorpedoIconModule by default.
    //  Both can be overridden in the DataAsset if a Blueprint subclass is needed.
    // -----------------------------------------------------------------------
    // Both child classes must be set to Blueprint subclasses in the DA.
    // Without BP subclasses, BindWidget members (DigitCanvas, IconImage) are null.
    TSubclassOf<UBaseHUDModule> CounterClass = Config.CounterModuleClass;

    if (!CounterClass)
    {
        CounterClass = UNumericDisplayModule::StaticClass();
    }

    TSubclassOf<UBaseHUDModule> IconClass = Config.SlotModuleClass;

    if (!IconClass)
    {
        IconClass = UTorpedoIconModule::StaticClass();
    }

    if (!Config.CounterModuleClass)
        UE_LOG(LogTemp, Warning,
            TEXT("[NormalAmmo] CounterModuleClass not set -- set it to BP_AmmoCounterModule_C in DA"));

    // -----------------------------------------------------------------------
    //  Create Counter child
    // -----------------------------------------------------------------------
    FHUDModuleConfig CounterCfg = MakeChildConfig(
        FName(*(Config.ModuleName.ToString() + TEXT("_Counter"))));

    UBaseHUDModule* CounterBase = CreateChildModule(
        CounterClass, AmmoCanvas,
        CounterPos, CounterSize, CanvasSize,
        CounterCfg);

    CounterModule = Cast<UNumericDisplayModule>(CounterBase);

    if (!CounterModule)
        UE_LOG(LogTemp, Warning,
            TEXT("[NormalAmmo] CreateChildModules: counter child creation failed for '%s'"),
            *Config.ModuleName.ToString());

    // -----------------------------------------------------------------------
    //  Create Icon child
    // -----------------------------------------------------------------------
    FHUDModuleConfig IconCfg = MakeChildConfig(
        FName(*(Config.ModuleName.ToString() + TEXT("_Icon"))));

    UBaseHUDModule* IconBase = CreateChildModule(
        IconClass, AmmoCanvas,
        TorpedoPos, TorpedoSize, CanvasSize,
        IconCfg);

    IconModule = Cast<UTorpedoIconModule>(IconBase);

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleCreation && DebugSettings->bLogNormalAmmo) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NormalAmmo] CreateChildModules: IconModule=%s"),
            IconModule ? TEXT("OK") : TEXT("FAILED -- check IconClass is valid and BP_TorpedoIconModule is set"));

    if (!IconModule && ShouldLog(DebugSettings ? (DebugSettings->bLogModuleCreation && DebugSettings->bLogNormalAmmo) : false))
        UE_LOG(LogTemp, Warning,
            TEXT("[NormalAmmo] CreateChildModules: icon child creation failed for '%s'"),
            *Config.ModuleName.ToString());

    bChildrenCreated = true;

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleCreation && DebugSettings->bLogNormalAmmo) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[NormalAmmo] CreateChildModules: done for '%s'  "
                "canvas=(%.0f,%.0f)"),
            *Config.ModuleName.ToString(), CanvasSize.X, CanvasSize.Y);
}

// ---------------------------------------------------------------------------
//  MakeChildConfig
//  Copies parent config and assigns the child a unique name.
//  Children share materials, textures, and floats from the parent.
// ---------------------------------------------------------------------------
FHUDModuleConfig UNormalAmmoModule::MakeChildConfig(FName ChildName) const
{
    FHUDModuleConfig Child = Config;  // full copy -- children inherit all params
    Child.ModuleName = ChildName;
    Child.ModuleClass = nullptr;      // child's class is passed separately
    Child.bEnabled = true;
    // SizeOverride / Anchors / PositionOffset are irrelevant for child modules
    // positioned manually by CreateChildModule -- clear to avoid confusion.
    Child.SizeOverride = FVector2D::ZeroVector;
    Child.PositionOffset = FVector2D::ZeroVector;
    Child.Anchors = FAnchors(0.f, 0.f, 0.f, 0.f);
    return Child;
}