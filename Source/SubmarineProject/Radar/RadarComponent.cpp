// Fill out your copyright notice in the Description page of Project Settings.

#include "Radar/RadarComponent.h"
#include "SubmarinePawn.h"
#include "Torpedo/TorpedoPawn.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

URadarComponent::URadarComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void URadarComponent::BeginPlay()
{
    Super::BeginPlay();

    const URadarSettings* S = GetSettings();
    CurrentZoom = S ? S->ZoomDefault : 1.f;
}

// ---------------------------------------------------------------------------
//  TickComponent
// ---------------------------------------------------------------------------
void URadarComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Decrement the scan frame counter. Set to 2 by TriggerScan().
    // This survives the full input->tick->NativeTick cycle regardless of ordering:
    //   Frame N:   Input fires TriggerScan -> counter=2
    //   Frame N:   TickComponent -> counter=1 (still >0)
    //   Frame N:   NativeTick -> GetScanJustTriggered()=true -> pulse triggered
    //   Frame N+1: TickComponent -> counter=0
    //   Frame N+1: NativeTick -> GetScanJustTriggered()=false
    if (ScanJustTriggeredFrames > 0)
        --ScanJustTriggeredFrames;

    // Tick scan cooldown
    ScanCooldownRemaining = FMath::Max(0.f, ScanCooldownRemaining - DeltaTime);

    UpdateVulnerabilityScore(DeltaTime);
    UpdateDetection(DeltaTime);
}

// ---------------------------------------------------------------------------
//  NotifyTorpedoFired
// ---------------------------------------------------------------------------
void URadarComponent::NotifyTorpedoFired()
{
    TorpedoContribution = 1.f;
}

// ---------------------------------------------------------------------------
//  TriggerScan
// ---------------------------------------------------------------------------
void URadarComponent::TriggerScan()
{
    const URadarSettings* S = GetSettings();

    // Enforce cooldown — one scan per press is already guaranteed by
    if (ScanCooldownRemaining > 0.f)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[RadarComponent|%s] TriggerScan BLOCKED: cooldown=%.2fs remaining"),
            GetOwner() ? *GetOwner()->GetName() : TEXT("?"),
            ScanCooldownRemaining);
        return;
    }
    
    // Start cooldown timer for next scan
    if (S) ScanCooldownRemaining = S->ScanCooldown;

    // Spike radar contribution (increases this sub's vulnerability to others)
    RadarContribution = 1.f;

    // Signal the UI that a scan just happened (2-frames flag)
    ScanJustTriggeredFrames = 2;  // 2 frames: survives one TickComponent decrement before NativeTick sees it

    // Refresh display lifetime for all currently detected entries
    if (!S) return;

    for (FDetectedEntry& Entry : DetectedEntries)
    {
        if (Entry.bIsCurrentlyDetected)
            Entry.DisplayTimeRemaining = S->GetDisplayLifetime(Entry.DetectionState);
    }

    // Always log scan success so you can confirm it fires without needing bRadarComponentDebug
    UE_LOG(LogTemp, Log,
        TEXT("[RadarComponent|%s] TriggerScan SUCCESS: cooldown=%.2fs  bScanJustTriggered=true  entries=%d"),
        GetOwner() ? *GetOwner()->GetName() : TEXT("?"),
        S->ScanCooldown,
        DetectedEntries.Num());

    // Per-entry detail: only when bRadarComponentDebug is on (very spammy)
    if (S->bRadarComponentDebug)
    {
        for (const FDetectedEntry& E : DetectedEntries)
        {
            UE_LOG(LogTemp, Log,
                TEXT("  Entry State=%d DisplayTime=%.2f DetTime=%.2f bCurrentlyDetected=%d"),
                (int32)E.DetectionState, E.DisplayTimeRemaining,
                E.DetectionTimeRemaining, E.bIsCurrentlyDetected ? 1 : 0);
        }
    }
}

// ---------------------------------------------------------------------------
//  SetZoom / IncrementZoom
// ---------------------------------------------------------------------------
void URadarComponent::SetZoom(float NewZoom)
{
    const URadarSettings* S = GetSettings();
    const float Min = S ? S->ZoomMin : 0.8f;
    const float Max = S ? S->ZoomMax : 3.f;
    CurrentZoom = FMath::Clamp(NewZoom, Min, Max);
}

