// Fill out your copyright notice in the Description page of Project Settings.

#include "Billboard/BillboardDisplayComponent.h"
#include "Billboard/SubmarineInfoBillboardComponent.h"
#include "Billboard/InfoBillboardWidget.h"
#include "Billboard/InfoBillboardSettings.h"
#include "Billboard/InfoBillboardContextSettings.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "EngineUtils.h"

#define BB_LOG(fmt, ...) \
    if (DebugSettings && DebugSettings->bLogBillboard) \
        UE_LOG(LogTemp, Log, TEXT(fmt), ##__VA_ARGS__)

UBillboardDisplayComponent::UBillboardDisplayComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    // Full framerate for smooth position updates.
    // Logic (relationship recompute, text update) is throttled internally.
    PrimaryComponentTick.TickInterval = 0.f;
}

void UBillboardDisplayComponent::BeginPlay()
{
    Super::BeginPlay();
    UE_LOG(LogTemp, Log, TEXT("[BBDisplay] BeginPlay on PC='%s'"),
        GetPC() ? *GetPC()->GetName() : TEXT("?"));
}

APlayerController* UBillboardDisplayComponent::GetPC() const
{
    return Cast<APlayerController>(GetOwner());
}

void UBillboardDisplayComponent::SetBillboardContext(
    UInfoBillboardContextSettings* Context,
    int32 InLocalTeamIndex, URuntimeMatchSettings* InMatchSettings)
{
    BillboardCtx = Context;
    LocalTeamIndex = InLocalTeamIndex;
    MatchSettings = InMatchSettings;
    RebuildEntries();
    BB_LOG("[BBDisplay] SetBillboardContext: PC='%s'  Ctx=%s  Team=%d  Entries=%d",
        GetPC() ? *GetPC()->GetName() : TEXT("?"),
        Context ? *Context->GetName() : TEXT("NULL"),
        LocalTeamIndex,
        Entries.Num());
}

void UBillboardDisplayComponent::RebuildEntries()
{
    // Release all existing widgets back to pool
    for (FBillboardEntry& E : Entries)
        if (IsValid(E.Widget))
        {
            E.Widget->SetVisibility(ESlateVisibility::Collapsed);
            WidgetPool.Add(E.Widget);
        }
    Entries.Empty();

    if (!GetWorld() || !BillboardCtx) return;

    // Find all billboard components in the world
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        USubmarineInfoBillboardComponent* BBC =
            It->FindComponentByClass<USubmarineInfoBillboardComponent>();
        if (!BBC) continue;

        // Compute relationship from this PC's perspective
        APlayerController* PC = GetPC();
        const EBillboardRelationship Rel =
            BBC->ComputeRelationshipForPC(LocalTeamIndex, PC);
        UInfoBillboardSettings* Settings = BillboardCtx->GetSettings(Rel);

        FBillboardEntry Entry;
        Entry.Source = BBC;
        Entry.bVisible = Settings && Settings->bVisible;

        if (Entry.bVisible)
        {
            // Acquire a widget and add it to this PC's screen
            UInfoBillboardWidget* W = AcquireWidget();
            if (W)
            {
                Entry.Widget = W;
                if (PC)
                {
                    // ZOrder
                    float ZOrder = Settings->BillboardZOrder;
                    W->AddToPlayerScreen(ZOrder);
                    // Set explicit size so the widget fills the DA-configured area.
                    // Without this the widget collapses to desired size (often tiny).
                    if (Settings->bOverrideDrawSize && !Settings->BackgroundSize.IsNearlyZero())
                        W->SetDesiredSizeInViewport(Settings->BackgroundSize);
                }
                const FString Text = BBC->GetEvaluatedText(Settings->TextTemplate);
                W->ApplySettings(Settings, Text);
                if (Settings->bUseTeamColor)
                {
                    FLinearColor TeamColor = FLinearColor::White;
                    // Try to get the color from FMatchTeamSettings first
                    if (MatchSettings)
                    {
                        const FMatchTeamSettings& TS =
                            MatchSettings->GetTeamSettings(BBC->EntityTeamIndex);
                        TeamColor = TS.TeamColor;
                    }
                    else
                    {
                        // Fallback palette when MatchSettings not set
                        static const FLinearColor TeamColors[] = {
                            FLinearColor(0.2f, 0.5f, 1.f,  1.f),  // 0: blue
                            FLinearColor(1.f,  0.3f, 0.2f, 1.f),  // 1: red
                            FLinearColor(0.2f, 0.9f, 0.2f, 1.f),  // 2: green
                            FLinearColor(1.f,  0.8f, 0.1f, 1.f),  // 3: yellow
                            FLinearColor(0.8f, 0.2f, 0.9f, 1.f),  // 4: purple
                            FLinearColor(1.f,  0.5f, 0.0f, 1.f),  // 5: orange
                            FLinearColor(0.0f, 0.9f, 0.9f, 1.f),  // 6: cyan
                            FLinearColor(1.f,  1.f,  1.f,  1.f),  // 7: white
                        };
                        TeamColor = TeamColors[FMath::Clamp(BBC->EntityTeamIndex, 0, 7)];
                    }
                    W->OverrideTextColor(TeamColor);
                }
                W->SetVisibility(ESlateVisibility::HitTestInvisible);
            }
        }

        Entries.Add(Entry);
        BB_LOG("[BBDisplay] Entry: PC='%s'  Entity='%s'  Rel=%d  WillShow=%d",
            PC ? *PC->GetName() : TEXT("?"),
            *BBC->EntityDisplayName,
            (int32)Rel,
            Entry.bVisible ? 1 : 0);
    }
}

