#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TrackableSubmarine.h"
#include "SubmarineHUDSettings.h"
#include "BaseHUDModule.generated.h"

class USubmarineHUDDebugSettings;

/**
 * UBaseHUDModule
 *
 * Base class for all HUD modules.
 *
 * -----------------------------------------------------------------------
 *  Lifecycle
 * -----------------------------------------------------------------------
 *  1. UMainHUDWidget creates the module via CreateWidget<UBaseHUDModule>()
 *  2. SetConfig()      -- stores config, applies layout, calls RefreshVisuals()
 *  3. SetDataSource()  -- binds delegates, calls RefreshVisuals()
 *  4. On submarine switch: SetDataSource() called again
 *     -> UnbindFromDataSource() removes old bindings
 *     -> BindToDataSource() adds new bindings
 *     -> RefreshVisuals() immediately refreshes display
 *  5. On destruction: UnbindFromDataSource() called automatically
 *
 * -----------------------------------------------------------------------
 *  Event-driven vs Tick
 * -----------------------------------------------------------------------
 *  NativeTick is DISABLED by default.
 *  Call SetContinuousTickEnabled(true) in BindToDataSource() for modules
 *  that need continuous updates (Speedometer, Depth, Radar).
 *  Call SetContinuousTickEnabled(false) in UnbindFromDataSource().
 *
 * -----------------------------------------------------------------------
 *  Delegate binding pattern (Option B)
 * -----------------------------------------------------------------------
 *  Override BindToDataSource() and UnbindFromDataSource() in subclasses.
 *  Always validate the UObject before binding:
 *
 *    void UMyModule::BindToDataSource()
 *    {
 *        UObject* Obj = DataSource.GetObject();
 *        if (!IsValid(Obj)) return;
 *        DataSource->GetOnDamagedDelegate().AddDynamic(
 *            this, &UMyModule::HandleDamaged);
 *    }
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API UBaseHUDModule : public UUserWidget
{
    GENERATED_BODY()

public:

    // -----------------------------------------------------------------------
    //  Public API  (called by UMainHUDWidget)
    // -----------------------------------------------------------------------

    /**
     * Apply configuration from the DataAsset.
     * Called once after widget creation, before SetDataSource.
     */
    UFUNCTION(BlueprintCallable, Category = "HUDModule")
    void SetConfig(const FHUDModuleConfig& InConfig);

    /**
     * Set or change the data source.
     * Pass an empty TScriptInterface to disconnect (e.g. on submarine death).
     */
    UFUNCTION(BlueprintCallable, Category = "HUDModule")
    void SetDataSource(const TScriptInterface<ITrackableSubmarine>& NewSource);

    UFUNCTION(BlueprintPure, Category = "HUDModule")
    const TScriptInterface<ITrackableSubmarine>& GetDataSource() const { return DataSource; }

    UFUNCTION(BlueprintPure, Category = "HUDModule")
    const FHUDModuleConfig& GetConfig() const { return Config; }

    // -----------------------------------------------------------------------
    //  Tick control  (opt-in for continuous modules)
    // -----------------------------------------------------------------------

    /**
     * Enable or disable per-frame tick for this module.
     * Disabled by default.
     */
    UFUNCTION(BlueprintCallable, Category = "HUDModule")
    void SetContinuousTickEnabled(bool bEnabled);

    // -----------------------------------------------------------------------
    //  Debug settings (set by MainHUDWidget after creation)
    // -----------------------------------------------------------------------

    /**
     * Assigned by UMainHUDWidget immediately after CreateWidget.
     * Null = all module logging disabled.
     */
    UPROPERTY()
    TObjectPtr<USubmarineHUDDebugSettings> DebugSettings;

protected:

    // -----------------------------------------------------------------------
    //  Overridable interface for subclasses
    // -----------------------------------------------------------------------

    virtual void BindToDataSource() {}
    virtual void UnbindFromDataSource() {}

    UFUNCTION(BlueprintNativeEvent, Category = "HUDModule")
    void RefreshVisuals();
    virtual void RefreshVisuals_Implementation() {}

    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual void NativeDestruct() override;

    // -----------------------------------------------------------------------
    //  Protected state
    // -----------------------------------------------------------------------

    UPROPERTY(BlueprintReadOnly, Category = "HUDModule")
    TScriptInterface<ITrackableSubmarine> DataSource;

    UPROPERTY(BlueprintReadOnly, Category = "HUDModule")
    FHUDModuleConfig Config;

    // -----------------------------------------------------------------------
    //  Log helpers for subclasses
    // -----------------------------------------------------------------------

    bool ShouldLog(bool bFlag) const
    {
        return IsValid(DebugSettings) && bFlag;
    }

private:
    bool bContinuousTickEnabled = false;
};