void URadarComponent::IncrementZoom(float Delta)
{
    SetZoom(CurrentZoom + Delta);
}

// ---------------------------------------------------------------------------
//  UpdateVulnerabilityScore
// ---------------------------------------------------------------------------
void URadarComponent::UpdateVulnerabilityScore(float DeltaTime)
{
    const URadarSettings* S = GetSettings();
    if (!S) return;

    ASubmarinePawn* Owner = Cast<ASubmarinePawn>(GetOwner());
    if (!Owner) return;

    // Movement contribution: 1 if moving, 0 if standing
    // No decay timer -- instantly reflects current movement state
    const bool bMoving = !FMath::IsNearlyZero(Owner->CurrentLinearSpeed, 10.f);
    MovementContribution = bMoving ? 1.f : 0.f;

    // Radar and torpedo contributions decay each tick
    const float RadarDecay = S->RadarContributionDecayRate * DeltaTime;
    const float TorpedoDecay = S->TorpedoContributionDecayRate * DeltaTime;

    RadarContribution = FMath::Max(0.f, RadarContribution - RadarDecay);
    TorpedoContribution = FMath::Max(0.f, TorpedoContribution - TorpedoDecay);

    // Weighted sum, clamped to [0,1]
    VulnerabilityScore = FMath::Clamp(
        MovementContribution * S->MovementWeight +
        RadarContribution * S->RadarWeight +
        TorpedoContribution * S->TorpedoWeight,
        0.f, 1.f);
}

