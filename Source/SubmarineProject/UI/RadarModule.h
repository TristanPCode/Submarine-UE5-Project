#pragma once

#include "CoreMinimal.h"
#include "HUD/BaseHUDModule.h"
#include "TrackableSubmarine.h"
#include "RadarModule.generated.h"

class UImage;
class UCanvasPanel;
class UOverlay;

/**
 * URadarModule
 *
 * Tick-driven radar display widget. Reads ONLY from ITrackableSubmarine.
 * Contains zero gameplay logic.
 *
 * -----------------------------------------------------------------------
 *  Visual layers (all children of RadarCanvas):
 * -----------------------------------------------------------------------
 *   1. BackgroundImage   -- static radar background (1080x1080, circle)
 *   2. EntitiesCanvas    -- parent panel for all entity icons (static position)
 *   3. CardinalImage     -- N/S/E/W ring, rotated each tick to reflect world yaw
 *   4. OverlayImage      -- static top overlay (e.g. frame, glass effect)
 *
 * -----------------------------------------------------------------------
 *  Entity icons
 * -----------------------------------------------------------------------
 *   One UImage per FDetectedEntry, created dynamically when first detected,
 *   reused while the entry persists, destroyed when entry expires.
 *
 *   Icon pool managed via TMap<FGuid, UImage*>.
 *   Entity positions are computed mathematically (no widget transforms for position):
 *     RelativePos = EntityWorldPos - SubWorldPos
 *     RotatedPos  = RelativePos rotated by -SubmarineYaw (horizontal plane)
 *     RadarPos2D  = FVector2D(RotatedPos.X, -RotatedPos.Y) / WorldRange * RadarRadius
 *     CanvasPos   = RadarCenter + RadarPos2D
 *
 *   If icon would be outside HideThreshold * RadarRadius -> hidden.
 *
 * -----------------------------------------------------------------------
 *  Rotation
 * -----------------------------------------------------------------------
 *   CardinalImage render transform: Angle = -SubmarineYaw
 *   Entity icons:
 *     Submarines / Unknown -> no rotation (SetRenderTransformAngle(0))
 *     Torpedoes -> SetRenderTransformAngle(Entry.IconRotation)
 *
 * -----------------------------------------------------------------------
 *  Fade
 * -----------------------------------------------------------------------
 *   Icon opacity = clamp(DisplayTimeRemaining / FadeDuration, 0, 1)
 *   When DisplayTimeRemaining == 0 -> icon hidden (opacity 0).
 *
 * -----------------------------------------------------------------------
 *  Config keys (FHUDModuleConfig):
 * -----------------------------------------------------------------------
 *   Textures:
 *     Background      -- radar background (1080x1080)
 *     CardinalLayer   -- N/S/E/W ring texture
 *     Overlay         -- top overlay
 *     IconUnknown     -- default detection icon
 *     IconSubmarine   -- identified submarine icon
 *     IconTorpedo     -- identified torpedo icon
 *   Floats:
 *     RadarRadius     -- visual radius of radar circle in original tex pixels
 *     TextureWidth    -- 1080
 *     TextureHeight   -- 1080
 *
 * -----------------------------------------------------------------------
 *  Blueprint setup:
 * -----------------------------------------------------------------------
 *   Create BP_RadarModule inheriting from this class.
 *   Add a UCanvasPanel named "RadarCanvas" as root (sized to match texture).
 *   Add children in order: BackgroundImage, EntitiesCanvas, CardinalImage, OverlayImage.
 *   BackgroundImage and OverlayImage: anchored to fill RadarCanvas.
 *   CardinalImage: anchored to fill RadarCanvas (rotation via render transform).
 *   EntitiesCanvas: anchored to fill RadarCanvas.
 *   No entity icons needed in Blueprint -- created in C++.
 */
UCLASS(Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API URadarModule : public UBaseHUDModule
{
    GENERATED_BODY()

protected:

    // -----------------------------------------------------------------------
    //  UMG widgets (bound from Blueprint)
    // -----------------------------------------------------------------------

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UCanvasPanel> RadarCanvas;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> BackgroundImage;

    /** Parent panel for all entity icon images. */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UCanvasPanel> EntitiesCanvas;

    /** Cardinal direction ring — rotated each tick. */
    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> CardinalImage;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> OverlayImage;

    // -----------------------------------------------------------------------
    //  UBaseHUDModule overrides
    // -----------------------------------------------------------------------

    virtual void NativeOnInitialized() override;

    virtual void BindToDataSource()      override;
    virtual void UnbindFromDataSource()  override;

    UFUNCTION()
    virtual void RefreshVisuals_Implementation() override;

    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:

    // -----------------------------------------------------------------------
    //  Entity icon pool
    // -----------------------------------------------------------------------

    /**
     * Map from ActorGuid to the UImage widget used to represent it.
     * Entries added when a new FDetectedEntry appears.
     * Entries removed when the FDetectedEntry expires.
     */
    UPROPERTY()
    TMap<FGuid, TObjectPtr<UImage>> IconPool;

    // -----------------------------------------------------------------------
    //  Cached radar geometry (computed once per layout pass)
    // -----------------------------------------------------------------------

    /** Radar circle center in DigitCanvas local space (pixels). */
    FVector2D RadarCenter = FVector2D::ZeroVector;

    /** Visual radar circle radius in screen pixels. */
    float RadarRadiusPx = 0.f;

    bool bGeometryCached = false;
    float ModuleLogTimer = 0.f;

    // -----------------------------------------------------------------------
    //  GlowPusle Effect
    // -----------------------------------------------------------------------

    // Pulse animation state
    float PulseProgress = 0.f;       // 0->1 as ring expands
    bool  bPulseActive  = false;
    float PulseLogTimer = 0.f;       // throttle for per-tick pulse logs
    float PreviousScanCooldownRatio = 1.f;  // for transition detection

    void TickPulse(float DeltaTime);


    // -----------------------------------------------------------------------
    //  Background MID
    // -----------------------------------------------------------------------

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> BackgroundMID;


    void CreateBackgroundMID();

    // -----------------------------------------------------------------------
    //  Internal helpers
    // -----------------------------------------------------------------------

    /** Compute and cache RadarCenter and RadarRadiusPx from canvas size. */
    void CacheRadarGeometry();

    /**
     * Synchronize IconPool with the current DetectionEntries array:
     *   - Create UImage for new entries.
     *   - Remove UImage for entries no longer in the list.
     *   - Update position, rotation, texture, opacity for all icons.
     */
    void UpdateEntityIcons(const TArray<FDetectedEntry>& Entries,
        float SubYaw);

    /**
     * Project a world-space entity position to radar canvas space.
     * @param WorldPos   Entity world position
     * @param SubPos     Submarine world position
     * @param SubYaw     Submarine yaw (degrees)
     * @param OutPos     Output position in RadarCanvas local space
     * @return           True if inside HideThreshold
     */
    bool ProjectToRadar(const FVector& WorldPos,
        const FVector& SubPos,
        float          SubYaw,
        FVector2D& OutPos) const;

    /**
     * Select the correct texture for an entity icon based on type.
     * Uses Unknown texture until ClearID/VulnerableID is reached.
     */
    UTexture2D* SelectIconTexture(const FDetectedEntry& Entry) const;

    /**
     * Compute icon opacity from DisplayTimeRemaining and FadeDuration.
     */
    float ComputeIconOpacity(const FDetectedEntry& Entry) const;
};