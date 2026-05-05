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

    // -----------------------------------------------------------------------
    //  ITrackableSubmarine (ASubmarinePawn implementation)
    // -----------------------------------------------------------------------

    /** Log delegate getter calls (very verbose -- disable after confirming bindings work). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Interface")
    bool bLogDelegateGetters = false;
};