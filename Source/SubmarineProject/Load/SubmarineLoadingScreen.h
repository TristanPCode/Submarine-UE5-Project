#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SubmarineLoadingScreen.generated.h"

class UImage;
class UProgressBar;
class UTextBlock;
class ULoadingScreenSettings;

/**
 * USubmarineLoadingScreen
 *
 * Full-screen loading screen widget shown while assets preload.
 * Create a Blueprint subclass (BP_LoadingScreen) with the following
 * BindWidget members in the UMG designer:
 *
 *   - BackgroundImage   (UImage, fills the screen)
 *   - OverlayImage      (UImage, optional overlay on top)
 *   - ProgressBar       (UProgressBar, optional)
 *   - TipText           (UTextBlock, optional)
 *
 * All are optional -- the widget works even if some are absent.
 *
 * Usage (from GameMode):
 *   LoadingScreen = CreateWidget<USubmarineLoadingScreen>(PC, LoadingScreenClass);
 *   LoadingScreen->ApplySettings(LoadingScreenSettings);
 *   LoadingScreen->AddToPlayerScreen(100);  // Z-order 100 = on top of everything
 *
 * Update progress each tick (or poll):
 *   LoadingScreen->SetProgress(Loader->GetLoadProgress());
 *
 * Dismiss:
 *   LoadingScreen->RemoveFromParent();
 *   LoadingScreen = nullptr;
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API USubmarineLoadingScreen : public UUserWidget
{
    GENERATED_BODY()

public:

    /**
     * Apply settings from a LoadingScreenSettings DataAsset.
     * Call immediately after creation, before adding to viewport.
     */
    UFUNCTION(BlueprintCallable, Category = "LoadingScreen")
    void ApplySettings(ULoadingScreenSettings* Settings);

    /**
     * Update the progress bar.
     * @param Progress 0..1
     */
    UFUNCTION(BlueprintCallable, Category = "LoadingScreen")
    void SetProgress(float Progress);

protected:

    // BindWidget — optional, may be absent in Blueprint
    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
    TObjectPtr<UImage> BackgroundImage;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
    TObjectPtr<UImage> OverlayImage;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
    TObjectPtr<UProgressBar> ProgressBar;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TipText;
};