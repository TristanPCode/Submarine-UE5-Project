#pragma once

#include "CoreMinimal.h"
#include "HUD/BaseHUDModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TorpedoIconModule.generated.h"

class UImage;

/**
 * UTorpedoIconModule
 *
 * Event-driven torpedo icon with glow animation and reload overlay.
 * Used as a child module inside UNormalAmmoModule.
 *
 * Visual layers:
 *   1. IconImage       -- M_TorpedoIcon MID (ready/cooldown texture + optional glow)
 *   2. ReloadOverlay   -- M_ReloadOverlay MID (clips bottom->top while reloading)
 *
 * State priority (strict):
 *   1. IsReloading     -> show overlay, icon = cooldown tex, tick enabled (overlay progress)
 *   2. FireCooldown<1  -> hide overlay, icon = cooldown tex, tick disabled
 *   3. Ready           -> hide overlay, icon = ready tex + glow, tick enabled (glow pulse)
 *
 * Material parameters:
 *   M_TorpedoIcon:
 *     MainTexture   (Texture2D)    -- ready or cooldown texture
 *     GlowMask      (Texture2D)    -- optional mask; white fallback = global glow
 *     GlowIntensity (float 0..1)   -- animated in tick when Ready
 *     GlowPulseSpeed(float)        -- from config
 *     bIsReady      (float 0 or 1) -- lets material branch on state if needed
 *
 *   M_ReloadOverlay:
 *     ReloadRatio   (float 0..1)   -- material computes VisibleRatio = 1 - ReloadRatio
 *                                     and clips overlay from bottom to top
 *
 * Config keys (FHUDModuleConfig):
 *   Textures:   TorpedoReady, TorpedoCooldown, GlowMask (optional)
 *   Materials:  MatTorpedoIcon, MatReloadOverlay
 *   Floats:     GlowPulseSpeed, FlashDuration,
 *               IconTextureWidth, IconTextureHeight
 *
 * Blueprint setup:
 *   Create BP_TorpedoIconModule inheriting from this class.
 *   Add a UCanvasPanel as root.
 *   Inside it (in order): IconImage, ReloadOverlayImage.
 *   Both images anchored to fill the panel.
 */
UCLASS(Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API UTorpedoIconModule : public UBaseHUDModule
{
    GENERATED_BODY()

protected:

    // -----------------------------------------------------------------------
    //  UMG widgets (bound from Blueprint)
    // -----------------------------------------------------------------------

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> IconImage;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> ReloadOverlayImage;

    // -----------------------------------------------------------------------
    //  UBaseHUDModule overrides
    // -----------------------------------------------------------------------

    virtual void BindToDataSource()      override;
    virtual void UnbindFromDataSource()  override;

    UFUNCTION()
    virtual void RefreshVisuals_Implementation() override;

    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:

    // -----------------------------------------------------------------------
    //  Internal state
    // -----------------------------------------------------------------------

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> IconMID;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> ReloadOverlayMID;

    enum class ETorpedoIconState : uint8
    {
        Ready,
        Cooldown,
        Reloading
    };

    ETorpedoIconState CurrentState = ETorpedoIconState::Cooldown;

    // Glow animation
    float GlowTimer = 0.f;
    bool  bIsReloading = false;  // cached each EvaluateState, drives overlay independently
    float FlashIntensity = 0.f;  // one-shot boost on Ready transition, decays to 0
    bool  bJustBecameReady = false;

    // -----------------------------------------------------------------------
    //  Internal helpers
    // -----------------------------------------------------------------------

    /** Create IconMID and ReloadOverlayMID from config materials. */
    bool CreateMIDs();

    /**
     * Evaluate current data source state and transition to the correct visual state.
     * Called from every relevant delegate callback and from RefreshVisuals.
     * @param bForceRefresh  If true, always apply even if state is unchanged.
     */
    void EvaluateState(bool bForceRefresh = false);

    /** Push the current state to MID parameters and widget visibility. */
    void ApplyStateToMaterials();

    // -----------------------------------------------------------------------
    //  Delegate callbacks (all route to EvaluateState)
    // -----------------------------------------------------------------------

    UFUNCTION() void OnAmmoChanged(int32 NormalCount, int32 SpecialCount);
    UFUNCTION() void OnFireCooldownComplete();
    UFUNCTION() void OnReadyToFire();
    UFUNCTION() void OnProgressiveReloadComplete();
    UFUNCTION() void OnFullReloadComplete();
};