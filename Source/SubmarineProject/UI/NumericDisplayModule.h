#pragma once

#include "CoreMinimal.h"
#include "HUD/BaseHUDModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NumericDisplayModule.generated.h"

class UImage;
class UCanvasPanel;

/** Maximum number of digits rendered (XXX.X format = 4 digits). */
static constexpr int32 MaxDigitCount = 4;

/**
 * UNumericDisplayModule
 *
 * Tick-driven numeric display using a digit atlas texture.
 * Renders up to 4 digits (XXX.X format) by selecting UV regions from
 * a shared atlas texture. The decimal dot is baked into the background.
 *
 * Atlas layout:
 *   3 rows (color variants), 10 columns (digits 0-9)
 *   Row 0 = Speed (green), Row 1 = Depth (blue), Row 2 = Ammo (orange)
 *   Each digit cell: AtlasDigitWidth x AtlasDigitHeight pixels
 *
 * All digit UImage widgets share ONE atlas texture via separate MID instances
 * of M_HUD_AtlasSample. Only UV parameters are updated per tick.
 *
 * Tick optimization: digits are only pushed to material when the digit value
 * changes. Steady-state (no movement) costs only 4 integer comparisons.
 *
 * Config keys:
 *   Textures:  Background, DigitAtlas
 *   Floats:    AtlasDigitWidth, AtlasDigitHeight,
 *              AtlasTotalWidth, AtlasTotalHeight,
 *              AtlasRowIndex (0/1/2),
 *              DigitSpacing, DigitOffsetX, DigitOffsetY,
 *              MaxValue, DecimalPlaces
 *   Materials: MatAtlasSample  (base material for atlas sampling)
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

    /** Four digit image slots. Digit0 = leftmost (hundreds), Digit3 = tenths. */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> Digit0Image;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> Digit1Image;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> Digit2Image;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> Digit3Image;

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
    //  Internal state
    // -----------------------------------------------------------------------

    /** One MID per digit slot. All reference the same atlas texture. */
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> DigitMIDs[MaxDigitCount];

    /** Last rendered digit values -- used to skip redundant material pushes. */
    int32 LastDigits[MaxDigitCount] = { -1, -1, -1, -1 };

    /** Cached pointer to the digit images for indexed access in tick. */
    UImage* DigitImages[MaxDigitCount] = { nullptr };

    /** Cached atlas UV constants computed once at BindToDataSource. */
    float CachedVMin = 0.f;
    float CachedVMax = 0.f;
    float CachedUStep = 0.f;  // width of one digit in UV space (1/10)

    bool bMIDsReady = false;

    // -----------------------------------------------------------------------
    //  Internal helpers
    // -----------------------------------------------------------------------

    /**
     * Create MID instances for all four digit slots from MatAtlasSample.
     * Called once per data source bind. Returns false on failure.
     */
    bool CreateDigitMIDs();

    /**
     * Position digit images in DigitCanvas based on config offsets.
     * Safe to call before layout -- deferred to NativeOnInitialized if needed.
     */
    void PositionDigitImages();

    /**
     * Decompose a float value into 4 display digits: [d0, d1, d2, d3]
     * where d0 is hundreds, d1 is tens, d2 is units, d3 is tenths.
     * Value is clamped to [0, MaxValue] before decomposition.
     */
    void DecomposeValue(float Value, int32 OutDigits[MaxDigitCount]) const;

    /**
     * Push UV parameters for digit at SlotIndex to its MID.
     * @param SlotIndex  0..3
     * @param DigitValue 0..9
     */
    void PushDigitToMaterial(int32 SlotIndex, int32 DigitValue);

    /**
     * Update all four digit MIDs from the current display value.
     * Skips slots where the digit hasn't changed (LastDigits comparison).
     */
    void UpdateAllDigits(float Value);

    bool bDigitImagesPositioned = false;
};