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
//  SwapHUDSettings
// ---------------------------------------------------------------------------

void USubmarineHUDComponent::SwapHUDSettings(USubmarineHUDSettings* NewSettings)
{
    if (!NewSettings)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[HUDComponent] SwapHUDSettings: NewSettings is null. "
                "Hiding HUD."));
        if (RootWidget)
            RootWidget->SetVisibility(ESlateVisibility::Hidden);
        return;
    }

    // Update cached settings
    HUDSettings = NewSettings;
    if (HUDSettings->DebugSettings)
        DebugSettings = HUDSettings->DebugSettings;

    if (!RootWidget)
    {
        // First time -- create the widget
        CreateHUDWidget();
        return;  // CreateHUDWidget calls InitializeHUD internally
    }

    // Widget already exists -- reinitialize modules
    // RootWidget->InitializeHUD tears down old modules and builds new ones
    RootWidget->InitializeHUD(NewSettings);

    // Re-apply tracked submarine so modules bind correctly
    if (TrackedActor.IsValid())
        RootWidget->SetDataSource(TrackedInterface);

    // Restore visibility in case it was hidden
    RootWidget->SetVisibility(ESlateVisibility::HitTestInvisible);

    // Deferred geometry refresh: modules that depend on canvas size
    // (PositionalIndicator, NumericDisplay) cache geometry in NativeTick.
    // After a swap, the new modules have zero cached canvas size until
    // UMG runs a layout pass. Force a SetDataSource next tick so they
    // re-evaluate their geometry with real values.
    {
        TWeakObjectPtr<UMainHUDWidget> WeakRoot(RootWidget);
        if (GetWorld())
            GetWorld()->GetTimerManager().SetTimerForNextTick([WeakRoot]()
                {
                    if (WeakRoot.IsValid())
                        WeakRoot->SetDataSource(WeakRoot->GetCachedDataSource());
                });
    }

    if (ShouldLog(DebugSettings ? DebugSettings->bLogHUDCreation : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HUDComponent] SwapHUDSettings: swapped to '%s'  "
                "TrackedActor='%s'"),
            *NewSettings->GetName(),
            TrackedActor.IsValid() ? *TrackedActor->GetName() : TEXT("None"));
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

    // --- Diagnostic dump ---
    ULocalPlayer* LP = PC->GetLocalPlayer();
    UE_LOG(LogTemp, Warning,
        TEXT("[HUDComponent] Diagnostic: PC='%s'  LocalPlayer=%s  IsLocalController=%d  NetMode=%d"),
        *PC->GetName(),
        LP ? *LP->GetName() : TEXT("NULL"),
        PC->IsLocalController() ? 1 : 0,
        (int32)GetWorld()->GetNetMode());

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

    // AddToPlayerScreen scopes the widget to this PC's local viewport.
    // This is critical for split-screen: each player's HUD must be scoped
    // to their own half-screen. AddToViewport puts it on the full screen.
    // NEVER fall back to AddToViewport -- if this fails, the PC has no
    // local player yet (timing issue) and the deferred tick will re-try.
    RootWidget->AddToPlayerScreen();

    UE_LOG(LogTemp, Log,
        TEXT("[HUDComponent] AddToPlayerScreen: PC='%s'  InViewport=%d  "
            "LocalPlayer=%s"),
        *PC->GetName(),
        RootWidget->IsInViewport() ? 1 : 0,
        LP ? *LP->GetName() : TEXT("NULL -- widget may not appear"));

    if (!RootWidget->IsInViewport())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[HUDComponent] AddToPlayerScreen failed for PC='%s'. "
                "This usually means LocalPlayer is null at widget creation time. "
                "The deferred SetDataSource tick will re-attempt layout. "
                "DO NOT fall back to AddToViewport -- it breaks split-screen."),
            *PC->GetName());
    }

    // If a submarine was already tracked before widget existed, apply it now
    if (TrackedActor.IsValid())
        RootWidget->SetDataSource(TrackedInterface);

    // Force one extra refresh next tick so deferred layout modules
    // (PositionalIndicator, NumericDisplay) get their canvas size.
    FTimerHandle RefreshTimer;
    TWeakObjectPtr<UMainHUDWidget> WeakRoot(RootWidget);
    GetWorld()->GetTimerManager().SetTimerForNextTick(
        [WeakRoot]()
        {
            if (WeakRoot.IsValid())
                WeakRoot->SetDataSource(WeakRoot->GetCachedDataSource());
        });

    if (ShouldLog(DebugSettings ? DebugSettings->bLogHUDCreation : false))
        UE_LOG(LogTemp, Log,
            TEXT("[HUDComponent] HUD created. PC='%s'  Modules=%d  InViewport=%d"),
            *PC->GetName(), RootWidget->GetModuleCount(),
            RootWidget->IsInViewport() ? 1 : 0);
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