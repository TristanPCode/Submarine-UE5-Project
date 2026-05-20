#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TrackableSubmarine.h"
#include "SubmarineHUDDebugSettings.h"
#include "SubmarineHUDComponent.generated.h"

class USubmarineHUDSettings;
class USubmarineHUDDebugSettings;
class UMainHUDWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTrackedSubmarineChanged,
    AActor*, NewTarget);

/**
 * USubmarineHUDComponent
 *
 * Attach to APlayerController (one instance per local player).
 * Uses AddToPlayerScreen() for correct split-screen scoping.
 */
UCLASS(ClassGroup = (Submarine), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API USubmarineHUDComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USubmarineHUDComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // -----------------------------------------------------------------------
    //  Configuration
    // -----------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
    TObjectPtr<USubmarineHUDSettings> HUDSettings;

    // -----------------------------------------------------------------------
    //  Public API
    // -----------------------------------------------------------------------

    /**
     * Set or change the submarine tracked by the HUD.
     * Target must implement ITrackableSubmarine.
     * Pass nullptr to explicitly clear.
     */
    UFUNCTION(BlueprintCallable, Category = "HUD")
    void SetTrackedSubmarine(AActor* Target);

    UFUNCTION(BlueprintPure, Category = "HUD")
    AActor* GetTrackedActor() const { return TrackedActor.Get(); }

    /**
     * Expose the tracked interface for HUDTransitionManager to re-apply
     * after a DataAsset swap.
     */
    const TScriptInterface<ITrackableSubmarine>& GetTrackedInterface() const
    {
        return TrackedInterface;
    }

    UFUNCTION(BlueprintPure, Category = "HUD")
    UMainHUDWidget* GetRootWidget() const { return RootWidget; }

    UFUNCTION(BlueprintCallable, Category = "HUD")
    void SetHUDVisible(bool bVisible);

    /**
     * Swap the HUD DataAsset and fully reinitialize all modules.
     * Called by UHUDTransitionManager during context transitions.
     * Tears down existing modules, builds new ones from NewSettings.
     * Re-applies the currently tracked submarine after reinitialization.
     */
    UFUNCTION(BlueprintCallable, Category = "HUD")
    void SwapHUDSettings(USubmarineHUDSettings* NewSettings);

    // -----------------------------------------------------------------------
    //  Delegates
    // -----------------------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "HUD|Events")
    FOnTrackedSubmarineChanged OnTrackedSubmarineChanged;

private:

    TWeakObjectPtr<AActor>                TrackedActor;
    TScriptInterface<ITrackableSubmarine> TrackedInterface;

    UPROPERTY()
    TObjectPtr<UMainHUDWidget> RootWidget;

    // Cached debug settings (pulled from HUDSettings)
    UPROPERTY()
    TObjectPtr<USubmarineHUDDebugSettings> DebugSettings;

    void CreateHUDWidget();

    UFUNCTION()
    void HandleTrackedSubmarineChanged(AActor* NewTarget);

    bool ShouldLog(bool bFlag) const
    {
        return IsValid(DebugSettings.Get()) && bFlag;
    }
};