// ---------------------------------------------------------------------------
//  UpdateDetection
// ---------------------------------------------------------------------------
void URadarComponent::UpdateDetection(float DeltaTime)
{
    const URadarSettings* S = GetSettings();
    if (!S) return;

    ASubmarinePawn* Owner = Cast<ASubmarinePawn>(GetOwner());
    if (!Owner) return;

    const FVector  OwnerLocation = Owner->GetActorLocation();
    const FVector  OwnerForward = Owner->GetCameraForwardVector();
    const float    OwnerYaw = Owner->GetActorRotation().Yaw;
    const bool     bPeriscope = Owner->GetIsPeriscopeActive();
    const float    Zoom = CurrentZoom;
    const float    CameraFOV = Owner->GetCameraFOV();

    // Mark all entries as not currently detected (will be reset below)
    for (FDetectedEntry& Entry : DetectedEntries)
        Entry.bIsCurrentlyDetected = false;

    // Scan all potential targets
    UWorld* World = GetWorld();
    if (!World) return;

    // Diagnostic: count how many ITrackableSubmarine actors exist this tick
    DetectLogTimer += DeltaTime;
    const bool bShouldLogDetect = (DetectLogTimer >= 2.f) && S->bRadarComponentDebug;
    if (bShouldLogDetect) DetectLogTimer = 0.f;

    int32 TargetCount = 0;

    // Unified detection lambda -- handles both submarines and torpedoes.
    // ForcedType = Unknown means the lambda infers type from cast.
    // bAlwaysAutoDisplay = true means display without needing a manual scan.
    auto ProcessTarget = [&](
        AActor* TargetActor,
        float            TargetVulnerability,
        ERadarEntityType ForcedType,
        bool bAlwaysAutoDisplay)
    {
        const FGuid TargetGuid = TargetActor->GetActorInstanceGuid();

        ERadarDetectionState DetectedState;
        ++TargetCount;
        const bool bDetected = EvaluateTarget(
            OwnerLocation, OwnerForward, OwnerYaw,
            bPeriscope, Zoom, CameraFOV,
            TargetActor, TargetVulnerability, DetectedState);

        if (bShouldLogDetect)
        {
            const float _D = FVector::Dist(OwnerLocation, TargetActor->GetActorLocation());
            const float _CR = S->CircleDetectionRange * S->EvalCircleMultiplier(TargetVulnerability);
            const float _FR = S->FOVDetectionRange * S->EvalFOVMultiplier(TargetVulnerability);
            const FVector _Dir = (TargetActor->GetActorLocation() - OwnerLocation).GetSafeNormal();
            const float _Ang = FMath::RadiansToDegrees(FMath::Acos(
                FMath::Clamp(FVector::DotProduct(OwnerForward, _Dir), -1.f, 1.f)));
            const float _FOVHalf = bPeriscope ? S->ComputeFOVHalfAngle(Zoom) : (CameraFOV * 0.5f);

            if (S->bRadarComponentDebug)
            UE_LOG(LogTemp, Log,
                TEXT("[Radar|%s->%s] Dist=%.0f CRange=%.0f(x%.2f) Ang=%.1f/FOVHalf=%.1f FRange=%.0f Det=%d St=%d"),
                *Owner->GetName(), *TargetActor->GetName(), _D,
                _CR, S->EvalCircleMultiplier(TargetVulnerability),
                _Ang, _FOVHalf, _FR, bDetected ? 1 : 0, (int32)DetectedState);
        }

        if (!bDetected) return;

        const bool bIdentified =
            DetectedState == ERadarDetectionState::ClearID ||
            DetectedState == ERadarDetectionState::VulnerableID;

        // Infer entity type from actor class, or use ForcedType
        ERadarEntityType EntityType = ForcedType;
        if (ForcedType == ERadarEntityType::Unknown)
        {
            if (Cast<ASubmarinePawn>(TargetActor))
                EntityType = bIdentified ? ERadarEntityType::Submarine : ERadarEntityType::Unknown;
            else if (Cast<ATorpedoPawn>(TargetActor))
                EntityType = ERadarEntityType::Torpedo;
        }

        // Torpedo icon rotation
        auto GetTorpedoRotation = [&]() -> float
            {
                if (ATorpedoPawn* Torp = Cast<ATorpedoPawn>(TargetActor))
                {
                    const FVector Vel = Torp->GetVelocity().IsNearlyZero()
                        ? Torp->GetActorForwardVector()
                        : Torp->GetVelocity().GetSafeNormal();
                    return ComputeTorpedoIconRotation(Vel, OwnerYaw);
                }
                return 0.f;
            };

        // bFOVDetected: target is genuinely in the camera FOV this tick.
        // bAlwaysAutoDisplay: forced regardless (torpedoes only).
        // For submarines: auto-display only when FOV-detected AND bFOVAutoDetection is on.
        // This is evaluated per-tick so it naturally stops refreshing when out of FOV.
        const bool bFOVDetected = (DetectedState >= ERadarDetectionState::NormalDetection);
        const bool bShouldAutoDisplay = bAlwaysAutoDisplay || (bFOVDetected && S->bFOVAutoDetection);

        FDetectedEntry* Existing = FindEntry(TargetGuid);
        if (!Existing)
        {
            FDetectedEntry NewEntry;
            NewEntry.ActorGuid = TargetGuid;
            NewEntry.EntityType = EntityType;
            NewEntry.DetectionState = DetectedState;
            NewEntry.WorldPosition = TargetActor->GetActorLocation();
            NewEntry.bIsCurrentlyDetected = true;
            NewEntry.bWasEverDetected = true;
            NewEntry.DetectionTimeRemaining = S->GetDetectionLifetime(DetectedState);
            NewEntry.DisplayTimeRemaining = bShouldAutoDisplay ? S->GetDisplayLifetime(DetectedState) : 0.f;
            NewEntry.IconRotation = GetTorpedoRotation();
            DetectedEntries.Add(NewEntry);
        }
        else
        {
            // Update existing entry
            Existing->bIsCurrentlyDetected = true;
            Existing->bWasEverDetected = true;
            Existing->WorldPosition = TargetActor->GetActorLocation();

            // Upgrade state if better detection achieved
            if ((int32)DetectedState > (int32)Existing->DetectionState)
                Existing->DetectionState = DetectedState;

            if (bIdentified && Existing->EntityType == ERadarEntityType::Unknown)
                Existing->EntityType = EntityType;

            // Refresh detection lifetime
            const float NewLifetime = S->GetDetectionLifetime(DetectedState);
            if (NewLifetime > Existing->DetectionTimeRemaining)
                Existing->DetectionTimeRemaining = NewLifetime;

            if (bShouldAutoDisplay)
                Existing->DisplayTimeRemaining = S->GetDisplayLifetime(DetectedState);

            if (Existing->EntityType == ERadarEntityType::Torpedo)
                Existing->IconRotation = GetTorpedoRotation();

        }
    };

    // Pass 1: ITrackableSubmarine actors (submarines)
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* TargetActor = *It;
        if (!TargetActor || TargetActor == Owner) continue;
        if (!TargetActor->Implements<UTrackableSubmarine>()) continue;
        ITrackableSubmarine* TargetInterface = Cast<ITrackableSubmarine>(TargetActor);
        if (!TargetInterface) continue;
        ProcessTarget(TargetActor,
            TargetInterface->GetVulnerabilityScore(),
            ERadarEntityType::Unknown, // infer from cast
            false);
    }

    // Pass 2: Torpedoes (don't implement ITrackableSubmarine)
    for (TActorIterator<ATorpedoPawn> TIt(World); TIt; ++TIt)
    {
        ATorpedoPawn* Torp = *TIt;
        if (!Torp || Torp->GetOwner() == Owner && !S->bOwnTorpedoDetect) continue;
        ProcessTarget(Torp,
            1.f,                           // always max vulnerability
            ERadarEntityType::Torpedo,   // always identified as torpedo
            S->bTorpedoAutoDisplay);     // Auto display torpedo
    }

    if (bShouldLogDetect)
        UE_LOG(LogTemp, Log,
            TEXT("[Radar|%s] entries=%d Settings=%s CircleRange=%.0f FOVRange=%.0f CamFwd=(%.2f,%.2f,%.2f)"),
            *Owner->GetName(), DetectedEntries.Num(),
            S ? *S->GetName() : TEXT("NULL"),
            S ? S->CircleDetectionRange * S->EvalCircleMultiplier(0.2f) : 0.f,
            S ? S->FOVDetectionRange * S->EvalFOVMultiplier(0.2f) : 0.f,
            OwnerForward.X, OwnerForward.Y, OwnerForward.Z);

    // Tick detection timers
    for (FDetectedEntry& Entry : DetectedEntries)
    {
        if (!Entry.bIsCurrentlyDetected)
            Entry.DetectionTimeRemaining =
            FMath::Max(0.f, Entry.DetectionTimeRemaining - DeltaTime);
    }

    // Tick display timers (always, regardless of detection state)
    for (FDetectedEntry& Entry : DetectedEntries)
        Entry.DisplayTimeRemaining =
        FMath::Max(0.f, Entry.DisplayTimeRemaining - DeltaTime);

    // Remove expired entries (detection timer reached 0)
    DetectedEntries.RemoveAll([](const FDetectedEntry& E)
        {
            return E.DetectionTimeRemaining <= 0.f && E.DisplayTimeRemaining <= 0.f;
        });
}

