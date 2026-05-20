// Fill out your copyright notice in the Description page of Project Settings.

#include "Load/SubmarineLoadingScreen.h"
#include "Load/LoadingScreenSettings.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

// ---------------------------------------------------------------------------
//  ApplySettings
// ---------------------------------------------------------------------------
void USubmarineLoadingScreen::ApplySettings(ULoadingScreenSettings* Settings)
{
    if (!Settings) return;

    if (BackgroundImage)
    {
        if (Settings->BackgroundImage)
        {
            BackgroundImage->SetBrushFromTexture(Settings->BackgroundImage);
            BackgroundImage->SetColorAndOpacity(Settings->BackgroundTint);
            BackgroundImage->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        else
        {
            // No image -- plain black background
            BackgroundImage->SetColorAndOpacity(FLinearColor::Black);
        }
    }

    if (OverlayImage)
    {
        if (Settings->OverlayImage)
        {
            OverlayImage->SetBrushFromTexture(Settings->OverlayImage);
            OverlayImage->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        else
        {
            OverlayImage->SetVisibility(ESlateVisibility::Collapsed);
        }
    }

    if (ProgressBar)
    {
        ProgressBar->SetVisibility(
            Settings->bShowProgressBar
            ? ESlateVisibility::HitTestInvisible
            : ESlateVisibility::Collapsed);

        ProgressBar->SetFillColorAndOpacity(Settings->ProgressBarColor);
        ProgressBar->SetPercent(0.f);
    }

    if (TipText)
    {
        if (Settings->LoadingTip.IsEmpty())
        {
            TipText->SetVisibility(ESlateVisibility::Collapsed);
        }
        else
        {
            TipText->SetText(Settings->LoadingTip);
            TipText->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
    }
}

// ---------------------------------------------------------------------------
//  SetProgress
// ---------------------------------------------------------------------------
void USubmarineLoadingScreen::SetProgress(float Progress)
{
    if (ProgressBar)
        ProgressBar->SetPercent(FMath::Clamp(Progress, 0.f, 1.f));
}