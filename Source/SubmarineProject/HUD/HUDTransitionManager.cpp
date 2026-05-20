// Fill out your copyright notice in the Description page of Project Settings.

#include "HUD/HUDTransitionManager.h"
#include "HUD/SubmarineHUDComponent.h"
#include "HUD/MainHUDWidget.h"
#include "HUD/BaseHUDModule.h"
#include "HUD/SubmarineHUDSettings.h"
#include "HUD/HUDGlobalDefaults.h"
#include "Match/RuntimeMatchSettings.h"
#include "Submarine/SubmarinePawn.h"
#include "SpectatorTrackerComponent.h"
#include "CameraEdit/ScreenFadeComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "Components/Image.h"
#include "Components/OverlaySlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetTree.h"

UHUDTransitionManager::UHUDTransitionManager()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UHUDTransitionManager::BeginPlay()
{
    Super::BeginPlay();

    AActor* Owner = GetOwner();
    if (!Owner) return;

    // ScreenFadeComponent is on the PlayerController (moved there in Phase 5)
    // Try owner first, then GameMode as fallback
    UScreenFadeComponent* FadeComp = Owner->FindComponentByClass<UScreenFadeComponent>();
    if (!FadeComp)
    {
        // Fallback: check GameMode
        if (AGameModeBase* GM = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr)
            FadeComp = GM->FindComponentByClass<UScreenFadeComponent>();
    }

    if (FadeComp)
    {
        FadeComp->OnFadeAlphaChanged.AddDynamic(
            this, &UHUDTransitionManager::OnScreenFadeAlphaChanged);
        UE_LOG(LogTemp, Log,
            TEXT("[HUDTransitionManager] Bound to ScreenFadeComponent for HUD opacity sync"));
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[HUDTransitionManager] No ScreenFadeComponent found -- "
                "HUD will not mirror camera fades"));
    }
}

// ---------------------------------------------------------------------------
//  TickComponent — drives the fade state machine
// ---------------------------------------------------------------------------
void UHUDTransitionManager::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // --- HUD fade (HUDFadeIn / HUDFadeOut) ---
    // This is the only fade system. TransitionToContext is always immediate.
    // bHUDFadeActive blocks OnScreenFadeAlphaChanged from overriding opacity.
    if (bHUDFadeActive)
    {
        HUDFadeTimer += DeltaTime;
        const float T = FMath::Clamp(HUDFadeTimer / FMath::Max(HUDFadeDuration, 0.001f), 0.f, 1.f);
        
        if (bHUDDarknessMode)
        {
            // Widget stays at 1; animate the black overlay
            // FadeIn = overlay goes 1->0 (black lifts to reveal colours)
            // FadeOut = overlay goes 0->1 (black descends over the UI)
            SetHUDDarknessOverlayOpacity(bHUDFadingIn ? 1.f - T : T);
        }
        else
        {
            SetHUDOpacity(bHUDFadingIn ? T : 1.f - T);
        }

        if (HUDFadeTimer >= HUDFadeDuration)
        {
            if (bHUDDarknessMode)
                SetHUDDarknessOverlayOpacity(bHUDFadingIn ? 0.f : 1.f);
            else
                SetHUDOpacity(bHUDFadingIn ? 1.f : 0.f);
            bHUDFadeActive = false;
            UE_LOG(LogTemp, Log,
                TEXT("[HUDTransitionManager] HUD fade %s complete"),
                bHUDFadingIn ? TEXT("IN") : TEXT("OUT"));
        }
    }

    // Disable tick when no HUD fade is running
    if (!bHUDFadeActive)
        PrimaryComponentTick.SetTickFunctionEnable(false);
}

// ---------------------------------------------------------------------------
//  SetRuntimeSettings
// ---------------------------------------------------------------------------
void UHUDTransitionManager::SetRuntimeSettings(
    URuntimeMatchSettings* InSettings,
    UHUDGlobalDefaults* InGlobalDefaults)
{
    RuntimeSettings = InSettings;
    GlobalDefaults = InGlobalDefaults;
}

// ---------------------------------------------------------------------------
//  TransitionToContext
// ---------------------------------------------------------------------------
void UHUDTransitionManager::TransitionToContext(EHUDContext NewContext, ASubmarinePawn* NewTrackedPawn)
{
    if (NewContext == CurrentContext && !NewTrackedPawn)
        return;

    PendingContext = NewContext;
    PendingTrackedPawn = NewTrackedPawn;

    // Always swap immediately -- visual fading is done externally
    // via HUDFadeOut (before) and HUDFadeIn (after). FadeDuration ignored.
    ExecuteContextSwap(NewContext, NewTrackedPawn);
}

