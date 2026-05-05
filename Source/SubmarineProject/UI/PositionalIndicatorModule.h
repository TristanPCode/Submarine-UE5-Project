#pragma once

#include "CoreMinimal.h"
#include "HUD/BaseHUDModule.h"
#include "PositionalIndicatorModule.generated.h"

class UImage;
class UCanvasPanel;
class UCanvasPanelSlot;

/**
 * UPositionalIndicatorModule
 *
 * Shared base class for UEngineStateModule and UPitchModule.
 *
 * Manages three stacked image layers on a UCanvasPanel:
 *   1. Background  -- static plate, pinned full-size
 *   2. Crank       -- repositioned vertically by subclass
 *   3. Metal       -- repositioned vertically by subclass
 *
 * Positioning is pixel-authored and converted to ratios at runtime:
 *   FinalRatio = PixelOffset / TextureHeight
 *   if bInvert: FinalRatio = 1.0 - FinalRatio
 *   FinalPosition = FinalRatio * RenderedHeight
 *
 * UMG layout timing:
 *   Widget rendered size is not available at construction time.
 *   Positions are applied in NativeOnInitialized() after the first layout pass,
 *   and also whenever UpdateElementPositions() is called by subclasses.
 *   Subclasses call UpdateElementPositions() from their update path
 *   (event handler or NativeTick).
 *
 * Config keys:
 *   Textures: Background, Crank, Metal
 *   Floats:   TextureHeight, CrankOffsetX
 *   StateEntries: TArray<FEngineStateEntry> (populated by subclasses' configs)
 *
 * Blueprint setup:
 *   Create a Blueprint subclass.
 *   Add a UCanvasPanel named "LayerCanvas" as root.
 *   Inside it: BackgroundImage, CrankImage, MetalImage.
 *   Set BackgroundImage slot to fill the panel (anchors 0,0 to 1,1).
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API UPositionalIndicatorModule : public UBaseHUDModule
{
    GENERATED_BODY()

protected:

    // -----------------------------------------------------------------------
    //  UMG widgets (bound from Blueprint)
    // -----------------------------------------------------------------------

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UCanvasPanel> LayerCanvas;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> BackgroundImage;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> CrankImage;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> MetalImage;

    // -----------------------------------------------------------------------
    //  UBaseHUDModule overrides
    // -----------------------------------------------------------------------

    virtual void NativeOnInitialized() override;

    UFUNCTION()
    virtual void RefreshVisuals_Implementation() override;

    // -----------------------------------------------------------------------
    //  API for subclasses
    // -----------------------------------------------------------------------

    /**
     * Apply absolute pixel positions to Crank and Metal widgets.
     *
     * PixelOffsets are in the original texture's pixel space.
     * Converted to actual screen positions by multiplying the ratio by the
     * current rendered module height.
     *
     * bInvertCrank / bInvertMetal: measure from bottom instead of top.
     *
     * Safe to call before layout is complete -- silently no-ops if the
     * canvas size is zero (positions will be applied at NativeOnInitialized).
     */
    void UpdateElementPositions(
        float CrankY_px, bool bInvertCrank,
        float MetalY_px, bool bInvertMetal);

    /**
     * Cached rendered size of LayerCanvas.
     * Valid after NativeOnInitialized(). Zero before that.
     */
    FVector2D CachedCanvasSize = FVector2D::ZeroVector;

    // -----------------------------------------------------------------------
    //  Pending position state
    //  Stored so positions can be re-applied when canvas size becomes known.
    // -----------------------------------------------------------------------

    float PendingCrankY_px = 0.f;
    float PendingMetalY_px = 0.f;
    bool  bPendingInvertCrank = false;
    bool  bPendingInvertMetal = false;
    bool  bHasPendingUpdate = false;

private:

    /**
     * Apply static textures (Background, Crank, Metal) from config.
     * Called once in RefreshVisuals_Implementation.
     */
    void ApplyStaticTextures();

    /**
     * Resolve current canvas size and apply pending positions.
     * Called from NativeOnInitialized and after each UpdateElementPositions.
     */
    void ApplyPositionsIfReady();

    /**
     * Set widget slot position in absolute canvas pixels.
     * Returns false if the widget has no canvas slot.
     */
    static bool SetCanvasSlotPosition(UWidget* Widget, FVector2D Position);
};