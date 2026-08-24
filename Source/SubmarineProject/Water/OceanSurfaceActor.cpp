#include "OceanSurfaceActor.h"
#include "OceanSubsystem.h"
#include "OceanHeightFieldGenerator.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"

AOceanSurfaceActor::AOceanSurfaceActor()
{
    // The surface actor never ticks. All per-frame rendering updates are
    // driven by MPC_Ocean writes from UOceanSubsystem. The material reads
    // MPC parameters directly -- no per-frame C++ work here.
    // No per-frame tick needed on this actor. All rendering parameter
    // updates go through MPC (OceanSubsystem writes them every frame).
    // Height field topology is handled by UOceanHeightFieldGenerator.
    PrimaryActorTick.bCanEverTick = false;

    // Create the height field generator subcomponent.
    // Its BeginPlay builds the height field texture from OceanSubsystem.
    HeightFieldGenerator = CreateDefaultSubobject<UOceanHeightFieldGenerator>(
        TEXT("HeightFieldGenerator"));

    // Surface mesh: faces upward, receives Single Layer Water material.
    SurfaceMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SurfaceMesh"));
    SetRootComponent(SurfaceMeshComponent);
    SurfaceMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SurfaceMeshComponent->SetCastShadow(false);
    SurfaceMeshComponent->SetReceivesDecals(false);

    // Underside mesh: offset slightly below surface to avoid Z-fighting.
    // The material handles the visual difference (reversed normals, Fresnel).
    UndersideMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("UndersideMesh"));
    UndersideMeshComponent->SetupAttachment(GetRootComponent());
    UndersideMeshComponent->SetRelativeLocation(FVector(0.f, 0.f, -1.f));
    UndersideMeshComponent->SetRelativeRotation(FRotator(180.f, 0.f, 0.f));
    UndersideMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    UndersideMeshComponent->SetCastShadow(false);
    UndersideMeshComponent->SetReceivesDecals(false);
}

// ---------------------------------------------------------------------------
//  BeginPlay
// ---------------------------------------------------------------------------
void AOceanSurfaceActor::BeginPlay()
{
    Super::BeginPlay();

    // Order matters: mesh scale first, then material creation, then alignment.
    // Alignment reads water height from the subsystem which is already valid
    // at BeginPlay (regions register before the surface actor ticks).
    ApplyMeshScale();
    CreateMaterialInstances();
    AlignToWaterHeight();

    // Write the mesh's world Z to MPC once -- used by the material to convert
    // the height field's absolute world-space height into a relative WPO offset.
    // The actor never moves after BeginPlay so this is written once and stays valid.
    UWorld* World = GetWorld();
    UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    UOceanSubsystem* OceanSys = GI ? GI->GetSubsystem<UOceanSubsystem>() : nullptr;
    if (OceanSys && OceanSys->OceanMPC)
    {
        UMaterialParameterCollectionInstance* MPCI =
            World->GetParameterCollectionInstance(OceanSys->OceanMPC);
        if (MPCI)
        {
            MPCI->SetScalarParameterValue(TEXT("Ocean_MeshBaseZ"),
                GetActorLocation().Z);
        }
    }
}

// ---------------------------------------------------------------------------
//  ApplyMeshScale
//  SM_OceanPlane is created at 100x100 UU (1m x 1m) in the Modeling Tools.
//  We scale X and Y uniformly so the mesh covers (OceanHalfExtent * 2) UU.
//  Z scale is always 1.0 -- wave displacement is handled via WPO in the material.
// ---------------------------------------------------------------------------
void AOceanSurfaceActor::ApplyMeshScale()
{
    if (!OceanPlaneMesh) return;

    // Assign the mesh to both components.
    SurfaceMeshComponent->SetStaticMesh(OceanPlaneMesh);
    UndersideMeshComponent->SetStaticMesh(OceanPlaneMesh);

    // Compute scale: the plane is 100x100 UU by default from Modeling Tools.
    // ScaleFactor = (OceanHalfExtent * 2) / 100
    const float PlaneSizeUU = 100.f;
    const float ScaleFactor = (OceanHalfExtent * 2.f) / PlaneSizeUU;

    SurfaceMeshComponent->SetRelativeScale3D(FVector(ScaleFactor, ScaleFactor, 1.f));
    UndersideMeshComponent->SetRelativeScale3D(FVector(ScaleFactor, ScaleFactor, 1.f));
}

