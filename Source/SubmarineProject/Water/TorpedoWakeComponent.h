#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WakeTypes.h"
#include "TorpedoWakeComponent.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UTorpedoPhysicsComponent;

// ---------------------------------------------------------------------------
//  UTorpedoWakeComponent
//  Lightweight per-torpedo variant of UWakeComponent.
//
//  Key differences from UWakeComponent:
//    - Only one Niagara system (NS_TorpedoWake, thinner ribbon, 3s lifetime)
//    - No foam streak (too expensive for up to 30 simultaneous torpedoes)
//    - Activation is fully binary (on/off from bIsNearSurface) -- no alpha
//    - Budget-limited by UOceanWakeRegistry::MaxActiveTorpedoWakes
//    - No surface breach pulse (torpedoes are too fast for a meaningful pulse)
//
//  Assign to ATorpedoPawn (or its Blueprint subclass) in the components panel.
//  Assign NS_TorpedoWake to TorpedoWakeSystem in the Details panel.
// ---------------------------------------------------------------------------
UCLASS(ClassGroup = (Wake), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API UTorpedoWakeComponent : public UActorComponent
{
    GENERATED_BODY()

public:

    UTorpedoWakeComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // -----------------------------------------------------------------------
    //  Niagara system asset
    // -----------------------------------------------------------------------

    /** Torpedo wake ribbon. Assign NS_TorpedoWake. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|FX")
    TObjectPtr<UNiagaraSystem> TorpedoWakeSystem;

    // -----------------------------------------------------------------------
    //  LOD control (called by UOceanWakeRegistry)
    // -----------------------------------------------------------------------

    void SetWakeLOD(EWakeLOD NewLOD);
    EWakeLOD GetCurrentLOD() const { return CurrentLOD; }

private:

    UPROPERTY(Transient)
    TObjectPtr<UNiagaraComponent> WakeFX;

    UPROPERTY(Transient)
    TObjectPtr<UTorpedoPhysicsComponent> PhysicsComp;

    EWakeLOD CurrentLOD = EWakeLOD::Off;
    int32    FrameSkipCounter = 0;

    UNiagaraComponent* CreateAttachedFX(UNiagaraSystem* System, FName ComponentName);
    void ApplyLODToFX();
};