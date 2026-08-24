#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WakeTypes.h"
#include "WakeComponent.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class USubmarinePhysicsComponent;
enum class EWakeLOD : uint8;

// ---------------------------------------------------------------------------
//  UWakeComponent
//  Per-submarine component that drives wake and foam Niagara effects.
//
//  Ownership:
//    Attached to ASubmarinePawn (one per submarine).
//    Registers itself with UOceanWakeRegistry at BeginPlay.
//    UOceanWakeRegistry calls SetWakeLOD() each frame with the distance-based
//    LOD level. This component applies the LOD to its Niagara systems.
//
//  Architecture:
//    This component NEVER reads from MPC_Ocean and NEVER writes physics data.
//    It is purely a visual driver. It reads from USubmarinePhysicsComponent
//    (owned by the same pawn) to get speed and near-surface state.
//
//  Niagara systems used:
//    NS_SubmarineWake  — ribbon trail behind the submarine at the surface
//    NS_SubmarineFoam  — foam/spray chunks spawned at the surface
//    NS_SurfacePulse   — one-shot burst when submarine breaches the surface
//
//  All Niagara systems expose these user parameters (set each tick by this
//  component):
//    SubmarineSpeed      (float)
//    NearSurfaceAlpha    (float, 0-1)
//    AgitationIntensity  (float, 0-1)
//    WakeLOD             (int32, 0-3)
//
//  Replay compatibility:
//    UReplayGhostComponent copies all UNiagaraComponent instances from the
//    source submarine onto the ghost actor (see ReplayGhostComponent.cpp).
//    The wake Niagara components are copied as-is -- they will replay their
//    captured state. No special replay path is needed in this component.
//
//  Split-screen:
//    UOceanWakeRegistry queries both local player cameras and uses the
//    minimum distance for LOD decisions, so wakes visible to either player
//    get the correct LOD automatically.
// ---------------------------------------------------------------------------
UCLASS(ClassGroup = (Wake), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API UWakeComponent : public UActorComponent
{
    GENERATED_BODY()

public:

    UWakeComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // -----------------------------------------------------------------------
    //  Niagara system assets
    //  Assign in the Blueprint subclass or Details panel.
    // -----------------------------------------------------------------------

    /** Wake trail ribbon. Assign NS_SubmarineWake. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|FX")
    TObjectPtr<UNiagaraSystem> WakeTrailSystem;

    /** Foam streak sprites. Assign NS_SubmarineFoam. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|FX")
    TObjectPtr<UNiagaraSystem> FoamStreakSystem;

    /** One-shot surface breach pulse. Assign NS_SurfacePulse. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|FX")
    TObjectPtr<UNiagaraSystem> SurfacePulseSystem;

    // -----------------------------------------------------------------------
    //  Configuration
    // -----------------------------------------------------------------------

    /** Minimum submarine speed (cm/s) before wake effects activate. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|Config",
        meta = (ClampMin = "0.0"))
    float MinSpeedForWake = 50.f;

    // -----------------------------------------------------------------------
    //  LOD control (called by UOceanWakeRegistry)
    // -----------------------------------------------------------------------

    /** Sets the current LOD level and applies it to all Niagara systems. */
    void SetWakeLOD(EWakeLOD NewLOD);

    /** Returns the current LOD level. */
    EWakeLOD GetCurrentLOD() const { return CurrentLOD; }

private:

    // -----------------------------------------------------------------------
    //  Runtime Niagara component instances (created at BeginPlay)
    // -----------------------------------------------------------------------

    UPROPERTY(Transient)
    TObjectPtr<UNiagaraComponent> WakeTrailFX;

    UPROPERTY(Transient)
    TObjectPtr<UNiagaraComponent> FoamStreakFX;

    // -----------------------------------------------------------------------
    //  Cached physics component reference
    //  Set once at BeginPlay from the owning pawn.
    // -----------------------------------------------------------------------

    UPROPERTY(Transient)
    TObjectPtr<USubmarinePhysicsComponent> PhysicsComp;

    // -----------------------------------------------------------------------
    //  State
    // -----------------------------------------------------------------------

    EWakeLOD CurrentLOD = EWakeLOD::Off;
    bool     bWasNearSurface = false;   // for surface breach pulse trigger

    // Frame skip counter for reduced-frequency LOD updates.
    int32 FrameSkipCounter = 0;
    float DebugLogTimer = 0.f;

    // Cached Niagara parameter values.
    // Parameters are only written to Niagara when they actually change,
    // avoiding per-frame GPU buffer updates on GPU Niagara systems.
    float LastPushedSpeed = -1.f;
    float LastPushedAlpha = -1.f;
    float LastPushedAgitation = -1.f;

    // -----------------------------------------------------------------------
    //  Diagnostics
    // -----------------------------------------------------------------------
    // Hard throttle: Niagara params written at most once every 0.1s.
    // Prevents excessive GPU buffer writes regardless of value change rate.
    float PushThrottleTimer = 0.f;
    int32 PushCallCount = 0;
    float PushCountTimer = 0.f;

    // -----------------------------------------------------------------------
    //  Internal helpers
    // -----------------------------------------------------------------------

    // Creates and attaches a Niagara component to the owner actor.
    // Returns null if the system asset is not assigned.
    UNiagaraComponent* CreateAttachedFX(UNiagaraSystem* System, FName ComponentName);

    // Pushes current state to all active Niagara systems as user parameters.
    void PushNiagaraParameters(float Speed, float NearSurfaceAlpha,
        float AgitationIntensity);

    // Triggers a one-shot surface breach pulse at the current actor location.
    void TriggerSurfacePulse();

    // Applies the LOD to Niagara system activation state and parameters.
    void ApplyLODToFX();
};