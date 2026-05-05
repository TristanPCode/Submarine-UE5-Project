// Fill out your copyright notice in the Description page of Project Settings.

#include "ReplayGhostComponent.h"
#include "ReplayHelpers.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

UReplayGhostComponent::UReplayGhostComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

// ---------------------------------------------------------------------------
//  BeginPlay — tag the owning actor as ReplayDynamic
// ---------------------------------------------------------------------------
void UReplayGhostComponent::BeginPlay()
{
    Super::BeginPlay();

    AActor* Owner = GetOwner();
    if (!Owner) return;

    // Tag the real actor so HideRealActorsForDeadPlayer can find it generically.
    if (!Owner->ActorHasTag(UReplayHelpers::Tag_ReplayDynamic))
        Owner->Tags.Add(UReplayHelpers::Tag_ReplayDynamic);

    // Log all components at BeginPlay so we can verify BP meshes are registered
    TArray<UStaticMeshComponent*> AllMeshes;
    Owner->GetComponents<UStaticMeshComponent>(AllMeshes);

    const UReplaySettings* S = GetSettings();
    if (!S) return;
    if (S->bLogReplayGhost) {
        UE_LOG(LogTemp, Log,
            TEXT("[ReplayGhost] BeginPlay on '%s' — found %d StaticMeshComponents:"),
            *Owner->GetName(), AllMeshes.Num());

        for (UStaticMeshComponent* M : AllMeshes)
        {
            if (S->bLogReplayGhost) {
                UE_LOG(LogTemp, Log,
                    TEXT("[ReplayGhost]   Mesh='%s'  Asset='%s'"),
                    *M->GetName(),
                    M->GetStaticMesh() ? *M->GetStaticMesh()->GetName() : TEXT("null"));
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  CloneComponentsOntoGhost
// ---------------------------------------------------------------------------
void UReplayGhostComponent::CloneComponentsOntoGhost(AActor* TargetGhost) const
{
    if (!TargetGhost || !GetOwner()) return;

    CopyStaticMeshComponents(TargetGhost);
    CopyNiagaraComponents(TargetGhost);
    CopyExtraComponents(TargetGhost);   // future hook — does nothing by default

    // Force the ghost actor visible: the source actor may be hidden
    // (e.g. dead submarine after FreezeOnDeath sets SetActorHiddenInGame(true)).
    // We must undo that propagation on the ghost explicitly.
    if (TargetGhost)
    {
        TargetGhost->SetActorHiddenInGame(false);
        const UReplaySettings* S = GetSettings();
        if (!S) return;
        if (S->bLogReplayGhost) {
            UE_LOG(LogTemp, Log, TEXT("[ReplayGhost] Ghost actor forced visible after clone: '%s'"), *TargetGhost->GetName());
        }
    }
}

// ---------------------------------------------------------------------------
//  ShouldSkipMesh
//
//  Returns true for mesh components that should NOT be copied onto ghosts:
//
//  1. CameraProxyMeshComponent — UE5 adds these automatically for every
//     UCameraComponent. They have the MatineeCam_SM asset assigned.
//     They are purely editor/debug aids and must not appear in-game on ghosts.
//
//  2. UCameraComponent subclasses — shouldn't be UStaticMeshComponent but
//     guard anyway.
//
//  We filter by component name prefix "CameraProxyMeshComponent" which is
//  the internal name UE uses for these auto-generated proxies.
// ---------------------------------------------------------------------------
static bool ShouldSkipMesh(const UStaticMeshComponent* Src)
{
    if (!Src) return true;

    // Filter camera proxy meshes by name
    const FString CompName = Src->GetName();
    if (CompName.StartsWith(TEXT("CameraProxyMeshComponent")))
        return true;

    // Filter by asset name as a secondary check
    if (Src->GetStaticMesh())
    {
        const FString AssetName = Src->GetStaticMesh()->GetName();
        if (AssetName == TEXT("MatineeCam_SM") ||
            AssetName.StartsWith(TEXT("EditorCamera")) ||
            AssetName.StartsWith(TEXT("CameraActor")))
            return true;
    }

    // Camera component guard (shouldn't happen but be safe)
    if (Src->IsA<UCameraComponent>())
        return true;

    return false;
}

// ---------------------------------------------------------------------------
//  CopyStaticMeshComponents
// ---------------------------------------------------------------------------
void UReplayGhostComponent::CopyStaticMeshComponents(AActor* Target) const
{
    AActor* Source = GetOwner();
    if (!Source || !Target) return;

    TArray<UStaticMeshComponent*> SourceMeshes;
    Source->GetComponents<UStaticMeshComponent>(SourceMeshes);

    const UReplaySettings* S = GetSettings();
    if (!S) return;
    if (S->bLogReplayGhost) {
        UE_LOG(LogTemp, Log,
            TEXT("[ReplayGhost] CopyStaticMeshComponents: '%s' has %d mesh components"),
            *Source->GetName(), SourceMeshes.Num());
    }

    if (SourceMeshes.Num() == 0)
    {
        if (S->bLogReplayGhost) {
            UE_LOG(LogTemp, Warning,
                TEXT("[ReplayGhost] No StaticMeshComponents found on '%s' — ghost will be invisible"),
                *Source->GetName());
        }
        return;
    }

    // The ghost root — we'll create it from the first valid mesh
    USceneComponent* GhostRoot = Target->GetRootComponent();

    // Source actor root transform — used to compute each mesh's offset
    // relative to the actor origin, which we then apply on the ghost
    const FTransform SourceRootTransform = Source->GetActorTransform();
    int32 CopiedCount = 0;

    for (UStaticMeshComponent* Src : SourceMeshes)
    {
        if (!Src || !Src->GetStaticMesh()) continue;

        // Skip camera proxy meshes and any other non-gameplay meshes
        if (ShouldSkipMesh(Src))
        {
            if (S->bLogReplayGhost) {
                UE_LOG(LogTemp, Log,
                    TEXT("[ReplayGhost]   SKIPPED '%s' (camera proxy or editor mesh)"),
                    *Src->GetName());
            }
            continue;
        }

        const FName NewName = MakeUniqueObjectName(
            Target, UStaticMeshComponent::StaticClass(),
            FName(*FString::Printf(TEXT("GhostSMC_%s"), *Src->GetName())));

        // KEY: Use Target as outer but call SetupAttachment BEFORE RegisterComponent.
        // In UE5, NewObject<UActorComponent>(ActorOuter) does NOT auto-register --
        // the component stays unregistered until RegisterComponent() is called explicitly.
        // However if RegisterComponent() has already been called (e.g. by a previous code path),
        // we guard with IsRegistered().
        UStaticMeshComponent* GhostMesh = NewObject<UStaticMeshComponent>(
            Target, UStaticMeshComponent::StaticClass(), NewName);
        if (!GhostMesh) continue;

        // Sanity-guard: if somehow already registered (should not happen), skip
        if (GhostMesh->IsRegistered())
        {
            if (S->bLogReplayGhost) {
                UE_LOG(LogTemp, Warning,
                    TEXT("[ReplayGhost]   WARN: '%s' already registered before we attached -- skipping"),
                    *NewName.ToString());
            }
            continue;
        }

        if (!GhostRoot)
        {
            // First mesh becomes the ghost root -- register first, then promote to root
            GhostMesh->RegisterComponent();
            Target->SetRootComponent(GhostMesh);
            GhostRoot = GhostMesh;
            // Root sits at the ghost actor's origin -- no relative offset needed
            GhostMesh->SetRelativeTransform(FTransform::Identity);
        }
        else
        {
            // Children: SetupAttachment MUST happen before RegisterComponent.
            // After RegisterComponent the component is placed in the world and
            // subsequent attachment changes do not reposition it.
            GhostMesh->SetupAttachment(GhostRoot);
            GhostMesh->RegisterComponent();

            // Offset relative to the source actor root (handles arbitrary nesting depth)
            const FTransform RelToRoot =
                Src->GetComponentTransform().GetRelativeTransform(SourceRootTransform);
            GhostMesh->SetRelativeTransform(RelToRoot);

            if (S->bLogReplayGhost) {
                UE_LOG(LogTemp, Log,
                    TEXT("[ReplayGhost]   Child mesh '%s' relative offset: T=%s R=%s"),
                    *Src->GetName(),
                    *RelToRoot.GetTranslation().ToString(),
                    *RelToRoot.GetRotation().Rotator().ToString());
            }
        }

        // Copy mesh asset and materials
        GhostMesh->SetStaticMesh(Src->GetStaticMesh());
        for (int32 i = 0; i < Src->GetNumMaterials(); ++i)
            if (UMaterialInterface* Mat = Src->GetMaterial(i))
                GhostMesh->SetMaterial(i, Mat);

        // Ghost-safe flags + force visible (source actor may be hidden after FreezeOnDeath)
        ApplyGhostFlags(GhostMesh);
        GhostMesh->SetHiddenInGame(false);
        GhostMesh->SetVisibility(true);

        // Associate with actor so it appears in GetComponents queries
        Target->AddInstanceComponent(GhostMesh);

        if (S->bLogReplayGhost) {
            UE_LOG(LogTemp, Log,
                TEXT("[ReplayGhost]   Copied mesh '%s' (asset='%s')"),
                *Src->GetName(),
                *Src->GetStaticMesh()->GetName());
        }
    }
}

// ---------------------------------------------------------------------------
//  CopyNiagaraComponents
//  Copies every UNiagaraComponent from Source onto Target.
//  The copy is activated immediately so persistent effects (engine trail,
//  glow, bubbles) play on the ghost during the replay.
// ---------------------------------------------------------------------------
void UReplayGhostComponent::CopyNiagaraComponents(AActor* Target) const
{
    AActor* Source = GetOwner();
    if (!Source || !Target) return;

    TArray<UNiagaraComponent*> FXComps;
    Source->GetComponents<UNiagaraComponent>(FXComps);
    if (FXComps.Num() == 0) return;

    // We need a root for attachment; StaticMesh copy may have created one already.
    USceneComponent* Root = Target->GetRootComponent();
    const FTransform SourceRootTransform = Source->GetActorTransform();


    for (UNiagaraComponent* Src : FXComps)
    {
        if (!Src || !Src->GetAsset()) continue;

        const FName NewName = MakeUniqueObjectName(
            Target, UNiagaraComponent::StaticClass(),
            FName(*FString::Printf(TEXT("GhostFX_%s"), *Src->GetName())));

        UNiagaraComponent* GhostFX = NewObject<UNiagaraComponent>(
            Target, UNiagaraComponent::StaticClass(), NewName);
        if (!GhostFX) continue;

        GhostFX->SetAsset(Src->GetAsset());
        GhostFX->RegisterComponent();

        if (Root)
        {
            GhostFX->SetupAttachment(Root);

            // Same world-relative transform logic as meshes
            const FTransform RelativeToRoot =
                Src->GetComponentTransform().GetRelativeTransform(SourceRootTransform);
            GhostFX->SetRelativeTransform(RelativeToRoot);
        }


        // Ghost-safe flags
        GhostFX->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        GhostFX->SetGenerateOverlapEvents(false);
        GhostFX->SetCastShadow(false);

        // Activate so persistent effects play during the replay
        GhostFX->Activate(true);
    }

    const UReplaySettings* S = GetSettings();
    if (!S) return;
    if (S->bLogReplayGhost) {
        UE_LOG(LogTemp, Verbose,
            TEXT("[ReplayGhost] Copied %d NiagaraComponents from '%s'"),
            FXComps.Num(), *Source->GetName());
    }
}

// ---------------------------------------------------------------------------
//  ApplyGhostFlags
// ---------------------------------------------------------------------------
void UReplayGhostComponent::ApplyGhostFlags(UPrimitiveComponent* Prim)
{
    if (!Prim) return;

    Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Prim->SetCollisionProfileName(TEXT("NoCollision"));
    Prim->SetSimulatePhysics(false);
    Prim->SetCastShadow(false);
    Prim->SetGenerateOverlapEvents(false);
    Prim->SetCanEverAffectNavigation(false);
}

// ---------------------------------------------------------------------------
//  Settings helper
// ---------------------------------------------------------------------------
const UReplaySettings* UReplayGhostComponent::GetSettings() const
{
    if (Settings) return Settings;
    return GetDefault<UReplaySettings>();
}