// Fill out your copyright notice in the Description page of Project Settings.

#include "HUD/SubmarineHUDComponent.h"
#include "HUD/MainHUDWidget.h"
#include "HUD/SubmarineHUDSettings.h"
#include "HUD/SubmarineHUDDebugSettings.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"

USubmarineHUDComponent::USubmarineHUDComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

// ---------------------------------------------------------------------------
//  BeginPlay
// ---------------------------------------------------------------------------
void USubmarineHUDComponent::BeginPlay()
{
    Super::BeginPlay();

    // Cache debug settings early so logging is available immediately
    if (HUDSettings)
        DebugSettings = HUDSettings->DebugSettings;

    if (ShouldLog(DebugSettings ? DebugSettings->bLogHUDCreation : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HUDComponent] BeginPlay  Owner='%s'  HUDSettings=%s"),
            *GetOwner()->GetName(),
            HUDSettings ? *HUDSettings->GetName() : TEXT("NULL"));

    OnTrackedSubmarineChanged.AddDynamic(
        this, &USubmarineHUDComponent::HandleTrackedSubmarineChanged);

    CreateHUDWidget();
}

// ---------------------------------------------------------------------------
//  EndPlay
// ---------------------------------------------------------------------------
void USubmarineHUDComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (ShouldLog(DebugSettings ? DebugSettings->bLogHUDCreation : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HUDComponent] EndPlay  Owner='%s'"),
            *GetOwner()->GetName());

    if (RootWidget)
    {
        // Disconnect all modules cleanly
        RootWidget->SetDataSource(TScriptInterface<ITrackableSubmarine>());
        RootWidget->RemoveFromParent();
        RootWidget = nullptr;
    }

    TrackedActor.Reset();
    TrackedInterface = nullptr;

    Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
//  SetTrackedSubmarine
// ---------------------------------------------------------------------------
void USubmarineHUDComponent::SetTrackedSubmarine(AActor* Target)
{
    // Clear path
    if (!Target)
    {
        if (ShouldLog(DebugSettings ? DebugSettings->bLogTrackedSubmarineChange : false))
            UE_LOG(LogTemp, Log,
                TEXT("[HUDComponent] SetTrackedSubmarine: clearing tracked submarine"));

        TrackedActor.Reset();
        TrackedInterface = nullptr;
        OnTrackedSubmarineChanged.Broadcast(nullptr);
        return;
    }

    // Validate interface
    if (!Target->Implements<UTrackableSubmarine>())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[HUDComponent] SetTrackedSubmarine: '%s' does not implement "
                "ITrackableSubmarine -- ignoring. "
                "Add ITrackableSubmarine to the class and implement all methods."),
            *Target->GetName());
        return;
    }

    // Avoid redundant updates
    if (TrackedActor.Get() == Target)
    {
        if (ShouldLog(DebugSettings ? DebugSettings->bLogTrackedSubmarineChange : false))
            UE_LOG(LogTemp, Log,
                TEXT("[HUDComponent] SetTrackedSubmarine: '%s' is already tracked -- no-op"),
                *Target->GetName());
        return;
    }

    const FString OldName = TrackedActor.IsValid()
        ? TrackedActor->GetName() : TEXT("None");

    TrackedActor = Target;
    TrackedInterface = TScriptInterface<ITrackableSubmarine>(Target);

    if (ShouldLog(DebugSettings ? DebugSettings->bLogTrackedSubmarineChange : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HUDComponent] SetTrackedSubmarine: '%s' -> '%s'"),
            *OldName, *Target->GetName());

    OnTrackedSubmarineChanged.Broadcast(Target);
}

