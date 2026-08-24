#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OceanSurfaceActor.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UOceanSubsystem;
class UOceanHeightFieldGenerator;

// ---------------------------------------------------------------------------
//  AOceanSurfaceActor
//  Placeable actor that owns and manages the ocean surface mesh.
//
//  Responsibilities:
//    - Holds the ocean plane static mesh (SM_OceanPlane)
//    - Creates Dynamic Material Instances for the surface and underside
//    - Scales itself to cover the map extents configured in the editor
//    - Positions itself so the mesh aligns with the authoritative water height
//      from UOceanSubsystem at BeginPlay
//    - Exposes quality tier controls for scalability
//    - Has zero gameplay authority: it is purely visual
//
//  Ownership:
//    Placed once in the level. Does not tick by default (MPC handles
//    per-frame rendering parameter updates). Only BeginPlay alignment needed.
//
//  Relationship to UOceanSubsystem:
//    Reads the default water height from UOceanSubsystem at BeginPlay
//    to position the mesh at the correct world Z.
//    Does NOT write to the subsystem. Does NOT read MPC (materials do).
//
//  Two-mesh setup:
//    Slot 0 (SurfaceMesh):   M_Ocean_Surface  -- visible from above (Single Layer Water)
//    Slot 1 (UndersideMesh): M_Ocean_Underside -- visible from below (Translucent, thin plane)
//    The underside mesh is a second static mesh component offset by -1 UU
//    to avoid Z-fighting, with reversed normals handled by the material.
// ---------------------------------------------------------------------------
UCLASS(BlueprintType, Blueprintable)
class SUBMARINEPROJECT_API AOceanSurfaceActor : public AActor
{
    GENERATED_BODY()

public:

    AOceanSurfaceActor();

    virtual void BeginPlay() override;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    // -----------------------------------------------------------------------
    //  Mesh configuration
    // -----------------------------------------------------------------------

    // The subdivided ocean plane mesh.
    // Assign SM_OceanPlane created via UE5 Modeling Tools.
    // Recommended: 128x128 subdivisions for medium quality.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Surface|Mesh")
    TObjectPtr<UStaticMesh> OceanPlaneMesh;

    // Half-extent of the ocean surface in world units (X and Y).
    // The mesh is scaled uniformly to cover this area.
    // Set this to match your largest expected map radius + margin.
    // Example: 500000 covers a 1km x 1km map with margin.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Surface|Mesh",
        meta = (ClampMin = "1000.0"))
    float OceanHalfExtent = 500000.f;

    // -----------------------------------------------------------------------
    //  Material configuration
    // -----------------------------------------------------------------------

    // Base material for the top surface (Single Layer Water domain).
    // Assign M_Ocean_Surface created in Phase 6.2.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Surface|Materials")
    TObjectPtr<UMaterialInterface> SurfaceMaterial;

    // Base material for the underside (Translucent, seen from below).
    // Assign M_Ocean_Underside created in Phase 6.2.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Surface|Materials")
    TObjectPtr<UMaterialInterface> UndersideMaterial;

    // -----------------------------------------------------------------------
    //  Height field
    //  UOceanHeightFieldGenerator is attached as a subcomponent and handles
    //  all height field texture generation. Its configuration (MapMin, MapMax,
    //  resolution, max displacement) is on the component itself.
    //  Access it via the Details panel after adding to the Blueprint.
    // -----------------------------------------------------------------------

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ocean Surface|HeightField")
    TObjectPtr<UOceanHeightFieldGenerator> HeightFieldGenerator;

    // -----------------------------------------------------------------------
    //  Scalability
    // -----------------------------------------------------------------------

    // Manual quality tier override.
    // 0 = Low, 1 = Medium, 2 = High.
    // Set to -1 to follow the project's r.MaterialQualityLevel automatically.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Surface|Quality",
        meta = (ClampMin = "-1", ClampMax = "2"))
    int32 QualityTierOverride = -1;

    // -----------------------------------------------------------------------
    //  Runtime material instance access (read-only, set at BeginPlay)
    // -----------------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Ocean Surface")
    UMaterialInstanceDynamic* GetSurfaceMID() const { return SurfaceMID; }

    UFUNCTION(BlueprintPure, Category = "Ocean Surface")
    UMaterialInstanceDynamic* GetUndersideMID() const { return UndersideMID; }

protected:

    // -----------------------------------------------------------------------
    //  Components
    // -----------------------------------------------------------------------

    // Top surface mesh (visible from above).
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> SurfaceMeshComponent;

    // Underside mesh (visible from below). Offset -1 UU to avoid Z-fighting.
    // Uses the same SM_OceanPlane mesh but with M_Ocean_Underside material.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> UndersideMeshComponent;

private:

    // -----------------------------------------------------------------------
    //  Internal setup
    // -----------------------------------------------------------------------

    // Creates Dynamic Material Instances from the assigned base materials.
    void CreateMaterialInstances();

    // Scales the mesh components to cover OceanHalfExtent.
    // The base SM_OceanPlane is assumed to be 100x100 UU (1m x 1m default).
    // We scale it to (OceanHalfExtent * 2) / 100 on X and Y axes.
    void ApplyMeshScale();

    // Reads the default water height from UOceanSubsystem and sets this
    // actor's Z position so the mesh aligns with the gameplay water surface.
    void AlignToWaterHeight();

    // -----------------------------------------------------------------------
    //  Dynamic material instances (created at BeginPlay, not serialized)
    // -----------------------------------------------------------------------

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> SurfaceMID;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> UndersideMID;
};