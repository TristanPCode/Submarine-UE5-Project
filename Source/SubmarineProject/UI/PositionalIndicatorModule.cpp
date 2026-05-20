// Fill out your copyright notice in the Description page of Project Settings.

#include "PositionalIndicatorModule.h"
#include "HUD/SubmarineHUDSettings.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"

// ---------------------------------------------------------------------------
//  DiagnoseSlot  (local helper)
//  Prints everything UMG knows about a widget's slot so we can see
//  exactly what mode it is in and whether our SetAnchors call landed.
// ---------------------------------------------------------------------------
static void DiagnoseSlot(UImage* Image, const TCHAR* Label)
{
    if (!Image)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PosIndicator][DIAG] %s: Image ptr is NULL"), Label);
        return;
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[PosIndicator][DIAG] %s: Image='%s'  Slot class='%s'"),
        Label, *Image->GetName(),
        Image->Slot ? *Image->Slot->GetClass()->GetName() : TEXT("NULL SLOT"));

    UCanvasPanelSlot* CPS = Cast<UCanvasPanelSlot>(Image->Slot);
    if (!CPS)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PosIndicator][DIAG] %s: Cast to UCanvasPanelSlot FAILED -- "
                "widget is NOT a direct child of a CanvasPanel, "
                "or Slot has not been assigned yet (widget not yet added to panel)."),
            Label);
        return;
    }

    const FAnchors  A = CPS->GetAnchors();
    const FVector2D Pos = CPS->GetPosition();
    const FVector2D Sz = CPS->GetSize();
    const FMargin   Off = CPS->GetOffsets();
    const FVector2D Aln = CPS->GetAlignment();
}

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

    if (ShouldLog(DebugSettings ? DebugSettings->bLogPositionalIndicator : false))
        UE_LOG(LogTemp, Log,
            TEXT("[PositionalIndicator] NativeOnInitialized '%s' "
                "(Config not yet set -- slot init deferred to RefreshVisuals)"),
            *Config.ModuleName.ToString());

    //// Diagnose slots immediately -- are they available at this point?
    //DiagnoseSlot(CrankImage, TEXT("Crank@NativeOnInitialized"));
    //DiagnoseSlot(MetalImage, TEXT("Metal@NativeOnInitialized"));

    // If somehow a position was queued before init (shouldn't happen, but safe)
    ApplyPositionsIfReady();
}