// ---------------------------------------------------------------------------
//  SetContextImmediate
// ---------------------------------------------------------------------------
void UHUDTransitionManager::SetContextImmediate(EHUDContext NewContext,
    ASubmarinePawn* NewTrackedPawn)
{
    TransitionToContext(NewContext, NewTrackedPawn);
}

// ---------------------------------------------------------------------------
//  ApplyModuleVisibility
// ---------------------------------------------------------------------------
void UHUDTransitionManager::ApplyModuleVisibility()
{
    USubmarineHUDComponent* HUDComp = GetHUDComponent();
    if (!HUDComp) return;

    UMainHUDWidget* Root = HUDComp->GetRootWidget();
    if (!Root) return;

    // Iterate all active modules and apply visibility based on HiddenInContexts
    for (int32 i = 0; i < Root->GetModuleCount(); ++i)
    {
        // GetModuleByIndex is not currently exposed — iterate via FindModule
        // For now, modules apply their own visibility in RefreshVisuals
        // when context is propagated. This method serves as a manual trigger.
    }

    // Trigger a full refresh so modules re-evaluate their visibility
    Root->SetDataSource(HUDComp->GetTrackedInterface());
}

// ---------------------------------------------------------------------------
//  ExecuteContextSwap
// ---------------------------------------------------------------------------
void UHUDTransitionManager::ExecuteContextSwap(EHUDContext NewContext,
    ASubmarinePawn* NewPawn)
{
    CurrentContext = NewContext;

    USubmarineHUDComponent* HUDComp = GetHUDComponent();
    if (!HUDComp) return;

    // Resolve new HUD settings
    USubmarineHUDSettings* NewSettings = nullptr;
    if (RuntimeSettings)
        NewSettings = RuntimeSettings->ResolveHUDSettings(NewContext, GlobalDefaults);

    if (!NewSettings)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[HUDTransitionManager] No HUD settings for context %d "
                "(valid for Cinematic/None). Hiding HUD."),
            (int32)NewContext);
        // No HUD for this context — hide entirely
        if (UMainHUDWidget* Root = HUDComp->GetRootWidget())
            Root->SetVisibility(ESlateVisibility::Hidden);
        return;
    }

    // Swap HUD settings (tears down old modules, builds new ones)
    HUDComp->SwapHUDSettings(NewSettings);

    // Update tracked submarine if provided
    if (NewPawn)
        HUDComp->SetTrackedSubmarine(NewPawn);

    // Auto-select a spectator target when entering Spectator or Replay context
    // and no explicit pawn was provided
    if (!NewPawn &&
        (NewContext == EHUDContext::Spectator ||
            NewContext == EHUDContext::Spectator_Splitscreen ||
            NewContext == EHUDContext::Replay))
    {
        AActor* Owner = GetOwner();
        if (Owner)
        {
            USpectatorTrackerComponent* Tracker =
                Owner->FindComponentByClass<USpectatorTrackerComponent>();
            if (Tracker)
            {
                ASubmarinePawn* AutoTarget = Tracker->AutoSelectTarget();
                UE_LOG(LogTemp, Log,
                    TEXT("[HUDTransitionManager] Auto-selected spectator target: '%s'"),
                    AutoTarget ? *AutoTarget->GetName() : TEXT("None"));
            }
        }
    }

    // Apply module visibility pass
    ApplyModuleVisibility();

    UE_LOG(LogTemp, Log,
        TEXT("[HUDTransitionManager] Context swap complete: context=%d  "
            "settings='%s'  tracked='%s'"),
        (int32)NewContext,
        *NewSettings->GetName(),
        NewPawn ? *NewPawn->GetName() : TEXT("unchanged"));
}

// ---------------------------------------------------------------------------
//  HUDFadeIn
// ---------------------------------------------------------------------------
void UHUDTransitionManager::HUDFadeIn(float Duration, bool bDarknessMode)
{
    bHUDFadeActive = true;
    bHUDFadingIn = true;
    bHUDDarknessMode = bDarknessMode;
    HUDFadeTimer = 0.f;
    HUDFadeDuration = FMath::Max(Duration, 0.001f);
    if (bDarknessMode)
    {
        // Widget stays fully visible; show black overlay at full opacity
        SetHUDOpacity(1.f);
        SetHUDDarknessOverlayOpacity(1.f);
    }
    else
    {
        SetHUDOpacity(0.f);  // start invisible
        SetHUDDarknessOverlayOpacity(0.f);
    }
    PrimaryComponentTick.SetTickFunctionEnable(true);
    UE_LOG(LogTemp, Log,
        TEXT("[HUDTransitionManager] HUDFadeIn started (duration=%.2fs)"), Duration);
}

