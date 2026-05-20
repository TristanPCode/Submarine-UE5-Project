#pragma once

#include "CoreMinimal.h"
#include "HUD/BaseHUDModule.h"
#include "NormalAmmoModule.generated.h"

class UCanvasPanel;
class UNumericDisplayModule;
class UTorpedoIconModule;

/**
 * UNormalAmmoModule
 *
 * Composite container owning two child modules:
 *   1. UNumericDisplayModule  -- ammo counter (reused from Phase 2)
 *   2. UTorpedoIconModule     -- torpedo icon with glow + reload overlay
 *
 * Layout is fully config-driven (normalized 0..1 relative to parent canvas).
 * Config keys:
 *   Floats:
 *     Counter_PosX, Counter_PosY   -- normalized position of counter child
 *     Counter_SizeX, Counter_SizeY -- normalized size of counter child
 *     Torpedo_PosX, Torpedo_PosY   -- normalized position of icon child
 *     Torpedo_SizeX, Torpedo_SizeY -- normalized size of icon child
 *
 * The child modules each have their own config populated from the parent's
 * config (materials, textures, floats, etc.) with their own ModuleName.
 * The parent config's Materials/Textures are passed down directly --
 * no duplication.
 *
 * Blueprint setup:
 *   Create BP_NormalAmmoModule inheriting from this class.
 *   Add a UCanvasPanel named "AmmoCanvas" as root.
 *   No child widgets needed -- child modules are created in C++.
 */
UCLASS(Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API UNormalAmmoModule : public UBaseHUDModule
{
    GENERATED_BODY()

protected:

    // -----------------------------------------------------------------------
    //  UMG widgets (bound from Blueprint)
    // -----------------------------------------------------------------------

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UCanvasPanel> AmmoCanvas;

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
    //  Child module references (owned via ChildModules array in base)
    // -----------------------------------------------------------------------

    UPROPERTY()
    TObjectPtr<UNumericDisplayModule> CounterModule;

    UPROPERTY()
    TObjectPtr<UTorpedoIconModule> IconModule;

    bool bChildrenCreated = false;

    // -----------------------------------------------------------------------
    //  Helpers
    // -----------------------------------------------------------------------

    /** Instantiate counter and icon child modules on AmmoCanvas. */
    void CreateChildModules();

    /**
     * Build a sub-config for a child module by copying relevant entries
     * from the parent config and assigning a child-specific ModuleName.
     */
    FHUDModuleConfig MakeChildConfig(FName ChildName) const;
};