// ---------------------------------------------------------------------------
//  SetHUDVisible
// ---------------------------------------------------------------------------
void USubmarineHUDComponent::SetHUDVisible(bool bVisible)
{
    if (ShouldLog(DebugSettings ? DebugSettings->bLogVisibilityChange : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HUDComponent] SetHUDVisible: %s  Owner='%s'"),
            bVisible ? TEXT("VISIBLE") : TEXT("HIDDEN"),
            *GetOwner()->GetName());

    if (RootWidget)
        RootWidget->SetVisibility(
            bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
    else if (ShouldLog(DebugSettings ? DebugSettings->bLogVisibilityChange : false))
        UE_LOG(LogTemp, Warning,
            TEXT("[HUDComponent] SetHUDVisible: RootWidget is null -- cannot change visibility"));
}

// ---------------------------------------------------------------------------
//  CreateHUDWidget
// ---------------------------------------------------------------------------
void USubmarineHUDComponent::CreateHUDWidget()
{
    if (!HUDSettings)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[HUDComponent] CreateHUDWidget: HUDSettings is null. "
                "Assign a USubmarineHUDSettings DataAsset to this component "
                "in the PlayerController Blueprint. No HUD will be created."));
        return;
    }

    if (!HUDSettings->WidgetClass)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[HUDComponent] CreateHUDWidget: HUDSettings->WidgetClass is null. "
                "Assign a UMainHUDWidget subclass in the DataAsset. No HUD will be created."));
        return;
    }

    APlayerController* PC = Cast<APlayerController>(GetOwner());
    if (!PC)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[HUDComponent] CreateHUDWidget: owner '%s' is not a PlayerController. "
                "USubmarineHUDComponent must be attached to APlayerController."),
            *GetOwner()->GetName());
        return;
    }

    if (ShouldLog(DebugSettings ? DebugSettings->bLogHUDCreation : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HUDComponent] CreateHUDWidget: creating '%s' for PC='%s'"),
            *HUDSettings->WidgetClass->GetName(), *PC->GetName());

    RootWidget = CreateWidget<UMainHUDWidget>(PC, HUDSettings->WidgetClass);
    if (!RootWidget)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[HUDComponent] CreateHUDWidget: CreateWidget returned null for class '%s'"),
            *HUDSettings->WidgetClass->GetName());
        return;
    }

    RootWidget->InitializeHUD(HUDSettings);

    // AddToPlayerScreen: scoped to this player's viewport area (split-screen safe)
    RootWidget->AddToPlayerScreen();

    if (ShouldLog(DebugSettings ? DebugSettings->bLogHUDCreation : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HUDComponent] HUD created and added to player screen. "
                "PC='%s'  Modules=%d"),
            *PC->GetName(), RootWidget->GetModuleCount());
}

// ---------------------------------------------------------------------------
//  HandleTrackedSubmarineChanged
// ---------------------------------------------------------------------------
void USubmarineHUDComponent::HandleTrackedSubmarineChanged(AActor* NewTarget)
{
    if (!RootWidget)
    {
        if (ShouldLog(DebugSettings ? DebugSettings->bLogTrackedSubmarineChange : false))
            UE_LOG(LogTemp, Warning,
                TEXT("[HUDComponent] HandleTrackedSubmarineChanged: RootWidget is null -- "
                    "cannot propagate data source"));
        return;
    }

    // If TrackedActor became invalid between broadcast and handler (actor destroyed),
    // pass empty interface to cleanly disconnect all modules
    if (!TrackedActor.IsValid())
    {
        if (ShouldLog(DebugSettings ? DebugSettings->bLogTrackedSubmarineChange : false))
            UE_LOG(LogTemp, Warning,
                TEXT("[HUDComponent] HandleTrackedSubmarineChanged: "
                    "TrackedActor is no longer valid (destroyed?) -- clearing modules"));

        RootWidget->SetDataSource(TScriptInterface<ITrackableSubmarine>());
        return;
    }

    if (ShouldLog(DebugSettings ? DebugSettings->bLogTrackedSubmarineChange : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HUDComponent] HandleTrackedSubmarineChanged: "
                "propagating '%s' to RootWidget"),
            *TrackedActor->GetName());

    RootWidget->SetDataSource(TrackedInterface);
}