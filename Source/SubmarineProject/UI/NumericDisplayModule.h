#pragma once

#include "CoreMinimal.h"
#include "HUD/BaseHUDModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NumericDisplayModule.generated.h"

class UImage;
class UCanvasPanel;

/**
 * UNumericDisplayModule
 *
 * Tick-driven numeric display using a digit atlas texture.
 * Renders up to 4 digits (XXX.X format) by selecting UV regions from
 * a shared atlas texture. The decimal dot is baked into the background.
 * 
 * Digit count is driven by Config.GetFloat("DigitCount") cast to int.
 * Digit images are created dynamically in NativeOnInitialized — no
 * hardcoded BindWidget slots.
 *
 * Atlas layout (3 rows × 10 digits):
 *   Row 0 = Speed (green), Row 1 = Depth (blue), Row 2 = Ammo (orange)
 *   Each digit cell: 82 × 167 px
 *   First digit in atlas: offset (1px from left, 2px from top)
 *   Horizontal spacing between digits in atlas: 2px
 *   Vertical spacing between rows in atlas: 3px
 *
 * Display layout (authored in original texture space, scaled at runtime):
 *
 *   Speed / Depth (XXX.X format, 4 digits):
 *     Background: 524 × 364 px
 *     First digit offset: (142, 33)
 *     Digit spacing: 33px
 *     Dot spacing (each side): 63px
 *     DecimalIndex = 3 (dot between digit 2 and digit 3)
 *
 *   Ammo (XX format, 2 digits):
 *     Background: 227 × 227 px
 *     First digit offset: (28, 20)
 *     Digit spacing: 23px
 *     DecimalIndex = -1 (no decimal)
 *
 * Config keys (FHUDModuleConfig):
 *   Textures:  Background, DigitAtlas, DecimalDot (optional image)
 *   Floats:
 *     DigitCount        -- total number of digit slots (2, 3, 4…)
 *     DecimalIndex      -- position of decimal dot (-1 = none)
 *                          dot placed between digit[DecimalIndex-1] and digit[DecimalIndex]
 *     AtlasDigitWidth   -- 82
 *     AtlasDigitHeight  -- 167
 *     AtlasTotalWidth   -- 842
 *     AtlasTotalHeight  -- 510
 *     AtlasRowIndex     -- 0/1/2 selects row
 *     AtlasFirstOffsetX -- 1  (left offset of first digit in atlas, px)
 *     AtlasFirstOffsetY -- 2  (top offset of first digit in atlas, px)
 *     AtlasHSpacing     -- 2  (horizontal gap between digits in atlas, px)
 *     AtlasVSpacing     -- 3  (vertical gap between rows in atlas, px)
 *     DigitOffsetX      -- first digit X offset from module origin (original px)
 *     DigitOffsetY      -- first digit Y offset from module origin (original px)
 *     DigitSpacing      -- spacing between digit centers (original px)
 *     DotSpacingLeft    -- extra gap left of dot (original px, 0 if no dot)
 *     DotSpacingRight   -- extra gap right of dot (original px, 0 if no dot)
 *     MaxValue          -- clamp for display value
 *   Materials: MatAtlasSample
 *
 * Performance note:
 *   MIDs are created once per bind. Each tick only pushes UV parameters
 *   for digits whose value has changed (LastDigits comparison). When value
 *   is static, tick cost = N integer comparisons where N = DigitCount.
 *
 * Material parameters updated per digit:
 *   UMin, UMax  (float, 0..1 -- horizontal UV region for this digit)
 *   VMin, VMax  (float, 0..1 -- vertical UV region = row)
 *
 * Blueprint setup:
 *   Create BP_NumericDisplayModule inheriting from this class.
 *   Add a UCanvasPanel named "DigitCanvas" as root.
 *   Inside it: BackgroundImage, Digit0Image, Digit1Image, Digit2Image, Digit3Image.
 *   BackgroundImage anchored to fill. Digit images positioned by C++.
 */
UCLASS(Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API UNumericDisplayModule : public UBaseHUDModule
{
    GENERATED_BODY()

protected:

    // -----------------------------------------------------------------------
    //  UMG widgets (bound from Blueprint)
    // -----------------------------------------------------------------------

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UCanvasPanel> DigitCanvas;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> BackgroundImage;

    // -----------------------------------------------------------------------
    //  UBaseHUDModule overrides
    // -----------------------------------------------------------------------

    virtual void NativeOnInitialized() override;

    virtual void BindToDataSource()     override;
    virtual void UnbindFromDataSource() override;

    UFUNCTION()
    virtual void RefreshVisuals_Implementation() override;

    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    // -----------------------------------------------------------------------
    //  Subclass interface
    // -----------------------------------------------------------------------

    /**
     * Return the current value to display.
     * Override in USpeedometerModule and UDepthModule.
     * Default returns 0.
     */
    virtual float GetDisplayValue() const;

private:

    // -----------------------------------------------------------------------
    //  Dynamically created digit images
    // -----------------------------------------------------------------------

    /** One UImage per digit slot, created in NativeOnInitialized. */
    UPROPERTY()
    TArray<TObjectPtr<UImage>> DigitImages;

    // -----------------------------------------------------------------------
    //  MID state
    // -----------------------------------------------------------------------

    /** One MID per digit slot. All reference the same atlas texture. */
    UPROPERTY()
    TArray<TObjectPtr<UMaterialInstanceDynamic>> DigitMIDs;

    /** Last rendered digit values. -1 = force refresh on next tick. */
    TArray<int32> LastDigits;

    /** Cached atlas UV constants computed once at BindToDataSource. */
    float CachedVMin = 0.f;
    float CachedVMax = 0.f;
    float CachedUStep = 0.f;  // width of one digit in UV space (1/10)
    float CachedUDigitWidth = 0.f; // pure digit width in UV (DigitW/TotalW, no spacing)
    float CachedUFirstOffset = 0.f; // UV offset for the first pixel of digit 0

    bool bMIDsReady = false;
    bool bWidgetsCreated = false;
    bool bWidgetsPositioned = false;

    // -----------------------------------------------------------------------
    //  Internal helpers
    // -----------------------------------------------------------------------

    /**
     * Dynamically create DigitImages (and optional DecimalDotImage) as
     * children of DigitCanvas. Called once from NativeOnInitialized.
     */
    void CreateDigitWidgets();

    /**
     * Position digit images and decimal dot on DigitCanvas using config
     * pixel offsets scaled to current canvas size.
     * Safe to call before layout — deferred if canvas size is zero.
     */
    void PositionDigitImages();

    /** Create MID instances from MatAtlasSample. Called in BindToDataSource. */
    bool CreateDigitMIDs();

    /**
     * Decompose a float value into individual digit values.
     * @param Value         Value to decompose (clamped to [0, MaxValue])
     * @param OutDigits     Output array, size = DigitCount
     * @param DecimalIndex  Where the decimal point sits (-1 = none)
     */
    void DecomposeValue(float Value,
        TArray<int32>& OutDigits,
        int32          DecimalIndex) const;

    /** Push UV parameters for one digit slot. */
    void PushDigitToMaterial(int32 SlotIndex, int32 DigitValue);

    /** Update all digit MIDs from the current display value. */
    void UpdateAllDigits(float Value);

    /** Return the configured digit count (from Config Floats["DigitCount"]). */
    int32 GetDigitCount() const;

    /** Return the configured decimal index (from Config Floats["DecimalIndex"]). */
    int32 GetDecimalIndex() const;
};