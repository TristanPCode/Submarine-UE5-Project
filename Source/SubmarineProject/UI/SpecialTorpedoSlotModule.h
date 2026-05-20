#pragma once

#include "CoreMinimal.h"
#include "HUD/BaseHUDModule.h"
#include "TorpedoCharacteristics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SpecialTorpedoSlotModule.generated.h"

class UImage;

/**
 * ESpecialSlotState
 *
 * Visual state of a single special torpedo slot.
 */
UENUM(BlueprintType)
enum class ESpecialSlotState : uint8
{
    Ready,     // available, shows glow animation
    Cooldown,  // fire cooldown active (temporary -- reverts to Ready when done)
    Used       // consumed, permanently greyed out until resupply
};

/**
 * USpecialTorpedoSlotModule
 *
 * Single icon slot for one special torpedo.
 * Purely visual -- no delegate bindings.
 * Parent (USpecialAmmoModule) controls state via SetSlotState().
 *
 * One MID: M_SpecialSlot
 *   MainTexture   (Texture2D)  -- set from IconPerType[SlotType] at init
 *   GlowMask      (Texture2D)  -- optional; white fallback = global glow
 *   GlowIntensity (float)      -- animated in tick when Ready
 *   GlowPulseSpeed(float)      -- from config
 *   State         (float)      -- 0=Used, 1=Ready, 2=Cooldown
 *
 * Tick: enabled only when State == Ready (glow pulse).
 *
 * Config keys (FHUDModuleConfig):
 *   Materials:  MatSpecialSlot
 *   Textures:   GlowMask (optional)
 *   Floats:     GlowPulseSpeed
 *   IconPerType: maps ETorpedoType -> UTexture2D*
 *   SlotTypes:  per-slot type array (read by parent; slot reads its index)
 *
 * Blueprint setup:
 *   Create BP_SpecialTorpedoSlotModule inheriting from this class.
 *   Add a UImage named "SlotImage" as root (or inside a panel).
 */
UCLASS(Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API USpecialTorpedoSlotModule : public UBaseHUDModule
{
    GENERATED_BODY()

public:

    /**
     * Called once by USpecialAmmoModule after creation.
     * Sets the torpedo type and pushes the correct icon texture to the MID.
     * Never called again at runtime.
     */
    void InitSlot(int32 InSlotIndex, ETorpedoType InSlotType);

    /**
     * Update the visual state of this slot.
     * Called by USpecialAmmoModule on every relevant event.
     * Zero widget reconstruction -- MID parameter push only.
     */
    void SetSlotState(ESpecialSlotState NewState);

    UFUNCTION(BlueprintPure, Category = "Slot")
    ESpecialSlotState GetSlotState() const { return CurrentState; }

    UFUNCTION(BlueprintPure, Category = "Slot")
    int32 GetSlotIndex() const { return SlotIndex; }

protected:

    // -----------------------------------------------------------------------
    //  UMG widgets (bound from Blueprint)
    // -----------------------------------------------------------------------

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UImage> SlotImage;

    // -----------------------------------------------------------------------
    //  UBaseHUDModule overrides
    // -----------------------------------------------------------------------

    // Slots have no delegate bindings -- parent drives state.
    virtual void BindToDataSource()     override {}
    virtual void UnbindFromDataSource() override {}

    UFUNCTION()
    virtual void RefreshVisuals_Implementation() override;

    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> SlotMID;

    ESpecialSlotState CurrentState = ESpecialSlotState::Used;
    ETorpedoType      SlotType = ETorpedoType::Normal;
    int32             SlotIndex = 0;

    float GlowTimer = 0.f;

    /** Create SlotMID from MatSpecialSlot and set initial texture. */
    bool CreateMID();

    /** Push current state to MID parameters. */
    void ApplyStateToMaterial();
};