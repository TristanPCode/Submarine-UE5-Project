#include "WakeComponent.h"
#include "OceanWakeRegistry.h"
#include "SubmarinePawn.h"
#include "SubmarinePhysicsComponent.h"
#include "SubmarineCharacteristics.h"
#include "OceanWakeRegistry.h"
#include "OceanSubsystem.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

UWakeComponent::UWakeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    // Wake runs after physics (TG_PostPhysics) so it reads the final
    // submarine position and velocity for this frame.
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

// ---------------------------------------------------------------------------
//  BeginPlay
// ---------------------------------------------------------------------------
void UWakeComponent::BeginPlay()
{
    Super::BeginPlay();

    AActor* Owner = GetOwner();
    if (!Owner) return;

    // Cache the physics component from the owning pawn.
    PhysicsComp = Owner->FindComponentByClass<USubmarinePhysicsComponent>();
    if (!PhysicsComp)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WakeComponent] '%s': No USubmarinePhysicsComponent found on owner. "
                "Wake effects will not activate."),
            *Owner->GetName());
    }

    // Create Niagara component instances.
    // These are created at runtime and attached to the actor root.
    // UReplayGhostComponent will copy these onto ghost actors automatically
    // via CopyNiagaraComponents -- no special replay handling needed here.
    WakeTrailFX = CreateAttachedFX(WakeTrailSystem, TEXT("WakeTrail"));
    FoamStreakFX = CreateAttachedFX(FoamStreakSystem, TEXT("FoamStreak"));

    // Position wake trail at the stern by default.
    // The tick updates this each frame based on movement direction.
    if (WakeTrailFX && PhysicsComp)
    {
        const USubmarineCharacteristics* Stats = PhysicsComp->GetStats();
        if (Stats)
        {
            WakeTrailFX->SetRelativeLocation(
                FVector(Stats->WakeSternBackOffset, 0.f, 0.f));
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[WakeComponent] '%s': WakeTrailFX=%s  FoamStreakFX=%s  "
            "WakeTrailSystem=%s  FoamStreakSystem=%s"),
        *Owner->GetName(),
        WakeTrailFX ? TEXT("OK") : TEXT("NULL -- assign NS_SubmarineWake in BP"),
        FoamStreakFX ? TEXT("OK") : TEXT("NULL -- assign NS_SubmarineFoam in BP"),
        WakeTrailSystem ? *WakeTrailSystem->GetName() : TEXT("NOT ASSIGNED"),
        FoamStreakSystem ? *FoamStreakSystem->GetName() : TEXT("NOT ASSIGNED"));

    // Initialize bWasNearSurface to the CURRENT state so we don't fire
    // a spurious surface pulse on the very first tick.
    // PhysicsComp may not have run its first tick yet so we read depth
    // directly from the actor's Z vs water height as a safe approximation.
    bWasNearSurface = PhysicsComp ? PhysicsComp->bIsNearSurface : false;

    // Start with LOD=Off. Registry will assign the correct LOD next frame.
    SetWakeLOD(EWakeLOD::Off);

    // Register with the wake registry.
    UWorld* World = GetWorld();
    if (World)
    {
        UOceanWakeRegistry* Registry = World->GetSubsystem<UOceanWakeRegistry>();
        if (Registry)
        {
            Registry->RegisterSubmarineWake(this);
            UE_LOG(LogTemp, Log,
                TEXT("[WakeComponent] '%s': Registered with OceanWakeRegistry."),
                *Owner->GetName());
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[WakeComponent] '%s': OceanWakeRegistry subsystem not found. "
                    "LOD will never be assigned -- wake will stay Off."),
                *Owner->GetName());
        }
    }
}

// ---------------------------------------------------------------------------
//  EndPlay
// ---------------------------------------------------------------------------
void UWakeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UWorld* World = GetWorld();
    if (World)
    {
        UOceanWakeRegistry* Registry = World->GetSubsystem<UOceanWakeRegistry>();
        if (Registry)
        {
            Registry->UnregisterSubmarineWake(this);
        }
    }

    Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
