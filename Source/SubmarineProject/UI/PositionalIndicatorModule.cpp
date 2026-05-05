// Fill out your copyright notice in the Description page of Project Settings.

#include "PositionalIndicatorModule.h"
#include "HUD/SubmarineHUDSettings.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"

// ---------------------------------------------------------------------------
//  NativeOnInitialized
//
//  Called by UMG after the widget hierarchy is fully constructed and the
//  first layout pass has completed. This is the earliest safe point to
//  read widget geometry.
// ---------------------------------------------------------------------------
void UPositionalIndicatorModule::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleBinding : false))
        UE_LOG(LogTemp, Log,
            TEXT("[PositionalIndicator] NativeOnInitialized  Module='%s'"),
            *Config.ModuleName.ToString());

    // Apply any positions that were requested before layout was ready
    ApplyPositionsIfReady();
}

// ---------------------------------------------------------------------------
//  RefreshVisuals_Implementation
// ---------------------------------------------------------------------------
void UPositionalIndicatorModule::RefreshVisuals_Implementation()
{
    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleRefresh : false))
        UE_LOG(LogTemp, Log,
            TEXT("[PositionalIndicator] RefreshVisuals  Module='%s'"),
            *Config.ModuleName.ToString());

    ApplyStaticTextures();

    // Re-apply pending positions in case canvas size changed (e.g. after re-init)
    if (bHasPendingUpdate)
        ApplyPositionsIfReady();
}

// ---------------------------------------------------------------------------
//  UpdateElementPositions
//
//  Called by subclasses whenever the crank/metal positions need to change.
//  Stores the pixel values and converts them to screen positions if the
//  canvas size is known. Otherwise stores as pending.
// ---------------------------------------------------------------------------
void UPositionalIndicatorModule::UpdateElementPositions(
    float CrankY_px, bool bInvertCrank,
    float MetalY_px, bool bInvertMetal)
{
    // Always store as pending so positions survive a re-layout
    PendingCrankY_px = CrankY_px;
    PendingMetalY_px = MetalY_px;
    bPendingInvertCrank = bInvertCrank;
    bPendingInvertMetal = bInvertMetal;
    bHasPendingUpdate = true;

    ApplyPositionsIfReady();
}

// ---------------------------------------------------------------------------
//  ApplyPositionsIfReady
// ---------------------------------------------------------------------------
void UPositionalIndicatorModule::ApplyPositionsIfReady()
{
    if (!bHasPendingUpdate) return;
    if (!LayerCanvas) return;

    // Get current rendered size of the canvas
    const FGeometry& Geo = LayerCanvas->GetCachedGeometry();
    const FVector2D CanvasSize = Geo.GetLocalSize();

    // Canvas size is zero before the first layout pass -- defer
    if (CanvasSize.IsNearlyZero())
    {
        if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleLayout : false))
            UE_LOG(LogTemp, Log,
                TEXT("[PositionalIndicator] ApplyPositionsIfReady: "
                    "canvas size not yet available, deferring (Module='%s')"),
                *Config.ModuleName.ToString());
        return;
    }

    CachedCanvasSize = CanvasSize;

    const float TexH = Config.GetFloat(HUDConfigKeys::TextureHeight, 1.f);
    const float CrankX_px = Config.GetFloat(HUDConfigKeys::CrankOffsetX, 0.f);

    // Convert pixel offsets to ratios, then to actual screen pixels
    auto ToScreenY = [&](float PixelOffset, bool bInvert) -> float
        {
            float Ratio = PixelOffset / FMath::Max(TexH, 1.f);
            if (bInvert) Ratio = 1.f - Ratio;
            return Ratio * CanvasSize.Y;
        };

    const float CrankX_screen = (CrankX_px / FMath::Max(TexH, 1.f)) * CanvasSize.X;
    const float CrankY_screen = ToScreenY(PendingCrankY_px, bPendingInvertCrank);
    const float MetalY_screen = ToScreenY(PendingMetalY_px, bPendingInvertMetal);

    SetCanvasSlotPosition(CrankImage, FVector2D(CrankX_screen, CrankY_screen));
    SetCanvasSlotPosition(MetalImage, FVector2D(0.f, MetalY_screen));

    if (ShouldLog(DebugSettings ? DebugSettings->bLogModuleLayout : false))
        UE_LOG(LogTemp, Log,
            TEXT("[PositionalIndicator] Applied positions  Module='%s'  "
                "Canvas=(%.0f,%.0f)  Crank=(%.1f,%.1f)  Metal=(0,%.1f)"),
            *Config.ModuleName.ToString(),
            CanvasSize.X, CanvasSize.Y,
            CrankX_screen, CrankY_screen, MetalY_screen);

    bHasPendingUpdate = false;
}

// ---------------------------------------------------------------------------
//  ApplyStaticTextures
// ---------------------------------------------------------------------------
void UPositionalIndicatorModule::ApplyStaticTextures()
{
    if (BackgroundImage)
    {
        if (UTexture2D* Tex = Config.GetTexture(HUDConfigKeys::Background))
            BackgroundImage->SetBrushFromTexture(Tex, false);
    }
    if (CrankImage)
    {
        if (UTexture2D* Tex = Config.GetTexture(HUDConfigKeys::Crank))
            CrankImage->SetBrushFromTexture(Tex, false);
    }
    if (MetalImage)
    {
        if (UTexture2D* Tex = Config.GetTexture(HUDConfigKeys::Metal))
            MetalImage->SetBrushFromTexture(Tex, false);
    }
}

// ---------------------------------------------------------------------------
//  SetCanvasSlotPosition
// ---------------------------------------------------------------------------
bool UPositionalIndicatorModule::SetCanvasSlotPosition(UWidget* Widget, FVector2D Position)
{
    if (!Widget) return false;

    UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot);
    if (!Slot) return false;

    Slot->SetPosition(Position);
    return true;
}