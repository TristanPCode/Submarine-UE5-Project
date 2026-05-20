#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TrackableSubmarine.h"
#include "SubmarineHUDSettings.h"
#include "MainHUDWidget.generated.h"

class USubmarineHUDSettings;
class USubmarineHUDDebugSettings;
class UBaseHUDModule;
class UCanvasPanel;
class USubmarineHUDSettings;

/**
 * UMainHUDWidget
 *
 * Root HUD container. Created and owned by USubmarineHUDComponent.
 *
 * Blueprint setup:
 *   Create a Blueprint subclass (BP_MainHUDWidget).
 *   Add a UCanvasPanel named "RootCanvas" as the root widget.
 *   Assign the Blueprint class to USubmarineHUDSettings::WidgetClass.
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API UMainHUDWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    UFUNCTION(BlueprintCallable, Category = "MainHUD")
    void InitializeHUD(USubmarineHUDSettings* Settings);

    UFUNCTION(BlueprintCallable, Category = "MainHUD")
    void SetDataSource(const TScriptInterface<ITrackableSubmarine>& NewSource);

    UFUNCTION(BlueprintPure, Category = "MainHUD")
    UBaseHUDModule* FindModule(FName ModuleName) const;

    UFUNCTION(BlueprintPure, Category = "MainHUD")
    int32 GetModuleCount() const { return ActiveModules.Num(); }

    const TScriptInterface<ITrackableSubmarine>& GetCachedDataSource() const
    {
        return CachedDataSource;
    }

    UCanvasPanel* GetRootCanvas() const { return RootCanvas; }
    USubmarineHUDSettings* GetCachedSettings() const { return CachedSettings; }

protected:

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UCanvasPanel> RootCanvas;

    virtual void NativeDestruct() override;

private:

    UPROPERTY()
    TArray<TObjectPtr<UBaseHUDModule>> ActiveModules;

    TArray<FName> ModuleNames;

    UPROPERTY()
    TObjectPtr<USubmarineHUDSettings> CachedSettings;

    TScriptInterface<ITrackableSubmarine> CachedDataSource;

    // Cached debug settings pointer (pulled from CachedSettings)
    UPROPERTY()
    TObjectPtr<USubmarineHUDDebugSettings> DebugSettings;

    UBaseHUDModule* CreateAndAddModule(const FHUDModuleConfig& ModuleConfig);

    static void ApplyLayoutToSlot(class UCanvasPanelSlot* CanvasSlot,
        const FHUDModuleConfig& Config, APlayerController* OwningPlayer, USubmarineHUDDebugSettings* InDebugSettings, FVector2D InDesignResolution);

    void TearDownModules();

    bool ShouldLog(bool bFlag) const
    {
        return IsValid(DebugSettings.Get()) && bFlag;
    }
};