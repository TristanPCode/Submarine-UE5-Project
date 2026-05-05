#pragma once

#include "CoreMinimal.h"
#include "HUD/BaseHUDModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HealthBarModule.generated.h"

class UImage;
class UOverlay;

/**
 * UHealthBarModule
 *
 * Event-driven health bar using three stacked image layers:
 *   1. Background   -- static texture
 *   2. Fill         -- dynamic material (M_HUD_Clip) clipped right-to-left
 *   3. Overlay      -- static texture with alpha preserved
 *
 * No tick. Updates only on OnDamaged delegate.
 *
 * Config keys (FHUDModuleConfig):
 *   Textures: Background, FillGreen, FillYellow, FillRed, Overlay
 *   Floats:   TextureWidth, TextureHeight, OffsetLeft, OffsetRight,
 *             ThresholdYellow, ThresholdRed
 *   Materials: MatClip  (base material for fill clipping)
 *
 * Material parameters pushed each update:
 *   MainTexture    (Texture2D)
 *   ClipRatioRight (float, 0..1)
 *
 * Blueprint setup:
 *   Create BP_HealthBarModule inheriting from this class.
 *   Add an UOverlay named "LayerOverlay" as root.
 *   Inside it (in order): BackgroundImage, FillImage, OverlayImage.
 */
UCLASS(Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API UHealthBarModule : public UBaseHUDModule
{
    GENERATED_BODY()

protected:

    // -----------------------------------------------------------------------
    //  UMG widgets  (bound from Blueprint via BindWidget)
    // -----------------------------------------------------------------------

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> BackgroundImage;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> FillImage;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> OverlayImage;

    // -----------------------------------------------------------------------
    //  UBaseHUDModule overrides
    // -----------------------------------------------------------------------

    virtual void BindToDataSource()   override;
    virtual void UnbindFromDataSource() override;

    UFUNCTION()
    virtual void RefreshVisuals_Implementation() override;

    // -----------------------------------------------------------------------
    //  Event handler
    // -----------------------------------------------------------------------

    UFUNCTION()
    void OnDamaged(float DamageAmount, AActor* DamageCauser);

private:

    // -----------------------------------------------------------------------
    //  Internal state
    // -----------------------------------------------------------------------

    /** Dynamic material instance for FillImage. Created once in BindToDataSource. */
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> FillMID;

    /** Last computed health ratio -- avoids redundant material pushes. */
    float LastHealthRatio = -1.f;

    // -----------------------------------------------------------------------
    //  Internal helpers
    // -----------------------------------------------------------------------

    /**
     * Compute the ClipRatioRight from health ratio and pixel offsets.
     * All pixel values are converted to texture-space ratios internally.
     *
     * @param HealthRatio  0..1
     * @return             UV right-clip boundary (0..1 in texture space)
     */
    float ComputeClipRatio(float HealthRatio) const;

    /**
     * Select the correct fill texture based on health ratio and thresholds.
     */
    UTexture2D* SelectFillTexture(float HealthRatio) const;

    /**
     * Push current health state to the fill material instance.
     * Safe to call with an invalid DataSource (uses cached LastHealthRatio).
     */
    void PushHealthToMaterial(float HealthRatio);

    /**
     * Create the fill MID from the MatClip base material in config.
     * Called once when a valid DataSource is set.
     * Returns false if the base material is missing from config.
     */
    bool CreateFillMID();
};