// ---------------------------------------------------------------------------
//  EvaluateTarget
// ---------------------------------------------------------------------------
bool URadarComponent::EvaluateTarget(
    const FVector& OwnerLocation,
    const FVector& OwnerForward,
    float           OwnerYaw,
    bool            bPeriscopeActive,
    float           Zoom,
    float           CameraFOV,
    AActor* TargetActor,
    float           TargetVulnerability,
    ERadarDetectionState& OutState) const
{
    const URadarSettings* S = GetSettings();
    if (!S) return false;

    const FVector TargetLoc = TargetActor->GetActorLocation();
    const FVector ToTarget = TargetLoc - OwnerLocation;
    const float   Distance = ToTarget.Size();
    const FVector ToTargetDir = ToTarget.GetSafeNormal();

    // Apply vulnerability multipliers to ranges
    const float CircleRange = S->CircleDetectionRange
        * S->EvalCircleMultiplier(TargetVulnerability);
    const float FOVRange = S->FOVDetectionRange
        * S->EvalFOVMultiplier(TargetVulnerability);
    const float IDRange = S->IdentificationRange
        * S->EvalIDMultiplier(TargetVulnerability);
    const float PeriscopeRange = S->ComputePeriscopeRange(Zoom)
        * S->EvalFOVMultiplier(TargetVulnerability);

    // FOV half angle:
    //   Normal camera: use CameraFOV/2 (actual rendered FOV from the camera component).
    //   Periscope: use ComputeFOVHalfAngle which accounts for zoom narrowing.
    //   This ensures what you see on screen is exactly what you can detect.
    const float RawFOVHalf = bPeriscopeActive
        ? S->ComputeFOVHalfAngle(Zoom)
        : (CameraFOV * 0.5f);
    const float FOVHalf = RawFOVHalf * S->FOVDetectionAngleMultiplier;

    // Is target inside FOV cone?
    const float AngleToTarget =
        FMath::RadiansToDegrees(FMath::Acos(
            FMath::Clamp(FVector::DotProduct(OwnerForward, ToTargetDir), -1.f, 1.f)));
    const bool bInFOV = (AngleToTarget <= FOVHalf);

    const bool bOverrideDetect = (Distance <= S->LowVulnerabilityOverrideRange);

    bool bDetected = false;
    OutState = ERadarDetectionState::WeakDetection;

    // 1. Circle detection (weakest)
    if (Distance <= CircleRange)
    {
        bDetected = true;
        OutState = ERadarDetectionState::WeakDetection;
    }

    // 2. FOV detection (normal or better)
    if (bInFOV && Distance <= FOVRange)
    {
        bDetected = true;
        if ((int32)ERadarDetectionState::NormalDetection > (int32)OutState)
            OutState = ERadarDetectionState::NormalDetection;
    }

    // 3. Periscope detection (potential identification)
    if (bPeriscopeActive && bInFOV && Distance <= PeriscopeRange)
    {
        bDetected = true;
        if ((int32)ERadarDetectionState::NormalDetection > (int32)OutState)
            OutState = ERadarDetectionState::NormalDetection;

        // Identification range check
        if (Distance <= IDRange)
        {
            // VulnerableID if target has high vulnerability, else ClearID
            if (TargetVulnerability >= S->VulnerabilityThresholdB)
                OutState = ERadarDetectionState::VulnerableID;
            else
                OutState = ERadarDetectionState::ClearID;
        }
    }
    else if (bInFOV && Distance <= IDRange)
    {
        // Non-periscope identification at close range via FOV
        bDetected = true;
        OutState = ERadarDetectionState::ClearID;
    }

    // Vulnerable identification upgrade: high vulnerability + in FOV + in FOV range
    if (bInFOV && Distance <= FOVRange
        && TargetVulnerability >= S->VulnerabilityThresholdB)
    {
        bDetected = true;
        if ((int32)ERadarDetectionState::VulnerableID > (int32)OutState)
            OutState = ERadarDetectionState::VulnerableID;
    }

    return bDetected;
}