// ---------------------------------------------------------------------------
//  RefreshVisuals_Implementation
// ---------------------------------------------------------------------------
void UPositionalIndicatorModule::RefreshVisuals_Implementation()
{
    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleRefresh && DebugSettings->bLogPositionalIndicator) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[PositionalIndicator] RefreshVisuals  Module='%s'"),
            *Config.ModuleName.ToString());

    //// Diagnose slots at RefreshVisuals time too
    //DiagnoseSlot(CrankImage, TEXT("Crank@RefreshVisuals"));
    //DiagnoseSlot(MetalImage, TEXT("Metal@RefreshVisuals"));

    // Re-initialize slot settings now that Config is populated
    InitMovingImageSlots();

    //// Diagnose again AFTER init to confirm the change landed
    //DiagnoseSlot(CrankImage, TEXT("Crank@AfterInit"));
    //DiagnoseSlot(MetalImage, TEXT("Metal@AfterInit"));

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
        if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleLayout && DebugSettings->bLogPositionalIndicator) : false))
            UE_LOG(LogTemp, Log,
                TEXT("[PositionalIndicator] ApplyPositionsIfReady: "
                    "canvas size not yet available, deferring (Module='%s')"),
                *Config.ModuleName.ToString());
        return;
    }

    CachedCanvasSize = CanvasSize;

    // TextureHeight is the authored pixel height of the background plate.
    // All Y offsets in the DA are in that same pixel space.
    // TextureWidth is used for the X offset of the crank.
    const float TexH = FMath::Max(Config.GetFloat(HUDConfigKeys::TextureHeight, 1.f), 1.f);
    const float TexW = FMath::Max(Config.GetFloat(HUDConfigKeys::TextureWidth, TexH), 1.f);

    // Convert a pixel Y offset (in texture space) to screen pixels.
    // bInvert=true means the value is measured from the BOTTOM of the texture.
    auto ToScreenY = [&](float PixelOffset, bool bInvert) -> float
        {
            float Ratio = PixelOffset / TexH;
            if (bInvert) Ratio = 1.f - Ratio;
            return Ratio * CanvasSize.Y;
        };

    // Scale factors: authored texture pixels -> screen pixels
    const float ScaleX = CanvasSize.X / TexW;
    const float ScaleY = CanvasSize.Y / TexH;

    // X positions (CrankOffsetX/MetalOffsetX = horizontal center in texture pixels)
    const float CrankX_px = Config.GetFloat(HUDConfigKeys::CrankOffsetX, TexW * 0.5f);
    const float MetalX_px = Config.GetFloat(HUDConfigKeys::MetalOffsetX, CrankX_px);
    const float CrankX_screen = CrankX_px * ScaleX;
    const float MetalX_screen = MetalX_px * ScaleX;
    const float CrankY_screen = ToScreenY(PendingCrankY_px, bPendingInvertCrank);
    const float MetalY_screen = ToScreenY(PendingMetalY_px, bPendingInvertMetal);

    // Scale slot sizes to screen pixels.
    // All authored widths/heights are in background-plate pixel space,
    // so they use the same ScaleX/ScaleY as positions.
    if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(CrankImage ? CrankImage->Slot : nullptr))
    {
        const float W = FMath::Max(Config.GetFloat(HUDConfigKeys::CrankWidth, TexW) * ScaleX, 1.f);
        const float H = FMath::Max(Config.GetFloat(HUDConfigKeys::CrankHeight, TexH) * ScaleY, 1.f);
        S->SetSize(FVector2D(W, H));
        if (ShouldLog(DebugSettings ? DebugSettings->bLogPositionalIndicator : false)) {
            UE_LOG(LogTemp, Log, TEXT("[PosIndicator] Crank screen size=(%.1fx%.1f)"), W, H);
        }
    }
    if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(MetalImage ? MetalImage->Slot : nullptr))
    {
        const float W = FMath::Max(Config.GetFloat(HUDConfigKeys::MetalWidth, TexW) * ScaleX, 1.f);
        const float H = FMath::Max(Config.GetFloat(HUDConfigKeys::MetalHeight, TexH) * ScaleY, 1.f);
        S->SetSize(FVector2D(W, H));
        if (ShouldLog(DebugSettings ? DebugSettings->bLogPositionalIndicator : false)) {
            UE_LOG(LogTemp, Log, TEXT("[PosIndicator] Metal screen size=(%.1fx%.1f)"), W, H);
        }
    }

    SetMovingImagePosition(CrankImage, FVector2D(CrankX_screen, CrankY_screen));
    SetMovingImagePosition(MetalImage, FVector2D(MetalX_screen, MetalY_screen));

    if (ShouldLog(DebugSettings ? (DebugSettings->bLogModuleLayout && DebugSettings->bLogPositionalIndicator) : false))
        UE_LOG(LogTemp, Log,
            TEXT("[PositionalIndicator] Applied positions  Module='%s'  "
                "Canvas=(%.0f,%.0f)  Crank=(%.1f,%.1f)  Metal=(0,%.1f)"),
            *Config.ModuleName.ToString(),
            CanvasSize.X, CanvasSize.Y,
            CrankX_screen, CrankY_screen, MetalY_screen);

    //// Diagnose slots after position update to confirm SetPosition landed
    //DiagnoseSlot(CrankImage, TEXT("Crank@AfterSetPos"));
    //DiagnoseSlot(MetalImage, TEXT("Metal@AfterSetPos"));

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
//  InitMovingImageSlots
//
//  Called from RefreshVisuals_Implementation, AFTER SetConfig has run.
//  This guarantees Config contains TextureWidth, CrankWidth, etc.
//
//  Root cause of the previous square-image bug:
//    NativeOnInitialized fires before SetConfig. Config was empty.
//    All size reads returned the 64px fallback.
//
//  Alignment (0.5, 0.0):
//    The authored CrankOffsetX / MetalOffsetX value points to the
//    HORIZONTAL CENTER of the element. The Y position is the TOP EDGE.
//    This matches the natural way you'd author positions in texture space.
// ---------------------------------------------------------------------------
void UPositionalIndicatorModule::InitMovingImageSlots()
{
    const float BgW = Config.GetFloat(HUDConfigKeys::TextureWidth, 64.f);
    const float BgH = Config.GetFloat(HUDConfigKeys::TextureHeight, 64.f);

    const float CrankW = Config.GetFloat(HUDConfigKeys::CrankWidth, BgW);
    const float CrankH = Config.GetFloat(HUDConfigKeys::CrankHeight, BgH);
    const float MetalW = Config.GetFloat(HUDConfigKeys::MetalWidth, BgW);
    const float MetalH = Config.GetFloat(HUDConfigKeys::MetalHeight, BgH);

    if (ShouldLog(DebugSettings ? DebugSettings->bLogPositionalIndicator : false)) {
        UE_LOG(LogTemp, Log,
            TEXT("[PositionalIndicator] InitMovingImageSlots '%s'  "
                "Crank=(%.0fx%.0f)  Metal=(%.0fx%.0f)  BgPlate=(%.0fx%.0f)"),
            *Config.ModuleName.ToString(),
            CrankW, CrankH, MetalW, MetalH, BgW, BgH);
    }

    auto SetupSlot = [](UImage* Image, float W, float H, const TCHAR* Label)
        {
            if (!Image) return;

            UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Image->Slot);
            if (!Slot)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[PositionalIndicator] InitMovingImageSlots: "
                        "'%s' (%s) has no CanvasPanelSlot -- "
                        "must be a direct child of LayerCanvas."),
                    *Image->GetName(), Label);
                return;
            }

            Slot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f)); // point anchor
            Slot->SetAlignment(FVector2D(0.f, 0.f));
            Slot->SetAutoSize(false);
            Slot->SetPosition(FVector2D::ZeroVector);
            Slot->SetSize(FVector2D(FMath::Max(W, 1.f), FMath::Max(H, 1.f)));
        };

    SetupSlot(CrankImage, CrankW, CrankH, TEXT("CrankImage"));
    SetupSlot(MetalImage, MetalW, MetalH, TEXT("MetalImage"));
}

// ---------------------------------------------------------------------------
//  SetMovingImagePosition
// ---------------------------------------------------------------------------
bool UPositionalIndicatorModule::SetMovingImagePosition(UImage* Image, FVector2D Position)
{
    if (!Image) return false;

    UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Image->Slot);
    if (!Slot) return false;

    Slot->SetPosition(Position);
    return true;
}