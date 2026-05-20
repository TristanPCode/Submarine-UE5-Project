// Fill out your copyright notice in the Description page of Project Settings.

#include "Billboard/InfoBillboardWidget.h"
#include "Billboard/InfoBillboardSettings.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/OverlaySlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelSlot.h"

// ---------------------------------------------------------------------------
//  ApplySettings
// ---------------------------------------------------------------------------
void UInfoBillboardWidget::ApplySettings(UInfoBillboardSettings* Settings,
    const FString& TextValue)
{
    if (!Settings) return;
    CachedSettings = Settings;

    // Overall widget opacity
    SetRenderOpacity(FMath::Clamp(Settings->OverallOpacity, 0.f, 1.f));

    // -- Text --
    if (BillboardContent)
    {
        BillboardContent->SetText(FText::FromString(TextValue));
        BillboardContent->SetRenderOpacity(FMath::Clamp(Settings->TextOpacity, 0.f, 1.f));

        FLinearColor Color = Settings->TextColor;
        // bUseTeamColor is applied externally by the component (it has team info)
        BillboardContent->SetColorAndOpacity(Color);

        // Font size
        FSlateFontInfo FontInfo = Settings->Font.HasValidFont()
            ? Settings->Font
            : BillboardContent->GetFont();
        FontInfo.Size = FMath::RoundToInt(Settings->FontSize);
        BillboardContent->SetFont(FontInfo);

        // Text alignment within background
        BillboardContent->SetJustification(
            Settings->TextHAlign == HAlign_Left ? ETextJustify::Left :
            Settings->TextHAlign == HAlign_Right ? ETextJustify::Right :
            ETextJustify::Center);
        // Vertical alignment: set on the widget slot if it exists
        if (UPanelSlot* PanelSlot = BillboardContent->Slot)
        {
            if (UOverlaySlot* OSlot = Cast<UOverlaySlot>(PanelSlot))
            {
                OSlot->SetHorizontalAlignment(Settings->TextHAlign.GetValue());
                OSlot->SetVerticalAlignment(Settings->TextVAlign.GetValue());
            }
            else if (UCanvasPanelSlot* CSlot = Cast<UCanvasPanelSlot>(PanelSlot))
            {
                // Canvas slots don't have H/V alignment for child content --
                // SetAlignment() sets the widget's own pivot, not content alignment.
                // Strategy: position the text block explicitly using Position + Size
                // based on BackgroundSize, then use SetJustification (H) and
                // manual Y offset (V) to place it correctly.
                // Use DA BackgroundSize if override is set, otherwise fall back
                // to the widget's own desired size (from Blueprint SizeBox/Canvas).
                FVector2D BgSize = FVector2D(200.f, 60.f);
                if (Settings->bOverrideDrawSize && !Settings->BackgroundSize.IsNearlyZero())
                    BgSize = Settings->BackgroundSize;
                else
                {
                    const FVector2D Desired = GetDesiredSize();
                    if (!Desired.IsNearlyZero()) BgSize = Desired;
                }

                // Estimate rendered text height from font size.
                // UE renders text at roughly 1.2x the font size in pixels
                // at the design resolution (96 DPI assumption).
                const float EstTextHeight = Settings->FontSize * 1.2f;
                // Center: position so the text block is vertically centered.
                // VOffset = 50% of BgHeight minus half the text height.
                const float CenterOffset = FMath::Max(0.f,
                    (BgSize.Y * 0.5f) - (EstTextHeight * 0.5f));
                const float VOffset =
                    Settings->TextVAlign == VAlign_Top ? 0.f :
                    Settings->TextVAlign == VAlign_Bottom ? FMath::Max(0.f, BgSize.Y - EstTextHeight) :
                    CenterOffset;

                CSlot->SetAutoSize(false);
                CSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
                CSlot->SetAlignment(FVector2D(0.f, 0.f));
                // Apply DA H/V offsets on top of the alignment-computed position
                CSlot->SetPosition(FVector2D(Settings->TextOffsetH, VOffset + Settings->TextOffsetV));
                CSlot->SetSize(FVector2D(BgSize.X, BgSize.Y - VOffset));
            }
        }

        // Outline (hard shadow offset in 4 directions = outline appearance)
        // This is the most readable approach for world-space billboards.
        // bEnableOutline takes priority over bEnableGlow since they share the shadow system.
        if (Settings->bEnableOutline)
        {
            // Renders shadow at (OS,0), (-OS,0), (0,OS), (0,-OS) is not possible
            // with a single UTextBlock shadow. Best approximation: use small
            // diagonal offset that covers all quadrants visually.
            // The outline BP layers (OutlineN/S/E/W TextBlocks) give the true result.
            const float OS = FMath::Clamp(Settings->OutlineSize, 0.5f, 8.f);
            BillboardContent->SetShadowColorAndOpacity(Settings->OutlineColor);
            BillboardContent->SetShadowOffset(FVector2D(OS * 0.7f, OS * 0.7f));
            // Drive the 4-direction outline layers (optional BP binding)
            SetOutlineLayersText(FText::FromString(TextValue), Settings->OutlineColor, OS);
        }
        // Glow (applied via shadow color as approximation in UMG)
        else if (Settings->bEnableGlow)
        {
            // Glow: centered soft shadow (zero offset, colored)
            FLinearColor GlowCol = Settings->GlowColor;
            GlowCol.A = FMath::Clamp(Settings->GlowIntensity, 0.f, 1.f);
            BillboardContent->SetShadowColorAndOpacity(GlowCol);
            BillboardContent->SetShadowOffset(FVector2D(0.f, 0.f));
        }
        else
        {
            BillboardContent->SetShadowColorAndOpacity(FLinearColor::Transparent);
        }
    }

    // -- Background --
    if (Background)
    {
        const float BgAlpha = FMath::Clamp(Settings->BackgroundOpacity, 0.f, 1.f);

        if (Settings->BackgroundTexture)
        {
            // Texture provided: use it tinted with BackgroundColor
            FLinearColor BgColor = Settings->BackgroundColor;
            BgColor.A = BgAlpha;
            Background->SetBrushFromTexture(Settings->BackgroundTexture);
            Background->SetColorAndOpacity(BgColor);
        }
        else
        {
            // No texture: build a solid colored brush so the color is visible
            // A plain UImage with no brush resource renders nothing by default.
            FSlateBrush Brush;
            FLinearColor BgColor = Settings->BackgroundColor;
            BgColor.A = BgAlpha;
            Brush.TintColor = FSlateColor(BgColor);
            Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
            Brush.OutlineSettings.CornerRadii = FVector4(4.f, 4.f, 4.f, 4.f);
            Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
            Background->SetBrush(Brush);
        }

        Background->SetVisibility(
            BgAlpha > 0.f
            ? ESlateVisibility::HitTestInvisible
            : ESlateVisibility::Collapsed);
    }
}