//  TickComponent
// ---------------------------------------------------------------------------
void UWakeComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // -----------------------------------------------------------------------
    //  LOG BLOCK A: Wake component state (every 3s)
    // -----------------------------------------------------------------------
    DebugLogTimer += DeltaTime;
    if (DebugLogTimer >= 3.f)
    {
        DebugLogTimer = 0.f;
        const float Speed = PhysicsComp ? PhysicsComp->PhysicsVelocity.Size() : 0.f;
        const float NSAlpha = PhysicsComp ? PhysicsComp->NearSurfaceAlpha : 0.f;
        const bool  bNearSurf = PhysicsComp ? PhysicsComp->bIsNearSurface : false;
        UE_LOG(LogTemp, Log,
            TEXT("[WakeComp|A] '%s': LOD=%d  Speed=%.0f  NSAlpha=%.2f  "
                "NearSurf=%s  TrailActive=%s  FoamActive=%s"),
            GetOwner() ? *GetOwner()->GetName() : TEXT("?"),
            (int32)CurrentLOD, Speed, NSAlpha,
            bNearSurf ? TEXT("YES") : TEXT("NO"),
            (WakeTrailFX && WakeTrailFX->IsActive()) ? TEXT("YES") : TEXT("NO"),
            (FoamStreakFX && FoamStreakFX->IsActive()) ? TEXT("YES") : TEXT("NO"));
    }

    // -----------------------------------------------------------------------
    //  LOG BLOCK B: PushNiagaraParameters call counter (every 2s)
    //  Tells us exactly how often we actually write to Niagara.
    // -----------------------------------------------------------------------
    PushCountTimer += DeltaTime;
    if (PushCountTimer >= 2.f)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[WakeComp|B] '%s': PushNiagaraParameters called %d times in last 2s"),
            GetOwner() ? *GetOwner()->GetName() : TEXT("?"),
            PushCallCount);
        PushCallCount = 0;
        PushCountTimer = 0.f;
    }

    // LOD=Off: Niagara systems are paused. No work needed.
    if (CurrentLOD == EWakeLOD::Off) return;

    // Frame skipping for reduced-frequency LODs.
    // LOD1 updates every 2 frames, LOD2 every 4 frames.
    ++FrameSkipCounter;
    const int32 RequiredInterval = (CurrentLOD == EWakeLOD::Full) ? 1
        : (CurrentLOD == EWakeLOD::Reduced) ? 2 : 4;
    if (FrameSkipCounter < RequiredInterval) return;
    FrameSkipCounter = 0;

    if (!PhysicsComp) return;

    const float NearSurfaceAlpha = PhysicsComp->NearSurfaceAlpha;

    const ITrackableSubmarine* Trackable = Cast<ITrackableSubmarine>(GetOwner());
    const float LinearSpeed = Trackable
        ? Trackable->GetCurrentSpeed()
        : PhysicsComp->PhysicsVelocity.Size();

    // Surface breach pulse: fire once when the submarine transitions from
    // submerged to near-surface. Prevents repeated triggers.
    const bool bIsNearSurface = PhysicsComp->bIsNearSurface;
    if (bIsNearSurface && !bWasNearSurface)
    {
        TriggerSurfacePulse();
    }
    bWasNearSurface = bIsNearSurface;

    // -----------------------------------------------------------------------
    //  Wake spawn position: direction-aware X + water surface Z
    // -----------------------------------------------------------------------
    if (WakeTrailFX && PhysicsComp)
    {
        const USubmarineCharacteristics* Stats = PhysicsComp->GetStats();
        AActor* WOwner = GetOwner();
        if (Stats && WOwner)
        {
            const float XOffset = (LinearSpeed >= -10.f)
                ? Stats->WakeSternBackOffset
                : Stats->WakeSternFrontOffset;
            const float RelZ = PhysicsComp->GetWaterSurfaceZ()
                - WOwner->GetActorLocation().Z
                + Stats->WakeZOffset;  // DA-tunable visual height tweak
            WakeTrailFX->SetRelativeLocation(FVector(XOffset, 0.f, RelZ));
        }
    }
    if (FoamStreakFX && PhysicsComp)
    {
        AActor* WOwner = GetOwner();
        if (WOwner)
        {
            const USubmarineCharacteristics* FoamStats = PhysicsComp->GetStats();
            const float FoamRelZ = PhysicsComp->GetWaterSurfaceZ()
                - WOwner->GetActorLocation().Z
                + (FoamStats ? FoamStats->WakeZOffset : 0.f);
            FoamStreakFX->SetRelativeLocation(FVector(0.f, 0.f, FoamRelZ));
        }
    }

    // Determine effective speed to push: 0 when not emitting.
    // Use absolute value so backward movement also activates wake.
    // LinearSpeed is negative when reversing (from GetCurrentSpeed()).
    const float AbsSpeed = FMath::Abs(LinearSpeed);
    const float EffectiveSpeed = (bIsNearSurface && AbsSpeed >= MinSpeedForWake)
        ? AbsSpeed : 0.f;

    // Query agitation only when actually near surface and moving.
    float AgitationIntensity = 0.f;
    if (bIsNearSurface && EffectiveSpeed > 0.f)
    {
        AActor* Owner = GetOwner();
        UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
        UOceanSubsystem* OceanSys = GI ? GI->GetSubsystem<UOceanSubsystem>() : nullptr;
        if (OceanSys)
            AgitationIntensity = OceanSys->GetAgitationAtPosition(
                Owner->GetActorLocation());
    }

    // Only write Niagara parameters when values actually changed.
    // Calling SetVariableFloat every frame on a GPU Niagara system
    // triggers a GPU buffer update each call, which can cause
    // driver timeouts at 60fps with 4+ simultaneous systems.
    const float AlphaToPush = bIsNearSurface ? NearSurfaceAlpha : 0.f;

    // Rate-limit Niagara parameter writes using the DA-configurable interval.
    // Default 0.033s = ~30 updates/sec. Tune WakeParameterUpdateInterval in DA
    // to increase fluidity (lower value) or reduce GPU pressure (higher value).
    PushThrottleTimer += DeltaTime;
    const USubmarineCharacteristics* ThrottleStats = PhysicsComp->GetStats();
    const float ThrottleInterval = ThrottleStats
        ? ThrottleStats->WakeParameterUpdateInterval
        : 0.033f;

    const bool bParamsChanged =
        !FMath::IsNearlyEqual(EffectiveSpeed, LastPushedSpeed, 1.f) ||
        !FMath::IsNearlyEqual(AlphaToPush, LastPushedAlpha, 0.01f) ||
        !FMath::IsNearlyEqual(AgitationIntensity, LastPushedAgitation, 0.01f);

    if (bParamsChanged && PushThrottleTimer >= ThrottleInterval)
    {
        PushThrottleTimer = 0.f;
        LastPushedSpeed = EffectiveSpeed;
        LastPushedAlpha = AlphaToPush;
        LastPushedAgitation = AgitationIntensity;
        ++PushCallCount;
        PushNiagaraParameters(EffectiveSpeed, AlphaToPush, AgitationIntensity);
    }
}

