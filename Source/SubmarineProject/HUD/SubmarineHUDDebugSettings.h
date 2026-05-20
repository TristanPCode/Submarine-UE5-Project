#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SubmarineHUDDebugSettings.generated.h"

/**
 * USubmarineHUDDebugSettings
 *
 * DataAsset controlling all HUD system log toggles.
 * Create one instance and assign it to USubmarineHUDSettings::DebugSettings.
 * Turn individual categories on/off without recompiling.
 *
 * All booleans default to true so the system is fully visible on first run.
 * Disable noisy categories once you've confirmed they work correctly.
 */
UCLASS(BlueprintType)
class SUBMARINEPROJECT_API USubmarineHUDDebugSettings : public UDataAsset
{
    GENERATED_BODY()

public:

    // -----------------------------------------------------------------------
    //  USubmarineHUDComponent
    // -----------------------------------------------------------------------

    /** Log when the HUD widget is created and added to the player screen. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Component")
    bool bLogHUDCreation = true;

    /** Log when SetTrackedSubmarine is called (target name, validation result). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Component")
    bool bLogTrackedSubmarineChange = true;

    /** Log when the HUD visibility changes. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Component")
    bool bLogVisibilityChange = true;

    // -----------------------------------------------------------------------
    //  UMainHUDWidget
    // -----------------------------------------------------------------------

    /** Log when InitializeHUD starts and how many module entries are found. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|MainWidget")
    bool bLogHUDInitialization = true;

    /** Log each module creation attempt (name, class, success/failure). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|MainWidget")
    bool bLogModuleCreation = true;

    /** Log when SetDataSource is forwarded to all modules. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|MainWidget")
    bool bLogDataSourcePropagation = true;

    /** Log when TearDownModules is called (module count being destroyed). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|MainWidget")
    bool bLogModuleTearDown = true;

    /** Log layout values applied to each module's canvas slot. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|MainWidget")
    bool bLogModuleLayout = false;

    // -----------------------------------------------------------------------
    //  UBaseHUDModule
    // -----------------------------------------------------------------------

    /** Log when SetConfig is called on a module. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module")
    bool bLogModuleSetConfig = true;

    /** Log when SetDataSource is called on a module (old and new source names). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module")
    bool bLogModuleSetDataSource = true;

    /** Log when BindToDataSource / UnbindFromDataSource are called. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module")
    bool bLogModuleBinding = true;

    /** Log when RefreshVisuals is called on a module. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module")
    bool bLogModuleRefresh = false;

    /** Log when continuous tick is enabled or disabled on a module. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module")
    bool bLogModuleTick = true;

    /** Log when NativeDestruct is called on a module. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module")
    bool bLogModuleDestruct = true;

    // =======================================================================
    //  PER-MODULE OVERRIDES
    //  Each pair: master bool (same as above) + optional name filter.
    //  If the filter string is empty, all instances of that module log.
    //  If set (e.g. "NormalAmmo_Icon"), only that named instance logs.
    // =======================================================================

    // -- Normal Ammo ---------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module|NormalAmmo")
    bool bLogNormalAmmo = false;

    /** Leave empty to log all NormalAmmo instances. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module|NormalAmmo",
        meta = (EditCondition = "bLogNormalAmmo"))
    FString NormalAmmoModuleFilter;

    // -- Torpedo Icon --------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module|TorpedoIcon")
    bool bLogTorpedoIcon = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module|TorpedoIcon",
        meta = (EditCondition = "bLogTorpedoIcon"))
    FString TorpedoIconModuleFilter;

    // -- Special Ammo --------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module|SpecialAmmo")
    bool bLogSpecialAmmo = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module|SpecialAmmo",
        meta = (EditCondition = "bLogSpecialAmmo"))
    FString SpecialAmmoModuleFilter;

    // -- Special Torpedo Slot ------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module|SpecialAmmo")
    bool bLogSpecialSlot = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module|SpecialAmmo",
        meta = (EditCondition = "bLogSpecialSlot"))
    FString SpecialSlotModuleFilter;

    // -- Numeric Display -----------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module|NumericDisplay")
    bool bLogNumericDisplay = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module|NumericDisplay",
        meta = (EditCondition = "bLogNumericDisplay"))
    FString NumericDisplayModuleFilter;

    // -- Modules --------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module|Radar")
    bool bLogRadar = false;

    /** Log PositionalIndicator slot initialization (crank/metal size messages). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug|Module")
    bool bLogPositionalIndicator = false;

    /** Log AmmoModule child creation. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug|Module")
    bool bLogAmmoModule = false;

    // -- HealthBar ----------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module|HealthBar")
    bool bLogHealthBar = false;

    // -- Pitch --------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module|Pitch")
    bool bLogPitch = false;

    // -- EngineState --------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Module|EngineState")
    bool bLogEngineState = false;

    // =======================================================================
    //  BILLBOARD
    // =======================================================================

    /** Log billboard init, context changes, and per-player visibility (5s). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Billboard")
    bool bLogBillboard = true;

    // =======================================================================
    //  RADAR COMPONENT (on SubmarinePawn)
    // =======================================================================

    /** Log per-target detection evaluation (fires every tick -- very spammy). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Component")
    bool bLogRadarDetection = false;

    /** Log TriggerScan calls and entry state at scan time. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Component")
    bool bLogRadarScan = false;

    /** Log entry summary (entries count, CircleRange, FOVRange) every 2s. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radar|Component")
    bool bLogRadarEntries = false;

    // =======================================================================
    //  HELPERS  (used in module code as ShouldLog(...) calls)
    // =======================================================================

    /**
     * Generic module log check.
     * Returns true if bCategory is on AND (Filter is empty OR ModuleName matches).
     */
    bool ShouldLogModule(bool bCategory, const FString& Filter,
        const FName& ModuleName) const
    {
        if (!bCategory) return false;
        return Filter.IsEmpty() || ModuleName.ToString().Contains(Filter);
    }

    /** Simple category check (no per-module filter). */
    bool ShouldLog(bool bCategory) const { return bCategory; }
};