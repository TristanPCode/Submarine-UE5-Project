#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InfoBillboardSettings.h"
#include "InfoBillboardWidget.generated.h"

class UTextBlock;
class UImage;
class UBorder;
class UInfoBillboardSettings;

/**
 * UInfoBillboardWidget
 *
 * World-space UMG widget displayed above submarines and torpedoes.
 * Rendered by USubmarineInfoBillboardComponent (UWidgetComponent).
 *
 * Blueprint setup (BP_InfoBillboardWidget):
 *   Create a Blueprint subclass of this class.
 *   Required BindWidget members:
 *     - BillboardText  (UTextBlock)
 *   Optional BindWidget members:
 *     - Background     (UImage or UBorder)
 *
 * The widget should be small and transparent by default.
 * USubmarineInfoBillboardComponent drives all visual properties via
 * ApplySettings() and UpdateText().
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class SUBMARINEPROJECT_API UInfoBillboardWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    /**
     * Apply full settings from a UInfoBillboardSettings DA.
     * Called when context changes or settings are first applied.
     *
     * @param Settings  The settings to apply
     * @param TextValue The already-evaluated text string (no {tokens})
     */
    UFUNCTION(BlueprintCallable, Category = "Billboard")
    void ApplySettings(UInfoBillboardSettings* Settings, const FString& TextValue);

    /**
     * Update only the text content (called each tick for dynamic fields).
     * Does not re-apply layout/color/font -- only the string.
     */
    UFUNCTION(BlueprintCallable, Category = "Billboard")
    void UpdateText(const FString& NewText);

    /** Override the text color (called by component after ApplySettings for team color). */
    UFUNCTION(BlueprintCallable, Category = "Billboard")
    void OverrideTextColor(FLinearColor Color);

    /**
     * Drive the 4 optional outline TextBlock layers.
     * Call after ApplySettings when bEnableOutline is true.
     * If the layers don't exist in the BP, this is a no-op.
     */
    void SetOutlineLayersText(const FText& Text, FLinearColor Color, float Offset);

protected:

    UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
    TObjectPtr<UTextBlock> BillboardContent;

    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
    TObjectPtr<UImage> Background;

    // -----------------------------------------------------------------------
    //  Optional 4-direction outline layers
    //  Add these to your BP_InfoBillboardWidget for true crisp outlines:
    //  4 UTextBlock widgets named OutlineN, OutlineS, OutlineE, OutlineW,
    //  placed behind BillboardContent, offset by OutlineSize pixels.
    //  Set their text/font to match BillboardContent but color = OutlineColor.
    // -----------------------------------------------------------------------
    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> OutlineN;  // offset (0, -OS)

    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> OutlineS;  // offset (0, +OS)

    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> OutlineE;  // offset (+OS, 0)

    UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> OutlineW;  // offset (-OS, 0)

private:

    UPROPERTY()
    TObjectPtr<UInfoBillboardSettings> CachedSettings;
};