// ---------------------------------------------------------------------------
//  SetWakeLOD
//  Called by UOceanWakeRegistry each frame with the distance-based LOD level.
// ---------------------------------------------------------------------------
void UWakeComponent::SetWakeLOD(EWakeLOD NewLOD)
{
    if (CurrentLOD == NewLOD) return;
    CurrentLOD = NewLOD;
    ApplyLODToFX();
}

// ---------------------------------------------------------------------------
//  ApplyLODToFX
//  Activates or pauses Niagara systems based on the current LOD.
// ---------------------------------------------------------------------------
void UWakeComponent::ApplyLODToFX()
{
    const bool bActive = (CurrentLOD != EWakeLOD::Off);

    if (WakeTrailFX)
    {
        if (bActive)
        {
            // Only activate if not already running.
            // Activate(true) on an already-active infinite-loop Niagara system
            // spawns a completely new system instance on top of the existing one.
            if (!WakeTrailFX->IsActive())
                WakeTrailFX->Activate(true);
        }
        else
        {
            WakeTrailFX->Deactivate();
        }
        WakeTrailFX->SetVariableInt(TEXT("WakeLOD"), (int32)CurrentLOD);
    }

    if (FoamStreakFX)
    {
        const bool bFoamActive = bActive && (CurrentLOD <= EWakeLOD::Reduced);
        if (bFoamActive)
        {
            if (!FoamStreakFX->IsActive())
                FoamStreakFX->Activate(true);
        }
        else
        {
            FoamStreakFX->Deactivate();
        }
        FoamStreakFX->SetVariableInt(TEXT("WakeLOD"), (int32)CurrentLOD);
    }
}