// ---------------------------------------------------------------------------
//  FindEntry
// ---------------------------------------------------------------------------
FDetectedEntry* URadarComponent::FindEntry(const FGuid& Guid)
{
    for (FDetectedEntry& Entry : DetectedEntries)
        if (Entry.ActorGuid == Guid)
            return &Entry;
    return nullptr;
}

// ---------------------------------------------------------------------------
//  ComputeTorpedoIconRotation
// ---------------------------------------------------------------------------
float URadarComponent::ComputeTorpedoIconRotation(
    const FVector& TorpedoVelocity,
    float          OwnerYaw) const
{
    const URadarSettings* S = GetSettings();

    // Project torpedo forward onto horizontal plane
    const FVector Flat = FVector(TorpedoVelocity.X, TorpedoVelocity.Y, 0.f)
        .GetSafeNormal();

    // World angle of torpedo direction
    const float WorldAngle =
        FMath::RadiansToDegrees(FMath::Atan2(Flat.Y, Flat.X));

    // Rotate into radar space (subtract submarine yaw)
    const float RadarAngle = WorldAngle - OwnerYaw;

    return RadarAngle + (S ? S->TorpedoIconAngleOffset : -90.f);
}

// ---------------------------------------------------------------------------
//  GetSettings
// ---------------------------------------------------------------------------
const URadarSettings* URadarComponent::GetSettings() const
{
    if (Settings) return Settings;
    return GetDefault<URadarSettings>();
}

// ---------------------------------------------------------------------------
//  ResetScanJustTriggeredFrames
// ---------------------------------------------------------------------------
void URadarComponent::ResetScanJustTriggeredFrames() {
    ScanJustTriggeredFrames = 0;
}