// ---------------------------------------------------------------------------
//  HUDFadeOut
// ---------------------------------------------------------------------------
void UHUDTransitionManager::HUDFadeOut(float Duration, bool bDarknessMode)
{
    bHUDFadeActive = true;
    bHUDFadingIn = false;
    bHUDDarknessMode = bDarknessMode;
    HUDFadeTimer = 0.f;
    HUDFadeDuration = FMath::Max(Duration, 0.001f);
    if (bDarknessMode)
    {
        // Widget stays fully visible; overlay starts transparent, fades to black
        SetHUDOpacity(1.f);
        SetHUDDarknessOverlayOpacity(0.f);
    }
    else
    {
        SetHUDOpacity(1.f);  // start fully visible
        SetHUDDarknessOverlayOpacity(0.f);
    }
    PrimaryComponentTick.SetTickFunctionEnable(true);
    UE_LOG(LogTemp, Log,
        TEXT("[HUDTransitionManager] HUDFadeOut started (duration=%.2fs)"), Duration);
}

// ---------------------------------------------------------------------------
//  SetHUDOpacity
// ---------------------------------------------------------------------------
void UHUDTransitionManager::SetHUDOpacity(float Opacity)
{
    USubmarineHUDComponent* HUDComp = GetHUDComponent();
    if (!HUDComp) return;

    if (UMainHUDWidget* Root = HUDComp->GetRootWidget())
        Root->SetRenderOpacity(FMath::Clamp(Opacity, 0.f, 1.f));
}

// ---------------------------------------------------------------------------
//  SetDarknessOverlayOpacity
// ---------------------------------------------------------------------------
void UHUDTransitionManager::SetHUDDarknessOverlayOpacity(float Opacity)
{
    USubmarineHUDComponent* HUDComp = GetHUDComponent();
    if (!HUDComp) return;
    UMainHUDWidget* Root = HUDComp->GetRootWidget();
    if (!Root) return;

    // Create the overlay image if it doesn't exist yet
    if (!IsValid(DarknessOverlay))
    {
        // Add a full-screen black image on top of the RootCanvas
        DarknessOverlay = Root->WidgetTree
            ? Root->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),
                TEXT("DarknessOverlay"))
            : nullptr;
        if (DarknessOverlay)
        {
            // Solid black brush
            FSlateBrush Brush;
            Brush.TintColor = FSlateColor(FLinearColor::Black);
            Brush.DrawAs = ESlateBrushDrawType::Box;
            Brush.Margin = FVector4(0.f, 0.f, 0.f, 0.f);
            DarknessOverlay->SetBrush(Brush);

            // Add to root canvas spanning full area
            if (UCanvasPanel* Canvas = Root->GetRootCanvas())
            {
                UCanvasPanelSlot* CanvaSlot = Canvas->AddChildToCanvas(DarknessOverlay);
                if (CanvaSlot)
                {
                    // Use the widget's design resolution -- same source as ApplyLayoutToSlot.
                    // This is the resolution all module positions are authored against,
                    // so the overlay exactly covers the HUD canvas.
                    FVector2D OverlaySize(1920.f, 1080.f);
                    if (USubmarineHUDSettings* Settings = Root->GetCachedSettings())
                    {
                        if (!Settings->DesignResolution.IsNearlyZero())
                            OverlaySize = Settings->DesignResolution;
                    }
                    CanvaSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
                    CanvaSlot->SetAlignment(FVector2D::ZeroVector);
                    CanvaSlot->SetPosition(FVector2D::ZeroVector);
                    CanvaSlot->SetSize(OverlaySize);
                    CanvaSlot->SetAutoSize(false);
                    CanvaSlot->SetZOrder(999);
                    UE_LOG(LogTemp, Log,
                        TEXT("[HUDTransitionManager] DarknessOverlay size=(%.0f,%.0f)"),
                        OverlaySize.X, OverlaySize.Y);
                }
            }
            UE_LOG(LogTemp, Log, TEXT("[HUDTransitionManager] DarknessOverlay created"));
        }
    }

    if (IsValid(DarknessOverlay))
        DarknessOverlay->SetRenderOpacity(FMath::Clamp(Opacity, 0.f, 1.f));
}

// ---------------------------------------------------------------------------
//  GetHUDComponent
// ---------------------------------------------------------------------------
USubmarineHUDComponent* UHUDTransitionManager::GetHUDComponent() const
{
    AActor* Owner = GetOwner();
    if (!Owner) return nullptr;
    return Owner->FindComponentByClass<USubmarineHUDComponent>();
}