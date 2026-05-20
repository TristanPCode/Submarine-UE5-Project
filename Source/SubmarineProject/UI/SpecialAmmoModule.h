#pragma once

#include "CoreMinimal.h"
#include "HUD/BaseHUDModule.h"
#include "SpecialTorpedoSlotModule.h"
#include "SpecialAmmoModule.generated.h"

class UCanvasPanel;

/**
 * ESlotRowAlignment
 *
 * Horizontal alignment applied per row in the special ammo grid.
 */
UENUM(BlueprintType)
enum class ESlotRowAlignment : uint8
{
    Left,
    Right,
    Center
};

/**
 * FSlotLayoutEntry
 *
 * Pre-computed absolute pixel position and size for one slot within GridCanvas.
 * Computed once in NativeOnInitialized, applied to slot canvas slots.
 */
USTRUCT()
struct FSlotLayoutEntry
{
    GENERATED_BODY()

    int32     SlotIndex = 0;
    FVector2D Position = FVector2D::ZeroVector;   // absolute pixels in GridCanvas
    FVector2D Size = FVector2D::ZeroVector;   // absolute pixels
};

/**
 * USpecialAmmoModule
 *
 * Composite grid module owning N USpecialTorpedoSlotModule instances
 * (one per special torpedo capacity slot).
 *
 * Layout:
 *   Slots are arranged in a grid. Two modes:
 *
 *   Auto-fill (bUseCustomRowCounts == false):
 *     Rows filled left-to-right, each row holds up to GridColumns slots.
 *
 *   Custom row counts (bUseCustomRowCounts == true):
 *     RowCounts[i] defines how many slots are in row i.
 *     Overflow  (sum > capacity): trim from last rows backward.
 *     Underflow (sum < capacity): append remaining to last row,
 *                                 adding new rows of GridColumns if needed.
 *
 *   All slot sizes are derived from original texture dimensions and
 *   scaled proportionally to the current rendered canvas size.
 *   No raw pixel values are applied directly.
 *
 * State updates:
 *   Triggered by OnAmmoChanged and OnFireCooldownComplete delegates.
 *   O(N) -- all slots updated, no widget rebuild.
 *
 * Config keys (FHUDModuleConfig):
 *   Floats:
 *     GridColumns       -- max slots per row
 *     GridRowAlignment  -- 0=Left, 1=Right, 2=Center
 *     IconTextureWidth  -- original icon width in pixels (for aspect ratio)
 *     IconTextureHeight -- original icon height in pixels
 *     SlotSpacingX      -- horizontal gap in original-texture-space pixels
 *     SlotSpacingY      -- vertical gap in original-texture-space pixels
 *   SlotModuleClass:
 *     Class to instantiate per slot (default: USpecialTorpedoSlotModule)
 *   SlotTypes:
 *     Array of ETorpedoType per slot index
 *   IconPerType:
 *     Map ETorpedoType -> UTexture2D*
 *   bUseCustomRowCounts, RowCounts:
 *     Custom row layout (see above)
 *
 * Blueprint setup:
 *   Create BP_SpecialAmmoModule inheriting from this class.
 *   Add a UCanvasPanel named "GridCanvas" as root.
 *   No child widgets needed -- slots are created in C++.
 */
UCLASS(Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API USpecialAmmoModule : public UBaseHUDModule
{
    GENERATED_BODY()

protected:

    // -----------------------------------------------------------------------
    //  UMG widgets (bound from Blueprint)
    // -----------------------------------------------------------------------

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UCanvasPanel> GridCanvas;

    // -----------------------------------------------------------------------
    //  UBaseHUDModule overrides
    // -----------------------------------------------------------------------

    virtual void NativeOnInitialized() override;

    virtual void BindToDataSource()      override;
    virtual void UnbindFromDataSource()  override;

    UFUNCTION()
    virtual void RefreshVisuals_Implementation() override;

private:

    // -----------------------------------------------------------------------
    //  Slot instances (owned via ChildModules array in base)
    // -----------------------------------------------------------------------

    UPROPERTY()
    TArray<TObjectPtr<USpecialTorpedoSlotModule>> SlotModules;

    bool bSlotsCreated = false;

    // -----------------------------------------------------------------------
    //  Internal helpers
    // -----------------------------------------------------------------------

    /**
     * Create all slot instances and position them on GridCanvas.
     * Called from NativeOnInitialized once canvas size is available.
     */
    void CreateSlots();

    /**
     * Resolve the final row layout as a flat TArray<int32> (slots per row)
     * from config, handling both auto-fill and custom modes, plus
     * overflow/underflow correction.
     *
     * @param TotalSlots  Total number of slots to lay out
     * @param Columns     Max slots per row (from GridColumns config)
     * @return            Array where element i = number of slots in row i
     */
    TArray<int32> ResolveRowCounts(int32 TotalSlots, int32 Columns) const;

    /**
     * Compute absolute pixel positions and sizes for all slots within GridCanvas.
     * Scaling-safe: all values derived from original texture dimensions and
     * converted to screen pixels at runtime.
     *
     * @param TotalSlots   Number of slots
     * @param CanvasSize   Current rendered size of GridCanvas (pixels)
     * @param RowCounts    Resolved row layout from ResolveRowCounts()
     * @return             One FSlotLayoutEntry per slot, ordered by slot index
     */
    TArray<FSlotLayoutEntry> ComputeGridLayout(
        int32                    TotalSlots,
        FVector2D                CanvasSize,
        const TArray<int32>& RowCounts) const;

    /**
     * Compute slot pixel size (width, height) preserving icon aspect ratio
     * and fitting within the canvas given the number of rows and columns.
     * Scaling is derived from original texture dimensions in config.
     *
     * @param CanvasSize  Rendered canvas size
     * @param NumRows     Number of rows
     * @param Columns     Max columns
     * @param SpacingX    Horizontal spacing (screen pixels)
     * @param SpacingY    Vertical spacing (screen pixels)
     */
    FVector2D ComputeSlotSize(
        FVector2D CanvasSize,
        int32     NumRows,
        int32     Columns,
        float     SpacingX,
        float     SpacingY) const;

    /**
     * Push the correct ESpecialSlotState to every slot based on current ammo
     * count and fire cooldown. O(N), event-driven only.
     */
    void RefreshAllSlotStates();

    // -----------------------------------------------------------------------
    //  Delegate callbacks
    // -----------------------------------------------------------------------

    UFUNCTION() void OnAmmoChanged(int32 NormalCount, int32 SpecialCount);
    UFUNCTION() void OnFireCooldownComplete();
};