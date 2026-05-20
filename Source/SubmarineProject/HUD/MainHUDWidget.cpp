// Fill out your copyright notice in the Description page of Project Settings.

#include "HUD/MainHUDWidget.h"
#include "HUD/BaseHUDModule.h"
#include "HUD/SubmarineHUDSettings.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetTree.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"

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

    if (ShouldLog(DebugSettings ? DebugSettings->bLogHUDInitialization : false))
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
            if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleCreation : false))
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

    if (ShouldLog(DebugSettings ? DebugSettings->bLogHUDInitialization : false))
        UE_LOG(LogTemp, Log,
            TEXT("[MainHUDWidget] InitializeHUD complete: Created=%d  Skipped=%d  Failed=%d"),
            CreatedCount, SkippedCount, FailedCount);

    // Reapply cached data source if one was set before re-init
    if (CachedDataSource.GetObject())
    {
        if (ShouldLog(DebugSettings ? DebugSettings->bLogDataSourcePropagation : false))
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

    const FVector2D DesRes = CachedSettings
        ? CachedSettings->DesignResolution : FVector2D(1920.f, 1080.f);
    ApplyLayoutToSlot(CanvasSlot, ModuleConfig, GetOwningPlayer(), DebugSettings, DesRes);

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
    const FHUDModuleConfig& Config, APlayerController* OwningPlayer, USubmarineHUDDebugSettings* InDebugSettings,FVector2D InDesignResolution)
{
    if (!CanvasSlot) return;

    /*CanvasSlot->SetAnchors(Config.Anchors);
    CanvasSlot->SetAlignment(Config.Pivot);
    CanvasSlot->SetPosition(Config.PositionOffset);

    const bool bAutoSize = Config.SizeOverride.IsNearlyZero();
    CanvasSlot->SetAutoSize(bAutoSize);
    if (!bAutoSize)
        CanvasSlot->SetSize(Config.SizeOverride);*/

    CanvasSlot->SetAnchors(Config.Anchors);
    CanvasSlot->SetAlignment(Config.Pivot);

    const bool bIsStretch = !Config.Anchors.Minimum.Equals(Config.Anchors.Maximum, KINDA_SMALL_NUMBER);
    if (!bIsStretch)
    {
        // ---- POINT ANCHOR MODE ----
        //
        // Key insight: we manually resolve the 0..1 anchor to absolute pixels
        // against the KNOWN HUD design resolution (1920x1080), then lock the
        // canvas slot anchors to (0,0,0,0) and use SetPosition with those
        // absolute pixels.
        //
        // This is the fix for the bug you observed:
        // When UMG resolves anchor points it uses the PARENT canvas size.
        // If the canvas slot's anchors are non-zero, UMG multiplies by the
        // parent's ALLOCATED size for that widget -- which may be the widget's
        // own desired/intrinsic size (from the BP canvas), not 1920x1080.
        // By doing the math ourselves and passing absolute pixels with a
        // (0,0,0,0) anchor, we remove that ambiguity entirely.

        // Design resolution: positions are authored against HUDSize pixels.
        // Read from the DA (CachedSettings->DesignResolution) so it's configurable
        // without recompiling. Default is 1920x1080.
        //
        // UMG scales this canvas to fit each player's actual viewport automatically
        // via AddToPlayerScreen. In split-screen, UMG handles the half-viewport.
        // In PIE the viewport is your editor window size -- positions scale accordingly.
        const FVector2D HUDSize = InDesignResolution.IsNearlyZero()
            ? FVector2D(1920.f, 1080.f) : InDesignResolution;


        // Anchor expressed as absolute screen pixels
        const FVector2D AnchorPx = Config.Anchors.Minimum * HUDSize;

        // Final position = resolved anchor + manual pixel nudge
        const FVector2D FinalPos = AnchorPx + Config.PositionOffset;

        FVector2D FinalSize = Config.SizeOverride;
        if (FinalSize.IsNearlyZero())
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[MainHUDWidget] Layout '%s': SizeOverride is (0,0). "
                    "Set SizeOverride to your texture's pixel size (e.g. 1078,64). "
                    "Using fallback (100,100)."),
                *Config.ModuleName.ToString());
            FinalSize = FVector2D(100.f, 100.f);
        }

        // Lock anchors to top-left origin -- all positioning is via SetPosition
        CanvasSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
        CanvasSlot->SetAlignment(Config.Pivot);
        CanvasSlot->SetAutoSize(false);
        CanvasSlot->SetPosition(FinalPos);
        CanvasSlot->SetSize(FinalSize);
    }
    else
    {
        // ---- STRETCH ANCHOR MODE ----
        // Let UMG handle anchor-relative sizing. Use FMargin offsets.
        FMargin Offsets;
        Offsets.Left = Config.PositionOffset.X;
        Offsets.Top = Config.PositionOffset.Y;
        Offsets.Right = Config.SizeOverride.X;
        Offsets.Bottom = Config.SizeOverride.Y;

        CanvasSlot->SetAnchors(Config.Anchors);
        CanvasSlot->SetAlignment(Config.Pivot);
        CanvasSlot->SetOffsets(Offsets);
    }

    // Apply ZOrder so HUD modules render above billboard widgets
    CanvasSlot->SetZOrder(Config.ZOrder);

    // Log to verify exact pixel positions at startup
    if (IsValid(InDebugSettings) && InDebugSettings->bLogModuleLayout)
        UE_LOG(LogTemp, Log,
            TEXT("[MainHUDWidget] Layout '%s'  Mode=%s  "
                "NormAnchor=(%.3f,%.3f)  Pivot=(%.2f,%.2f)  "
                "FinalPos=(%.1f,%.1f)  Size=(%.1f,%.1f)"),
            *Config.ModuleName.ToString(),
            bIsStretch ? TEXT("STRETCH") : TEXT("POINT"),
            Config.Anchors.Minimum.X, Config.Anchors.Minimum.Y,
            Config.Pivot.X, Config.Pivot.Y,
            CanvasSlot->GetPosition().X, CanvasSlot->GetPosition().Y,
            CanvasSlot->GetSize().X, CanvasSlot->GetSize().Y);
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