// ---------------------------------------------------------------------------
//  PushNiagaraParameters
// ---------------------------------------------------------------------------
void UWakeComponent::PushNiagaraParameters(float Speed, float NearSurfaceAlpha,
    float AgitationIntensity)
{
    // -----------------------------------------------------------------------
    //  LOG BLOCK C: Parameter validation (fires on every push).
    //  If a parameter name doesn't exist in the Niagara asset, UE5 silently
    //  ignores the write -- but on GPU systems it may also crash.
    //  We log the values being pushed so we can confirm they are sane.
    // -----------------------------------------------------------------------
    UE_LOG(LogTemp, Log,
        TEXT("[WakeComp|C] PushParams: Speed=%.1f  Alpha=%.2f  Agit=%.2f  "
            "TrailIsActive=%s  FoamIsActive=%s"),
        Speed, NearSurfaceAlpha, AgitationIntensity,
        (WakeTrailFX && WakeTrailFX->IsActive()) ? TEXT("YES") : TEXT("NO"),
        (FoamStreakFX && FoamStreakFX->IsActive()) ? TEXT("YES") : TEXT("NO"));

    const float WaterZ = PhysicsComp ? PhysicsComp->GetWaterSurfaceZ() : 0.f;

    if (WakeTrailFX && WakeTrailFX->IsActive())
    {
        WakeTrailFX->SetVariableFloat(TEXT("SubmarineSpeed"), Speed);
        WakeTrailFX->SetVariableFloat(TEXT("NearSurfaceAlpha"), NearSurfaceAlpha);
        WakeTrailFX->SetVariableFloat(TEXT("AgitationIntensity"), AgitationIntensity);
        WakeTrailFX->SetVariableInt(TEXT("WakeLOD"), (int32)CurrentLOD);
        WakeTrailFX->SetVariableFloat(TEXT("WaterSurfaceZ"), WaterZ);
    }

    if (FoamStreakFX && FoamStreakFX->IsActive())
    {
        FoamStreakFX->SetVariableFloat(TEXT("SubmarineSpeed"), Speed);
        FoamStreakFX->SetVariableFloat(TEXT("NearSurfaceAlpha"), NearSurfaceAlpha);
        FoamStreakFX->SetVariableFloat(TEXT("AgitationIntensity"), AgitationIntensity);
        FoamStreakFX->SetVariableInt(TEXT("WakeLOD"), (int32)CurrentLOD);
        FoamStreakFX->SetVariableFloat(TEXT("WaterSurfaceZ"), WaterZ);
    }
}

// ---------------------------------------------------------------------------
//  TriggerSurfacePulse
//  Spawns a one-shot burst Niagara effect at the submarine's current position
//  when it transitions from submerged to near-surface.
// ---------------------------------------------------------------------------
void UWakeComponent::TriggerSurfacePulse()
{
    if (!SurfacePulseSystem) return;

    AActor* Owner = GetOwner();
    if (!Owner) return;

    // Spawn at the water surface height, not the submarine's Z position.
    // This places the splash at the right height even if the submarine
    // hasn't fully breached the surface yet.
    FVector SpawnLoc = Owner->GetActorLocation();
    if (PhysicsComp)
    {
        SpawnLoc.Z = PhysicsComp->GetWaterSurfaceZ();
    }

    UNiagaraFunctionLibrary::SpawnSystemAtLocation(
        GetWorld(), SurfacePulseSystem, SpawnLoc,
        FRotator::ZeroRotator, FVector::OneVector,
        /*bAutoDestroy=*/true, /*bAutoActivate=*/true,
        ENCPoolMethod::None);
}

// ---------------------------------------------------------------------------
//  CreateAttachedFX
// ---------------------------------------------------------------------------
UNiagaraComponent* UWakeComponent::CreateAttachedFX(UNiagaraSystem* System,
    FName ComponentName)
{
    if (!System) return nullptr;

    AActor* Owner = GetOwner();
    if (!Owner) return nullptr;

    UNiagaraComponent* FXComp = NewObject<UNiagaraComponent>(Owner, ComponentName);
    FXComp->SetAsset(System);
    // bAutoActivate must be false BEFORE RegisterComponent so the component
    // doesn't auto-activate on registration. Calling Deactivate() after
    // RegisterComponent in the same frame can leave Niagara in a stale state
    // where subsequent Activate() calls are silently ignored.
    FXComp->bAutoActivate = false;
    FXComp->SetupAttachment(Owner->GetRootComponent());
    FXComp->RegisterComponent();

    return FXComp;
}