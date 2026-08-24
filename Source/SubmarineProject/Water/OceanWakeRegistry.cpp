#include "OceanWakeRegistry.h"
#include "WakeComponent.h"
#include "TorpedoWakeComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "DrawDebugHelpers.h"

// ---------------------------------------------------------------------------
//  Static world-to-registry map
//  Required because FWorldDelegates::OnWorldPostActorTick is a global
//  multicast delegate, not per-world. We use this map to route the callback
//  to the correct UOceanWakeRegistry instance for each world.
// ---------------------------------------------------------------------------
TMap<UWorld*, UOceanWakeRegistry*> UOceanWakeRegistry::WorldToRegistry;

// ---------------------------------------------------------------------------
//  Initialize
// ---------------------------------------------------------------------------
void UOceanWakeRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UWorld* World = GetWorld();
    if (!World) return;

    // Register ourselves in the world map so the static callback finds us.
    WorldToRegistry.Add(World, this);

    // UE5.7: The correct world post-actor-tick hook is the global delegate
    // FWorldDelegates::OnWorldPostActorTick, NOT UWorld::OnWorldPostActorTick
    // (which was removed). We use a static callback with a world filter guard.
    WorldTickHandle = FWorldDelegates::OnWorldPostActorTick.AddStatic(
        &UOceanWakeRegistry::OnWorldPostActorTickStatic);
}

// ---------------------------------------------------------------------------
//  Deinitialize
// ---------------------------------------------------------------------------
void UOceanWakeRegistry::Deinitialize()
{
    FWorldDelegates::OnWorldPostActorTick.Remove(WorldTickHandle);

    if (UWorld* World = GetWorld())
    {
        WorldToRegistry.Remove(World);
    }

    SubmarineWakes.Empty();
    TorpedoWakes.Empty();

    Super::Deinitialize();
}

// ---------------------------------------------------------------------------
//  OnWorldPostActorTickStatic
//  Global delegate callback. Routes to the correct registry instance via the
//  world map. Guards against editor preview worlds and null worlds.
// ---------------------------------------------------------------------------
void UOceanWakeRegistry::OnWorldPostActorTickStatic(UWorld* World,
    ELevelTick TickType, float DeltaTime)
{
    if (!World) return;

    // Only run for game worlds -- skip editor preview worlds.
    const EWorldType::Type WorldType = World->WorldType;
    if (WorldType != EWorldType::Game &&
        WorldType != EWorldType::PIE &&
        WorldType != EWorldType::GamePreview) return;

    UOceanWakeRegistry** Found = WorldToRegistry.Find(World);
    if (!Found || !(*Found)) return;

    (*Found)->UpdateLODs(World);
}

// ---------------------------------------------------------------------------
//  UpdateLODs — per-frame main update
// ---------------------------------------------------------------------------
void UOceanWakeRegistry::UpdateLODs(UWorld* World)
{
    PurgeStaleEntries();

    int32 ActiveSubCount = 0;
    int32 ActiveTorpCount = 0;

    for (const auto& WeakComp : SubmarineWakes)
    {
        UWakeComponent* WakeComp = WeakComp.Get();
        if (!WakeComp || !WakeComp->GetOwner()) continue;

        const float Dist = GetDistanceToNearestCamera(World,
            WakeComp->GetOwner()->GetActorLocation());
        WakeComp->SetWakeLOD(ComputeLOD(Dist));
        const EWakeLOD OldLOD = WakeComp->GetCurrentLOD();
        const EWakeLOD NewLOD = ComputeLOD(Dist);

        if (OldLOD != NewLOD)
        {
            UE_LOG(LogTemp, Log,
                TEXT("[OceanWakeRegistry] '%s': LOD %d -> %d  (dist=%.0f)"),
                *WakeComp->GetOwner()->GetName(),
                (int32)OldLOD, (int32)NewLOD, Dist);
        }

        ++ActiveSubCount;
    }

    for (const auto& WeakComp : TorpedoWakes)
    {
        UTorpedoWakeComponent* WakeComp = WeakComp.Get();
        if (!WakeComp || !WakeComp->GetOwner()) continue;

        const float Dist = GetDistanceToNearestCamera(World,
            WakeComp->GetOwner()->GetActorLocation());
        WakeComp->SetWakeLOD(ComputeLOD(Dist));
        ++ActiveTorpCount;
    }

    ActiveSubmarineWakes = ActiveSubCount;
    ActiveTorpedoWakes = ActiveTorpCount;

    if (bDrawDebugRegistry) DrawDebugStats();
}