// ---------------------------------------------------------------------------
//  CreateMaterialInstances
//  Creates Dynamic Material Instances from the base material assignments.
//  DMIs allow future code to push per-instance parameters if needed.
//  For now the materials read entirely from MPC -- the DMIs are created
//  but not written to, preserving the single-write-through-MPC architecture.
// ---------------------------------------------------------------------------
void AOceanSurfaceActor::CreateMaterialInstances()
{
    if (SurfaceMaterial)
    {
        SurfaceMID = UMaterialInstanceDynamic::Create(SurfaceMaterial, this);
        SurfaceMeshComponent->SetMaterial(0, SurfaceMID);
        UE_LOG(LogTemp, Log,
            TEXT("[OceanSurfaceActor] '%s' Surface material assigned: %s"),
            *GetName(), *SurfaceMaterial->GetName());
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[OceanSurfaceActor] '%s' SurfaceMaterial is not assigned. ")
            TEXT("Assign M_Ocean_Surface in the Details panel."),
            *GetName());
    }

    if (UndersideMaterial)
    {
        UndersideMID = UMaterialInstanceDynamic::Create(UndersideMaterial, this);
        UndersideMeshComponent->SetMaterial(0, UndersideMID);

        // Diagnostic: log the underside mesh transform so we can verify it is
        // correctly positioned relative to the surface mesh.
        const FVector UndersideRelLoc = UndersideMeshComponent->GetRelativeLocation();
        UE_LOG(LogTemp, Log,
            TEXT("[OceanSurfaceActor] '%s' Underside material assigned: %s. ")
            TEXT("Underside mesh relative Z offset: %.2f (should be -1.0)."),
            *GetName(), *UndersideMaterial->GetName(), UndersideRelLoc.Z);

        // Diagnostic: confirm visibility flags are correct.
        UE_LOG(LogTemp, Log,
            TEXT("[OceanSurfaceActor] '%s' Underside mesh hidden in game: %s. ")
            TEXT("If true, underside will not render at runtime -- set to false."),
            *GetName(),
            UndersideMeshComponent->bHiddenInGame ? TEXT("YES (PROBLEM)") : TEXT("NO (correct)"));
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[OceanSurfaceActor] '%s' UndersideMaterial is not assigned. ")
            TEXT("Assign M_Ocean_Underside in the Details panel."),
            *GetName());
    }
}

// ---------------------------------------------------------------------------
//  AlignToWaterHeight
//  Moves the actor's Z position to match the authoritative water surface height
//  from UOceanSubsystem. Uses the world origin (0,0,0) as the sample position
//  since at BeginPlay we use the default region's flat water height.
//  This ensures the visual mesh aligns exactly with the physics water plane.
// ---------------------------------------------------------------------------
void AOceanSurfaceActor::AlignToWaterHeight()
{
    UWorld* World = GetWorld();
    UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    if (!GI)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[OceanSurfaceActor] '%s' AlignToWaterHeight: no GameInstance found. ")
            TEXT("Actor will stay at placed Z position."),
            *GetName());
        return;
    }

    UOceanSubsystem* OceanSys = GI->GetSubsystem<UOceanSubsystem>();
    if (!OceanSys)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[OceanSurfaceActor] '%s' AlignToWaterHeight: UOceanSubsystem not found. ")
            TEXT("Check that GameInstance class is set to BP_SubmarineGameInstance."),
            *GetName());
        return;
    }

    // Sample at world origin -- gives us the default region's BaseWaterHeight.
    // For maps with multiple regions at different heights, the surface actor
    // should be placed at the dominant region's height and regions handle
    // the visual height variation via WPO in the material.
    const float WaterZ = OceanSys->GetWaterHeightAtPosition(FVector::ZeroVector);
    UE_LOG(LogTemp, Log,
        TEXT("[OceanSurfaceActor] '%s' AlignToWaterHeight: WaterZ=%.2f. ")
        TEXT("Actor moved to Z=%.2f."),
        *GetName(), WaterZ, WaterZ);
    SetActorLocation(FVector(0.f, 0.f, WaterZ));
}

// ---------------------------------------------------------------------------
//  Editor - refresh mesh scale when OceanHalfExtent changes in the viewport
// ---------------------------------------------------------------------------
#if WITH_EDITOR
void AOceanSurfaceActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    if (OceanPlaneMesh)
    {
        const float PlaneSizeUU = 100.f;
        const float ScaleFactor = (OceanHalfExtent * 2.f) / PlaneSizeUU;
        SurfaceMeshComponent->SetRelativeScale3D(FVector(ScaleFactor, ScaleFactor, 1.f));
        UndersideMeshComponent->SetRelativeScale3D(FVector(ScaleFactor, ScaleFactor, 1.f));
    }
}
#endif