UInfoBillboardWidget* UBillboardDisplayComponent::AcquireWidget()
{
    // Return a pooled widget if available
    for (int32 i = WidgetPool.Num() - 1; i >= 0; --i)
    {
        if (IsValid(WidgetPool[i]))
        {
            UInfoBillboardWidget* W = WidgetPool[i];
            WidgetPool.RemoveAt(i);
            return W;
        }
    }
    // Create a new widget from the configured class
    if (!WidgetClass)
    {
        BB_LOG("[BBDisplay] AcquireWidget: WidgetClass not set on PC='%s' -- "
            "assign BP_InfoBillboardWidget to BillboardDisplayComponent.WidgetClass",
            GetPC() ? *GetPC()->GetName() : TEXT("?"));
        return nullptr;
    }
    APlayerController* PC = GetPC();
    if (!PC) return nullptr;
    UInfoBillboardWidget* NewWidget = CreateWidget<UInfoBillboardWidget>(PC, WidgetClass);
    if (!NewWidget)
        UE_LOG(LogTemp, Warning, TEXT("[BBDisplay] CreateWidget failed for WidgetClass='%s'"),
            *WidgetClass->GetName());
    return NewWidget;
}

void UBillboardDisplayComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    APlayerController* PC = GetPC();
    if (!PC || !BillboardCtx) return;

    // Slow logic: recompute relationships and update text at 10Hz
    LogicAccumulator += DeltaTime;
    const bool bDoLogic = (LogicAccumulator >= 0.1f);
    if (bDoLogic) LogicAccumulator = 0.f;

    for (FBillboardEntry& E : Entries)
    {
        if (!E.Source.IsValid() || !IsValid(E.Widget)) continue;

        USubmarineInfoBillboardComponent* BBC = E.Source.Get();

        if (bDoLogic)
        {
            // Recompute relationship (pawn may have changed, e.g. death)
            const EBillboardRelationship Rel =
                BBC->ComputeRelationshipForPC(LocalTeamIndex, PC);
            UInfoBillboardSettings* Settings = BillboardCtx->GetSettings(Rel);
            E.bVisible = Settings && Settings->bVisible && BBC->CheckIdentificationGateForPC(PC);
            E.CachedSettings = Settings;  // cache for fast tick

            if (E.bVisible && Settings)
            {
                const FString NewText = BBC->GetEvaluatedText(Settings->TextTemplate);
                E.Widget->UpdateText(NewText);
                if (Settings->bOverrideDrawSize && !Settings->BackgroundSize.IsNearlyZero())
                    E.Widget->SetDesiredSizeInViewport(Settings->BackgroundSize);
            }
        }

        UInfoBillboardSettings* Settings = E.CachedSettings;

        if (!E.bVisible || !Settings)
        {
            E.Widget->SetVisibility(ESlateVisibility::Collapsed);
            continue;
        }

        // Fast tick: project world position every frame for smooth movement
        AActor* Owner = BBC->GetOwner();
        if (!Owner) { E.Widget->SetVisibility(ESlateVisibility::Collapsed); continue; }

        // WorldOffset is in world space -- adding it before projection
        // means it scales correctly with distance (shrinks when far away).
        const FVector WorldPos = Owner->GetActorLocation() + Settings->WorldOffset;

        FVector2D ScreenPos;
        if (!PC->ProjectWorldLocationToScreen(WorldPos, ScreenPos, true))
        {
            E.Widget->SetVisibility(ESlateVisibility::Collapsed);
            continue;
        }

        // ProjectWorldLocationToScreen returns physical pixels.
        // SetRenderTranslation operates in DPI-scaled widget units.
        // Divide by DPI scale to convert physical px -> widget units.
        const float DPIScale = UWidgetLayoutLibrary::GetViewportScale(this);
        const FVector2D WidgetPos = (DPIScale > 0.f)
            ? ScreenPos / DPIScale : ScreenPos;

        // Distance culling
        if (Settings->MaxVisibleDistance > 0.f)
        {
            const float DistSq = FVector::DistSquared(
                Owner->GetActorLocation(),
                PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : FVector::ZeroVector);
            if (DistSq > FMath::Square(Settings->MaxVisibleDistance))
            {
                E.Widget->SetVisibility(ESlateVisibility::Collapsed);
                continue;
            }
        }

        E.Widget->SetVisibility(ESlateVisibility::HitTestInvisible);

        // Center widget horizontally on screen pos, bottom edge at screen pos
        const FVector2D WSize = Settings->bOverrideDrawSize && !Settings->BackgroundSize.IsNearlyZero()
            ? Settings->BackgroundSize
            : E.Widget->GetDesiredSize();
        const FVector2D FinalPos = WidgetPos - FVector2D(WSize.X * 0.5f, WSize.Y);
        E.Widget->SetRenderTranslation(FinalPos);

        // Diagnostic log (gated, fires every 0.5s per entry)
        if (DebugSettings && DebugSettings->bLogBillboard)
        {
            E.DiagAccumulator += DeltaTime;
            if (E.DiagAccumulator >= 0.5f)
            {
                E.DiagAccumulator = 0.f;
                const FVector ActorLoc = Owner->GetActorLocation();
                UE_LOG(LogTemp, Log,
                    TEXT("[BBPos] '%s'  ActorLoc=(%.0f,%.0f,%.0f)  "
                        "WorldPos=(%.0f,%.0f,%.0f)  ScreenPx=(%.1f,%.1f)  "
                        "DPI=%.2f  WidgetPos=(%.1f,%.1f)  WSize=(%.0f,%.0f)  "
                        "FinalPos=(%.1f,%.1f)"),
                    *BBC->EntityDisplayName,
                    ActorLoc.X, ActorLoc.Y, ActorLoc.Z,
                    WorldPos.X, WorldPos.Y, WorldPos.Z,
                    ScreenPos.X, ScreenPos.Y,
                    DPIScale,
                    WidgetPos.X, WidgetPos.Y,
                    WSize.X, WSize.Y,
                    FinalPos.X, FinalPos.Y);
            }
        }
    }

    // 5s diagnostic
    TickAccumulator += DeltaTime;
    if (TickAccumulator >= 5.f)
    {
        TickAccumulator = 0.f;
        for (const FBillboardEntry& E : Entries)
        {
            if (!E.Source.IsValid()) continue;
            BB_LOG("[BBDisplay:P%d sees '%s'] Visible=%d",
                PC->GetLocalPlayer()
                ? GetWorld()->GetGameInstance()->GetLocalPlayers()
                .IndexOfByKey(PC->GetLocalPlayer()) : -1,
                *E.Source->EntityDisplayName,
                E.bVisible ? 1 : 0);
        }
    }
}