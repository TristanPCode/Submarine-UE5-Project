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
 * Shared base for UEngineStateModule and UPitchModule.
 *
 * Three image layers on a UCanvasPanel:
 *   1. BackgroundImage  -- static, fills the whole canvas (anchor 0,0->1,1)
 *   2. CrankImage       -- moved as a rigid block by subclass logic
 *   3. MetalImage       -- moved as a rigid block by subclass logic
 *
 * -----------------------------------------------------------------------
 *  BLUEPRINT SETUP
 * -----------------------------------------------------------------------
 *  Root widget: LayerCanvas (UCanvasPanel)
 *    BackgroundImage  -- anchor Min(0,0) Max(1,1), offsets 0   <- fills panel
 *    CrankImage       -- any anchor/size, overwritten at runtime
 *    MetalImage       -- any anchor/size, overwritten at runtime
 *
 * -----------------------------------------------------------------------
 *  DA CONFIG KEYS
 * -----------------------------------------------------------------------
 *  Textures:
 *    Background    -- static backdrop
 *    Crank         -- moving crank element
 *    Metal         -- moving metal element
 *
 *  Floats (background plate dimensions -- coordinate reference for all positions):
 *    TextureWidth   -- authored pixel width  of the background texture
 *    TextureHeight  -- authored pixel height of the background texture
 *
 *  Floats (each element's OWN texture pixel dimensions):
 *    CrankWidth, CrankHeight   -- pixel size of the Crank texture
 *    MetalWidth, MetalHeight   -- pixel size of the Metal texture
 *
 *  Floats (horizontal center positions, in background-plate pixel space):
 *    CrankOffsetX   -- X center of crank measured from left of background plate
 *    MetalOffsetX   -- X center of metal measured from left (default = CrankOffsetX)
 *
 *  For EngineStateModule:
 *    StateEntries[] -- 7 FEngineStateEntry, indexed by (int32)ELinearSpeedState:
 *      0=BackwardMAX, 1=BackwardMED, 2=BackwardMIN, 3=Stand,
 *      4=ForwardMIN,  5=ForwardMED,  6=ForwardMAX
 *
 *  For PitchModule:
 *    MinCrankY, MaxCrankY  -- crank Y at full-down / full-up pitch (texture pixels)
 *    MinMetalY, MaxMetalY  -- metal Y at full-down / full-up pitch (texture pixels)
 *    MaxPitchAngle         -- degrees, e.g. 30.0
 *    InvertPitch           -- 1.0 to flip crank direction if it moves wrong way
 *    FlipMetalOnNegativePitch -- 1.0 to mirror metal texture when nose is down
 *
 * -----------------------------------------------------------------------
 *  COORDINATE SYSTEM
 * -----------------------------------------------------------------------
 *  All authored Y values are in background-plate PIXEL SPACE (0=top).
 *  Converted at runtime: ScreenY = (PixelY / TextureHeight) * CanvasHeight
 *  bInvert=true: ScreenY = (1 - PixelY/TextureHeight) * CanvasHeight
 *
 *  X positions use TextureWidth as the reference. Alignment (0.5, 0.0)
 *  means the authored X is the HORIZONTAL CENTER of the element,
 *  and Y is the TOP EDGE.
 *
 * -----------------------------------------------------------------------
 *  IMPORTANT -- CONFIG TIMING
 * -----------------------------------------------------------------------
 *  NativeOnInitialized fires BEFORE SetConfig. Therefore InitMovingImageSlot
 *  (which reads CrankWidth etc.) must NOT be called in NativeOnInitialized.
 *  It is called in RefreshVisuals_Implementation, which fires after SetConfig.
 *  A guard flag (bSlotsInitialized) ensures it only re-runs when config changes.
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
     * Force a UImage slot into point-anchor + fixed-size mode.
     * Called from RefreshVisuals (after SetConfig), NOT from NativeOnInitialized.
     * Alignment (0.5, 0.0): authored X = horizontal center, Y = top edge.
     */
    void InitMovingImageSlots();

    /**
     * Move a UImage that has been set up by InitMovingImageSlot.
     * Uses SetPosition which only works correctly in point-anchor mode.
     */
    static bool SetMovingImagePosition(UImage* Image, FVector2D Position);
};