// ---------------------------------------------------------------------------
//  UpdateText
// ---------------------------------------------------------------------------
void UInfoBillboardWidget::UpdateText(const FString& NewText)
{
    if (BillboardContent)
        BillboardContent->SetText(FText::FromString(NewText));
}

// ---------------------------------------------------------------------------
//  SetOutlineLayersText
// ---------------------------------------------------------------------------
void UInfoBillboardWidget::SetOutlineLayersText(
    const FText& Text, FLinearColor Color, float Offset)
{
    // Drive optional outline TextBlock layers if they exist in the Blueprint.
    // Each layer should be positioned offset by Offset pixels in its direction.
    // The canvas slot position offset is set here dynamically.
    const bool bVisible = (Color.A > 0.f && Offset > 0.f);
    const ESlateVisibility Vis = bVisible
        ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;

    // Helper: set text, color, visibility and slot offset
    auto ApplyLayer = [&](UTextBlock* TB, FVector2D SlotOffset)
        {
            if (!TB) return;
            TB->SetVisibility(Vis);
            if (!bVisible) return;
            TB->SetText(Text);
            TB->SetColorAndOpacity(Color);
            // Keep font matching BillboardContent
            if (BillboardContent)
                TB->SetFont(BillboardContent->GetFont());
            // Shift the slot position
            if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(TB->Slot))
            {
                const FVector2D BasePos = BillboardContent
                    ? (Cast<UCanvasPanelSlot>(BillboardContent->Slot)
                        ? Cast<UCanvasPanelSlot>(BillboardContent->Slot)->GetPosition()
                        : FVector2D::ZeroVector)
                    : FVector2D::ZeroVector;
                Slot->SetPosition(BasePos + SlotOffset);
                if (BillboardContent)
                    if (UCanvasPanelSlot* BS = Cast<UCanvasPanelSlot>(BillboardContent->Slot))
                        Slot->SetSize(BS->GetSize());
            }
        };

    ApplyLayer(OutlineN, FVector2D(0.f, -Offset));
    ApplyLayer(OutlineS, FVector2D(0.f, +Offset));
    ApplyLayer(OutlineE, FVector2D(+Offset, 0.f));
    ApplyLayer(OutlineW, FVector2D(-Offset, 0.f));
}

// ---------------------------------------------------------------------------
//  OverrideTextColor
// ---------------------------------------------------------------------------
void UInfoBillboardWidget::OverrideTextColor(FLinearColor Color)
{
    if (BillboardContent)
        BillboardContent->SetColorAndOpacity(Color);
}