// ---------------------------------------------------------------------------
//  Registration
// ---------------------------------------------------------------------------
void UOceanWakeRegistry::RegisterSubmarineWake(UWakeComponent* WakeComp)
{
    if (!WakeComp) return;
    for (const auto& E : SubmarineWakes)
        if (E.Get() == WakeComp) return;
    SubmarineWakes.Add(WakeComp);

    UE_LOG(LogTemp, Log,
        TEXT("[OceanWakeRegistry] RegisterSubmarineWake: '%s'. Total submarine wakes: %d"),
        WakeComp->GetOwner() ? *WakeComp->GetOwner()->GetName() : TEXT("?"),
        SubmarineWakes.Num());
}

void UOceanWakeRegistry::UnregisterSubmarineWake(UWakeComponent* WakeComp)
{
    SubmarineWakes.RemoveAll([WakeComp](const TWeakObjectPtr<UWakeComponent>& E)
        {
            return !E.IsValid() || E.Get() == WakeComp;
        });
}

void UOceanWakeRegistry::RegisterTorpedoWake(UTorpedoWakeComponent* WakeComp)
{
    if (!WakeComp) return;

    int32 ValidCount = 0;
    for (const auto& E : TorpedoWakes)
        if (E.IsValid()) ++ValidCount;

    if (ValidCount >= MaxActiveTorpedoWakes) return;

    for (const auto& E : TorpedoWakes)
        if (E.Get() == WakeComp) return;

    TorpedoWakes.Add(WakeComp);
}

void UOceanWakeRegistry::UnregisterTorpedoWake(UTorpedoWakeComponent* WakeComp)
{
    TorpedoWakes.RemoveAll([WakeComp](const TWeakObjectPtr<UTorpedoWakeComponent>& E)
        {
            return !E.IsValid() || E.Get() == WakeComp;
        });
}

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
EWakeLOD UOceanWakeRegistry::ComputeLOD(float Dist) const
{
    if (Dist <= LOD0MaxDistance) return EWakeLOD::Full;
    if (Dist <= LOD1MaxDistance) return EWakeLOD::Reduced;
    if (Dist <= LOD2MaxDistance) return EWakeLOD::Minimal;
    return EWakeLOD::Off;
}

float UOceanWakeRegistry::GetDistanceToNearestCamera(const UWorld* World,
    const FVector& WorldPos) const
{
    if (!World) return 0.f;

    float MinDistSq = FLT_MAX;
    bool  bFound = false;

    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator();
        It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!PC || !PC->IsLocalController()) continue;

        FVector CamLoc; FRotator CamRot;
        PC->GetPlayerViewPoint(CamLoc, CamRot);

        const float DistSq = FVector::DistSquared(WorldPos, CamLoc);
        if (DistSq < MinDistSq) { MinDistSq = DistSq; bFound = true; }
    }

    return bFound ? FMath::Sqrt(MinDistSq) : 0.f;
}

void UOceanWakeRegistry::PurgeStaleEntries()
{
    SubmarineWakes.RemoveAll([](const TWeakObjectPtr<UWakeComponent>& E)
        { return !E.IsValid(); });
    TorpedoWakes.RemoveAll([](const TWeakObjectPtr<UTorpedoWakeComponent>& E)
        { return !E.IsValid(); });
}

void UOceanWakeRegistry::DrawDebugStats() const
{
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Cyan,
            FString::Printf(TEXT("[WakeRegistry] Subs:%d  Torps:%d/%d"),
                ActiveSubmarineWakes, ActiveTorpedoWakes,
                MaxActiveTorpedoWakes));
    }
}