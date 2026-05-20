#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LoadingScreenSettings.generated.h"

/**
 * ULoadingScreenSettings
 *
 * DataAsset that defines what the loading screen looks like for a given level
 * or match configuration.
 *
 * Assign one instance to ASubmarineGameMode::LoadingScreenSettings.
 * Different maps can use different instances with different artwork.
 *
 * Future use: tip texts, animated elements, level previews, etc.
 */
UCLASS(BlueprintType)
class SUBMARINEPROJECT_API ULoadingScreenSettings : public UDataAsset
{
    GENERATED_BODY()

public:

    /**
     * Background image displayed behind the loading screen.
     * Recommended: full 1920x1080 artwork, one per map/level.
     * Leave null to use a plain black background.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LoadingScreen")
    TObjectPtr<UTexture2D> BackgroundImage;

    /**
     * Optional overlay image drawn on top of the background
     * (e.g. a vignette, logo, or frame).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LoadingScreen")
    TObjectPtr<UTexture2D> OverlayImage;

    /**
     * Tint applied to the background image.
     * Default: white (no tint).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LoadingScreen")
    FLinearColor BackgroundTint = FLinearColor::White;

    /**
     * Optional loading tip or lore text displayed during loading.
     * Leave empty to show nothing.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LoadingScreen")
    FText LoadingTip;

    /**
     * Whether to show a progress bar.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LoadingScreen")
    bool bShowProgressBar = true;

    /**
     * Color of the progress bar fill.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LoadingScreen",
        meta = (EditCondition = "bShowProgressBar"))
    FLinearColor ProgressBarColor = FLinearColor(0.2f, 0.6f, 1.f, 1.f);
};