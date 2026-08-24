#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WakeTypes.h"
#include "OceanWakeRegistry.generated.h"

class UWakeComponent;
class UTorpedoWakeComponent;

// ---------------------------------------------------------------------------
//  UOceanWakeRegistry
//  World subsystem. Owns nothing; coordinates all wake components.
//
//  Responsibilities:
//    - Registration / unregistration of UWakeComponent and UTorpedoWakeComponent
//    - Per-frame LOD decisions based on distance to nearest local camera
//    - Budget enforcement (max active torpedo wakes, max active foam effects)
//    - Debug statistics (draw calls, active component counts)
//
//  Architecture note:
//    This is a UWorldSubsystem (not UGameInstanceSubsystem) because wake
//    effects are level-local. It is created fresh for each world, which
//    naturally handles level transitions and PIE cleanup.
//
//    All registration arrays use TWeakObjectPtr so destroyed actors are
//    silently skipped and cleaned up during the next LOD update pass.
// ---------------------------------------------------------------------------
UCLASS()
class SUBMARINEPROJECT_API UOceanWakeRegistry : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // -----------------------------------------------------------------------
    //  UWorldSubsystem interface
    // -----------------------------------------------------------------------

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // -----------------------------------------------------------------------
    //  Registration
    //  Called from UWakeComponent/UTorpedoWakeComponent BeginPlay/EndPlay.
    // -----------------------------------------------------------------------

    void RegisterSubmarineWake(UWakeComponent* WakeComp);
    void UnregisterSubmarineWake(UWakeComponent* WakeComp);

    void RegisterTorpedoWake(UTorpedoWakeComponent* WakeComp);
    void UnregisterTorpedoWake(UTorpedoWakeComponent* WakeComp);

    // -----------------------------------------------------------------------
    //  Budget configuration (designer-tunable)
    //  Set these via a DA or directly in the GameInstance if needed.
    // -----------------------------------------------------------------------

    // Maximum number of torpedo wake effects active simultaneously.
    // If this limit is reached, new registrations are silently rejected
    // until a slot frees up. Default: 12.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|Budget")
    int32 MaxActiveTorpedoWakes = 12;

    // Maximum number of foam streak effects active simultaneously.
    // Foam is more expensive than the base trail, so its budget is smaller.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|Budget")
    int32 MaxActiveFoamEffects = 8;

    // -----------------------------------------------------------------------
    //  LOD distance thresholds (cm from nearest camera)
    // -----------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|LOD")
    float LOD0MaxDistance = 3000.f;     // Full

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|LOD")
    float LOD1MaxDistance = 8000.f;     // Reduced

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|LOD")
    float LOD2MaxDistance = 20000.f;    // Minimal

    // Beyond LOD2MaxDistance: Off

    // -----------------------------------------------------------------------
    //  Debug
    // -----------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wake|Debug")
    bool bDrawDebugRegistry = false;

    // -----------------------------------------------------------------------
    //  Runtime statistics (read-only, updated each frame)
    // -----------------------------------------------------------------------

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wake|Stats")
    int32 ActiveSubmarineWakes = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wake|Stats")
    int32 ActiveTorpedoWakes = 0;

private:

    // -----------------------------------------------------------------------
    //  Registered components (weak pointers -- stale entries auto-skip)
    // -----------------------------------------------------------------------

    TArray<TWeakObjectPtr<UWakeComponent>>        SubmarineWakes;
    TArray<TWeakObjectPtr<UTorpedoWakeComponent>> TorpedoWakes;

    // -----------------------------------------------------------------------
    //  Per-frame LOD update
    // -----------------------------------------------------------------------

    FDelegateHandle WorldTickHandle;

    // Bound to the world tick delegate. Fires after all actor ticks.
    static void OnWorldPostActorTickStatic(UWorld* World, ELevelTick TickType,
        float DeltaTime);
    void UpdateLODs(UWorld* World);

    // Returns the EWakeLOD for a given distance from the nearest camera.
    EWakeLOD ComputeLOD(float DistanceToNearestCamera) const;

    // Returns the distance from WorldPos to the nearest local player camera.
    // Returns 0 if the world has no local player controllers (server-only mode).
    float    GetDistanceToNearestCamera(const UWorld* World,
        const FVector& WorldPos) const;

    // Removes null/stale entries from both registration arrays.
    // Called once per frame during LOD update to keep arrays clean.
    void PurgeStaleEntries();

    void DrawDebugStats() const;

    // Weak self-reference so the static delegate callback can find this instance.
    // UWorldSubsystem lifetime is tied to the world, so this is safe.
    static TMap<UWorld*, UOceanWakeRegistry*> WorldToRegistry;
};