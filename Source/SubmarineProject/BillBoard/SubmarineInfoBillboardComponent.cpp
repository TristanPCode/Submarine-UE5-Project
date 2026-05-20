// Fill out your copyright notice in the Description page of Project Settings.

#include "Billboard/SubmarineInfoBillboardComponent.h"
#include "Billboard/InfoBillboardWidget.h"
#include "Billboard/InfoBillboardSettings.h"
#include "Billboard/InfoBillboardContextSettings.h"
#include "TrackableSubmarine.h"
#include "TorpedoPawn.h"
#include "GameFramework/PlayerController.h"
#include "CameraEdit/ScreenFadeComponent.h"
#include "GameFramework/GameModeBase.h"

USubmarineInfoBillboardComponent::USubmarineInfoBillboardComponent()
{
    // Tick disabled: this component is data-only.
    // BillboardDisplayComponent on each PC handles all rendering and ticking.
    PrimaryComponentTick.bCanEverTick = false;

    SetDrawAtDesiredSize(true);
    SetWidgetSpace(EWidgetSpace::Screen);
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void USubmarineInfoBillboardComponent::BeginPlay()
{
    Super::BeginPlay();

    // SetDrawAtDesiredSize(true) breaks pivot centering because the widget size
    // isn't known when the pivot offset is computed by UWidgetComponent.
    // Fix: disable desired-size mode and instead use the widget's own desired
    // size (set in the Blueprint) as a fixed draw size. This lets the BP
    // control the visual size while keeping pivot math deterministic.
    SetDrawAtDesiredSize(false);

    // Read the widget's desired size (set by the BP designer in the widget's
    // canvas root, e.g. via a fixed-size SizeBox or the canvas desired size).
    // Falls back to (200,60) if the widget hasn't been laid out yet.
    FVector2D DesiredSz = FVector2D(200.f, 60.f);
    if (UUserWidget* W = GetUserWidgetObject())
    {
        const FVector2D Computed = W->GetDesiredSize();
        if (!Computed.IsNearlyZero())
            DesiredSz = Computed;
    }
    SetDrawSize(DesiredSz);

    // (0.5, 1.0): horizontally centered, bottom edge at the WorldOffset point.
    SetPivot(FVector2D(0.5f, 1.0f));

    UE_LOG(LogTemp, Log,
        TEXT("[Billboard] BeginPlay: Owner='%s'  DrawSize=(%.0f,%.0f)  Pivot=(0.5,1.0)"),
        GetOwner() ? *GetOwner()->GetName() : TEXT("?"),
        DesiredSz.X, DesiredSz.Y);

    // Bind to ScreenFadeComponent so billboard hides during camera fades/black screens.
    // Try PC owner first, then GameMode as fallback (same as HUDTransitionManager).
    if (UWorld* W = GetWorld())
    {
        UScreenFadeComponent* FadeComp = nullptr;
        // Search all local PCs
        for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
        {
            APlayerController* PC = Cast<APlayerController>(It->Get());
            if (!PC || !PC->IsLocalPlayerController()) continue;
            FadeComp = PC->FindComponentByClass<UScreenFadeComponent>();
            if (FadeComp) break;
        }
        if (!FadeComp)
        {
            if (AGameModeBase* GM = W->GetAuthGameMode())
                FadeComp = GM->FindComponentByClass<UScreenFadeComponent>();
        }
        if (FadeComp)
        {
            FadeComp->OnFadeAlphaChanged.AddDynamic(
                this, &USubmarineInfoBillboardComponent::OnScreenFadeAlphaChanged);
            UE_LOG(LogTemp, Log,
                TEXT("[Billboard] Bound to ScreenFadeComponent for fade sync"));
        }
    }
}

// ---------------------------------------------------------------------------
//  InitializeBillboard
// ---------------------------------------------------------------------------
void USubmarineInfoBillboardComponent::InitializeBillboard(
    EBillboardEntityType InEntityType,
    int32 InTeamIndex,
    bool bInIsCPU,
    const FString& InOwnerName)
{
    EntityType = InEntityType;
    EntityTeamIndex = InTeamIndex;
    bEntityIsCPU = bInIsCPU;
    TorpedoOwnerName = InOwnerName;

    BillboardWidget = Cast<UInfoBillboardWidget>(GetUserWidgetObject());

    // Default to hidden until ApplyContextSettings is called
    SetVisibility(false);
}

// ---------------------------------------------------------------------------
//  ApplyContextSettings
// ---------------------------------------------------------------------------
void USubmarineInfoBillboardComponent::ApplyContextSettings(
    UInfoBillboardContextSettings* ContextSettings,
    int32 LocalPlayerTeamIndex,
    APlayerController* ObserverPC)
{
    // Cache context for per-observer re-evaluation in Tick
    CachedBillboardCtx = ContextSettings;

    if (!ContextSettings)
    {
        SetVisibility(false);
        ActiveSettings = nullptr;
        return;
    }

    const EBillboardRelationship Rel = ComputeRelationship(
        LocalPlayerTeamIndex, /*bLocalPlayerIsCPU=*/false, ObserverPC);

    ActiveSettings = ContextSettings->GetSettings(Rel);

    UE_LOG(LogTemp, Log,
        TEXT("[BB:Apply] Entity='%s'  Ctx=%s  ObserverPC=%s  ObsTeam=%d  Rel=%d  "
            "ResolvedSettings=%s  WillShow=%d"),
        *EntityDisplayName,
        *ContextSettings->GetName(),
        ObserverPC ? *ObserverPC->GetName() : TEXT("none"),
        LocalPlayerTeamIndex,
        (int32)Rel,
        ActiveSettings ? *ActiveSettings->GetName() : TEXT("NULL"),
        (ActiveSettings && ActiveSettings->bVisible) ? 1 : 0);

    if (!ActiveSettings || !ActiveSettings->bVisible)
    {
        SetVisibility(false);
        return;
    }

    SetVisibility(true);
    SetRelativeLocation(ActiveSettings->WorldOffset);

    // Apply DA-driven draw size if configured, otherwise keep BP widget size
    if (ActiveSettings->bOverrideDrawSize && !ActiveSettings->BackgroundSize.IsNearlyZero())
    {
        SetDrawAtDesiredSize(false);
        SetDrawSize(ActiveSettings->BackgroundSize);
        SetPivot(FVector2D(0.5f, 1.0f));  // re-apply pivot after size change
        UE_LOG(LogTemp, Log,
            TEXT("[Billboard] ApplyContextSettings: DrawSize overridden from DA: (%.0f,%.0f)"),
            ActiveSettings->BackgroundSize.X, ActiveSettings->BackgroundSize.Y);
    }

    if (BillboardWidget)
    {
        BillboardWidget->ApplySettings(ActiveSettings, EvaluateTemplate(ActiveSettings->TextTemplate));

        // Apply team color if the settings request it.
        // Uses a built-in per-team palette until RuntimeMatchSettings is wired.
        if (ActiveSettings->bUseTeamColor)
        {
            static const FLinearColor TeamColors[] = {
                FLinearColor(0.2f, 0.5f, 1.f,  1.f),   // Team 0: blue
                FLinearColor(1.f,  0.3f, 0.2f,  1.f),   // Team 1: red
                FLinearColor(0.2f, 0.9f, 0.2f,  1.f),   // Team 2: green
                FLinearColor(1.f,  0.8f, 0.1f,  1.f),   // Team 3: yellow
                FLinearColor(0.8f, 0.2f, 0.9f,  1.f),   // Team 4: purple
                FLinearColor(1.f,  0.5f, 0.0f,  1.f),   // Team 5: orange
                FLinearColor(0.0f, 0.9f, 0.9f,  1.f),   // Team 6: cyan
                FLinearColor(1.f,  1.f,  1.f,   1.f),   // Team 7: white
            };
            const int32 ColorIdx = FMath::Clamp(EntityTeamIndex, 0, 7);
            BillboardWidget->OverrideTextColor(TeamColors[ColorIdx]);
        }
    }
}

// ---------------------------------------------------------------------------
//  CheckIdentificationGate
// ---------------------------------------------------------------------------

bool USubmarineInfoBillboardComponent::CheckIdentificationGate() const
{
    if (!bRequireIdentification) return true;
    if (!GetWorld()) return false;

    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = Cast<APlayerController>(It->Get());
        if (!PC || !PC->IsLocalPlayerController()) continue;

        APawn* LocalPawn = PC->GetPawn();
        if (!LocalPawn) continue;

        // Self: always show
        if (LocalPawn == GetOwner()) return true;

        ITrackableSubmarine* LocalTS = Cast<ITrackableSubmarine>(LocalPawn);
        if (!LocalTS) continue;

        const FGuid OwnerGuid = GetOwner()
            ? GetOwner()->GetActorInstanceGuid() : FGuid();

        for (const FDetectedEntry& Entry : LocalTS->GetDetectionEntries())
        {
            if (Entry.ActorGuid == OwnerGuid)
            {
                if (Entry.DetectionState == ERadarDetectionState::ClearID
                    || Entry.DetectionState == ERadarDetectionState::VulnerableID)
                    return true;
            }
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
//  CheckIdentificationGateForPC
// ---------------------------------------------------------------------------
bool USubmarineInfoBillboardComponent::CheckIdentificationGateForPC(
    APlayerController* ObserverPC) const
{
    if (!bRequireIdentification) return true;
    if (!ObserverPC) return CheckIdentificationGate();

    APawn* LocalPawn = ObserverPC->GetPawn();
    if (!LocalPawn) return false;
    if (LocalPawn == GetOwner()) return true;  // always show self

    ITrackableSubmarine* LocalTS = Cast<ITrackableSubmarine>(LocalPawn);
    if (!LocalTS) return false;

    const FGuid OwnerGuid = GetOwner() ? GetOwner()->GetActorInstanceGuid() : FGuid();
    for (const FDetectedEntry& Entry : LocalTS->GetDetectionEntries())
    {
        if (Entry.ActorGuid == OwnerGuid)
            return Entry.DetectionState == ERadarDetectionState::ClearID
            || Entry.DetectionState == ERadarDetectionState::VulnerableID;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  AddLocalPlayerTeam
// ---------------------------------------------------------------------------
void USubmarineInfoBillboardComponent::AddLocalPlayerTeam(
    int32 LocalPlayerIndex, int32 TeamIndex)
{
    // Store team for each local player so Tick can evaluate relationship
    // from each player's perspective.
    // Resize array if needed
    if (LocalPlayerTeamIndices.Num() <= LocalPlayerIndex)
        LocalPlayerTeamIndices.SetNum(LocalPlayerIndex + 1, EAllowShrinking::No);
    LocalPlayerTeamIndices[LocalPlayerIndex] = TeamIndex;

    UE_LOG(LogTemp, Log,
        TEXT("[Billboard] AddLocalPlayerTeam: Owner='%s'  "
            "LocalPlayer=%d  Team=%d"),
        GetOwner() ? *GetOwner()->GetName() : TEXT("?"),
        LocalPlayerIndex, TeamIndex);
}

// ---------------------------------------------------------------------------
//  TickComponent
// ---------------------------------------------------------------------------
void USubmarineInfoBillboardComponent::TickComponent(float DeltaTime,
    ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Per-observer re-evaluation: world-space billboards are visible to ALL local players.
    // We must show the billboard if ANY local player would see it.
    // Strategy: find the "most permissive" observer -- the first local PC whose
    // relationship with this entity results in visible settings.
    // This runs at 10Hz (TickInterval=0.1) and exits early when nothing changes.
    if (CachedBillboardCtx && GetWorld())
    {
        // Per-observer evaluation:
        // Each local PC sees this billboard independently based on their relationship.
        // We pick the MOST PERMISSIVE observer -- but only the observer whose
        // pawn IS this actor may claim "Self". Other observers skip Self billboards.
        // This prevents P1 from suppressing a billboard that P2 would see as Self.
        UInfoBillboardSettings* BestSettings = nullptr;
        int32 BestObserverIdx = -1;
        int32 BestObserverTeam = 0;
        APlayerController* BestObserverPC = nullptr;

        UGameInstance* GI = GetWorld()->GetGameInstance();
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator();
            It; ++It)
        {
            APlayerController* PC = Cast<APlayerController>(It->Get());
            if (!PC || !PC->IsLocalPlayerController()) continue;

            // Determine this PC's team
            int32 ObserverTeam = 0;
            int32 ObserverIdx = 0;
            if (GI)
            {
                const TArray<ULocalPlayer*>& Players = GI->GetLocalPlayers();
                const int32 Idx = Players.IndexOfByKey(PC->GetLocalPlayer());
                ObserverIdx = (Idx != INDEX_NONE) ? Idx : 0;
                if (LocalPlayerTeamIndices.IsValidIndex(ObserverIdx))
                    ObserverTeam = LocalPlayerTeamIndices[ObserverIdx];
            }

            const EBillboardRelationship Rel = ComputeRelationship(ObserverTeam, false, PC);

            UInfoBillboardSettings* Candidate = CachedBillboardCtx->GetSettings(Rel);

            if (Candidate && Candidate->bVisible)
            {
                BestSettings = Candidate;
                BestObserverIdx = ObserverIdx;
                BestObserverTeam = ObserverTeam;
                BestObserverPC = PC;
                break;
            }
        }

        // Re-apply if observer or settings changed
        if (BestObserverIdx != LastObserverPCIdx)
        {
            UE_LOG(LogTemp, Log,
                TEXT("[BB:ObsChange] Entity='%s'  OldObserver=%d -> NewObserver=%d  PC=%s  Team=%d"),
                *EntityDisplayName, LastObserverPCIdx, BestObserverIdx,
                BestObserverPC ? *BestObserverPC->GetName() : TEXT("none"),
                BestObserverTeam);
            LastObserverPCIdx = BestObserverIdx;
            ApplyContextSettings(CachedBillboardCtx, BestObserverTeam, BestObserverPC);
        }
    }

    // Identification gate -- hide if not identified by local player
    if (!CheckIdentificationGate())
    {
        SetVisibility(false);
        TickAccumulator += DeltaTime;
        if (TickAccumulator >= 2.f)
        {
            TickAccumulator = 0.f;
            UE_LOG(LogTemp, Log,
                TEXT("[Billboard] Hidden (identification gate): Owner='%s'"),
                GetOwner() ? *GetOwner()->GetName() : TEXT("?"));
        }
        return;
    }

    // Suppress during camera fades / black screens
    if (bSuppressedByFade)
    {
        SetVisibility(false);
        return;
    }

    SetVisibility(true);

    if (!BillboardWidget) return;

    // Update dynamic fields (TimeLeft, ammo counts, etc)
    const FString NewText = EvaluateTemplate(ActiveSettings->TextTemplate);
    BillboardWidget->UpdateText(NewText);

    // Distance culling
    if (ActiveSettings->MaxVisibleDistance > 0.f && GetWorld())
    {
        float MinDist = FLT_MAX;
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            APlayerController* PC = Cast<APlayerController>(It->Get());
            if (PC && PC->IsLocalPlayerController() && PC->GetPawn())
                MinDist = FMath::Min(MinDist,
                    FVector::Dist(GetComponentLocation(), PC->GetPawn()->GetActorLocation()));
        }
        if (MinDist < FLT_MAX)
            SetVisibility(MinDist <= ActiveSettings->MaxVisibleDistance);
    }

    TickAccumulator += DeltaTime;
    if (TickAccumulator >= 2.f)   // log every 2 seconds
    {
        TickAccumulator = 0.f;
        // One line per (local player, billboard) so you see exactly what each
        // player would see for this billboard.
        if (UWorld* W = GetWorld())
        {
            UGameInstance* GI = W->GetGameInstance();
            for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
            {
                APlayerController* PC = Cast<APlayerController>(It->Get());
                if (!PC || !PC->IsLocalPlayerController()) continue;

                int32 ObsIdx = 0, ObsTeam = 0;
                if (GI)
                {
                    const int32 Idx = GI->GetLocalPlayers().IndexOfByKey(PC->GetLocalPlayer());
                    ObsIdx = (Idx != INDEX_NONE) ? Idx : 0;
                    if (LocalPlayerTeamIndices.IsValidIndex(ObsIdx))
                        ObsTeam = LocalPlayerTeamIndices[ObsIdx];
                }

                const EBillboardRelationship Rel = ComputeRelationship(ObsTeam, false, PC);
                UInfoBillboardSettings* S = CachedBillboardCtx
                    ? CachedBillboardCtx->GetSettings(Rel) : nullptr;
                const bool bWouldShow = S && S->bVisible && !bSuppressedByFade
                    && CheckIdentificationGate();

                UE_LOG(LogTemp, Log,
                    TEXT("[BB:P%d sees '%s'] Rel=%d  Settings=%s  WouldShow=%d  "
                        "IsSelf=%d  FadeSuppressed=%d  IdentGate=%d"),
                    ObsIdx,
                    *EntityDisplayName,
                    (int32)Rel,
                    S ? *S->GetName() : TEXT("NULL"),
                    bWouldShow ? 1 : 0,
                    (PC->GetPawn() == GetOwner()) ? 1 : 0,
                    bSuppressedByFade ? 1 : 0,
                    CheckIdentificationGate() ? 1 : 0);
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  ComputeRelationship
// ---------------------------------------------------------------------------
EBillboardRelationship USubmarineInfoBillboardComponent::ComputeRelationship(
    int32 LocalPlayerTeamIndex, bool bLocalPlayerIsCPU, APlayerController* ObserverPC) const
{
    const bool bSameTeam = (EntityTeamIndex == LocalPlayerTeamIndex);

    if (EntityType == EBillboardEntityType::Torpedo)
    {
        // Check if local player owns this torpedo by checking team affiliation.
        // bEntityIsCPU reflects the firing submarine's CPU status.
        // bSameTeam = (torpedo's firing team == observer's team).
        //
        // OwnTorpedo: fired by THIS specific local player's pawn.
        // We detect this by checking if any local PC's pawn fired it.
        // Since torpedo owner info is stored as a display name (TorpedoOwnerName),
        // we compare against local pawns' display names as a best effort.
        if (GetWorld())
        {
            for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator();
                It; ++It)
            {
                APlayerController* PC = Cast<APlayerController>(It->Get());
                if (!PC || !PC->IsLocalPlayerController()) continue;
                ITrackableSubmarine* TS = Cast<ITrackableSubmarine>(PC->GetPawn());
                if (!TS) continue;
                // If this local player's sub name matches the torpedo owner name,
                // this is their own torpedo.
                if (TS->GetDisplayName().ToString() == TorpedoOwnerName)
                    return EBillboardRelationship::OwnTorpedo;
            }
        }

        // Not own torpedo -- allied or enemy based on team and CPU status
        if (bSameTeam)
            return bEntityIsCPU
            ? EBillboardRelationship::AlliedCPUTorpedo
            : EBillboardRelationship::AlliedPlayerTorpedo;
        else
            return bEntityIsCPU
            ? EBillboardRelationship::EnemyCPUTorpedo
            : EBillboardRelationship::EnemyPlayerTorpedo;
    }

    // Submarine
    // Self: only when the SPECIFIC observer PC owns this pawn.
    // If ObserverPC is provided, check only that PC (split-screen safe).
    // If null (legacy call), check any local PC.
    if (ObserverPC)
    {
        if (ObserverPC->GetPawn() == GetOwner())
            return EBillboardRelationship::Self;
    }
    else if (GetWorld())
    {
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            APlayerController* PC = Cast<APlayerController>(It->Get());
            if (PC && PC->IsLocalPlayerController() && PC->GetPawn() == GetOwner())
                return EBillboardRelationship::Self;
        }
    }

    if (bSameTeam)
        return bEntityIsCPU
        ? EBillboardRelationship::AlliedCPU
        : EBillboardRelationship::AlliedPlayer;
    else
        return bEntityIsCPU
        ? EBillboardRelationship::EnemyCPU
        : EBillboardRelationship::EnemyPlayer;
}

// ---------------------------------------------------------------------------
//  OnScreenFadeAlphaChanged
// ---------------------------------------------------------------------------
void USubmarineInfoBillboardComponent::OnScreenFadeAlphaChanged(float Alpha)
{
    // Alpha: 0 = fully visible, 1 = fully black.
    // Hide billboard completely during fades so it doesn't float over black screens.
    // We use a threshold: if the screen is more than 5% faded, suppress the billboard.
    bSuppressedByFade = (Alpha > 0.05f);
}

// ---------------------------------------------------------------------------
//  EvaluateTemplate
// ---------------------------------------------------------------------------
FString USubmarineInfoBillboardComponent::EvaluateTemplate(
    const FString& Template) const
{
    if (Template.IsEmpty()) return FString();

    // Parse {Field} tokens and replace
    FString Result;
    Result.Reserve(Template.Len() * 2);

    int32 i = 0;
    while (i < Template.Len())
    {
        if (Template[i] == TEXT('{'))
        {
            int32 End = Template.Find(TEXT("}"), ESearchCase::CaseSensitive,
                ESearchDir::FromStart, i + 1);
            if (End != INDEX_NONE)
            {
                const FString FieldName = Template.Mid(i + 1, End - i - 1);
                Result += ResolveField(FieldName);
                i = End + 1;
                continue;
            }
        }
        Result += Template[i];
        ++i;
    }

    return Result;
}

// ---------------------------------------------------------------------------
//  ResolveField
// ---------------------------------------------------------------------------
FString USubmarineInfoBillboardComponent::ResolveField(
    const FString& FieldName) const
{
    AActor* Owner = GetOwner();

    // ---- Submarine fields ----
    if (EntityType == EBillboardEntityType::Submarine)
    {
        ITrackableSubmarine* TS = Cast<ITrackableSubmarine>(Owner);
        if (!TS) return FString(TEXT("?"));

        if (FieldName == TEXT("Name"))
            return EntityDisplayName.IsEmpty()
            ? TS->GetDisplayName().ToString() : EntityDisplayName;

        if (FieldName == TEXT("TeamName"))   return EntityTeamName.IsEmpty()
            ? FString::Printf(TEXT("Team%d"), EntityTeamIndex) : EntityTeamName;
        if (FieldName == TEXT("FactionName")) return EntityTeamName;

        if (FieldName == TEXT("Level"))
        {
            ITrackableSubmarine* TempTS = Cast<ITrackableSubmarine>(Owner);
            return TempTS ? FString::FromInt(TempTS->GetLevel()) : TEXT("1");
        }

        if (FieldName == TEXT("PlayerType"))     return bEntityIsCPU ? TEXT("CPU") : TEXT("Player");
        if (FieldName == TEXT("Relationship"))
        {
            APlayerController* PC = GetWorld()
                ? GetWorld()->GetFirstPlayerController() : nullptr;
            if (PC && PC->GetPawn() == Owner) return TEXT("Self");
            return (EntityTeamIndex == 0) ? TEXT("Ally") : TEXT("Enemy"); // simplified
        }

        if (FieldName == TEXT("TorpedoStatus"))
            return TS->GetFireCooldownRatio() >= 1.f ? TEXT("Ready") : TEXT("Cooldown");

        if (FieldName == TEXT("NormalAmmo"))
            return FString::FromInt(TS->GetNormalAmmoCount());
        if (FieldName == TEXT("NormalAmmoMax"))
            return FString::FromInt(TS->GetNormalAmmoCapacity());
        if (FieldName == TEXT("SpecialAmmo"))
            return FString::FromInt(TS->GetSpecialAmmoCount());
        if (FieldName == TEXT("SpecialAmmoMax"))
            return FString::FromInt(TS->GetSpecialAmmoCapacity());

        return FString(TEXT("?"));
    }

    // ---- Torpedo fields ----
    ATorpedoPawn* Torpedo = Cast<ATorpedoPawn>(Owner);
    if (!Torpedo) return FString(TEXT("?"));

    if (FieldName == TEXT("Name"))
        return TorpedoOwnerName + TEXT(" - ") + Owner->GetName();

    if (FieldName == TEXT("Owner"))
        return TorpedoOwnerName;

    if (FieldName == TEXT("TorpedoType"))
    {
        if (Torpedo->Characteristics)
        {
            switch (Torpedo->Characteristics->TorpedoType)
            {
            case ETorpedoType::Light:  return TEXT("Light");
            case ETorpedoType::Normal: return TEXT("Normal");
            case ETorpedoType::Heavy:  return TEXT("Heavy");
            case ETorpedoType::Seeker: return TEXT("Seeker");
            case ETorpedoType::Radio:  return TEXT("Radio");
            }
        }
        return TEXT("?");
    }

    if (FieldName == TEXT("Damage") && Torpedo->Characteristics)
        return FString::Printf(TEXT("%.0f"), Torpedo->Characteristics->AttackDamage);

    if (FieldName == TEXT("Lifetime") && Torpedo->Characteristics)
        return FString::Printf(TEXT("%.1f"), Torpedo->Characteristics->MaxLifetime);

    if (FieldName == TEXT("TimeLeft"))
    {
        // TimeLeft is dynamic -- read from the pawn's elapsed lifetime
        // ATorpedoPawn exposes GetCurrentSpeed but not lifetime directly
        // For now: return "?" until exposed in TorpedoPawn interface
        return FString::Printf(TEXT("%.1f"), Torpedo->GetLifetimeElapsed());
    }

    return FString(TEXT("?"));
}