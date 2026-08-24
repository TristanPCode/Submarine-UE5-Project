#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OceanHeightFieldGenerator.generated.h"

class UOceanSubsystem;
class UMaterialInstanceDynamic;
class UTexture2D;

// ---------------------------------------------------------------------------
//  UOceanHeightFieldGenerator
//
//  Owned by AOceanSurfaceActor. Responsible for one task:
//  converting the CPU-authoritative ocean height field into a UTexture2D
//  that the ocean surface material can sample per-vertex in WPO.
//
//  Architecture contract:
//    - CPU (OceanSubsystem) is the ONLY authority for water height.
//    - This component reads from OceanSubsystem at startup and on region
//      change. It never writes back to the subsystem.
//    - The generated texture is world-space 2D (XY only). The surface system
//      does NOT encode 3D region volumes -- those are CPU-only concerns.
//    - The texture encodes: water height (R channel, R16F) at each map XY.
//    - Bilinear filtering in the material provides smooth region transitions
//      that match OceanSubsystem's BlendRadius behaviour.
//
//  How it works:
//    1. At BeginPlay (deferred 1 tick to ensure region registration):
//       iterate a HeightFieldResolution x HeightFieldResolution grid over
//       [MapMin, MapMax]. For each cell, sample the surface height at that
//       XY by calling OceanSubsystem->GetWaterHeightAtPosition with Z set
//       to the default region BaseWaterHeight (surface-level query).
//    2. Pack sampled heights into a UTexture2D (PF_R16F, no mipmaps).
//    3. Pass the texture + map bounds to the surface material MID.
//    4. On OceanSubsystem::OnRegionChanged: rebuild.
//
//  Why a 2D texture and not MPC scalars:
//    MPC scalars are global constants. They cannot encode a spatial field.
//    A texture IS a spatial field. Sampling it at world XY is the only
//    correct way to give the GPU per-pixel height data.
//
//  Performance:
//    HeightFieldResolution=256: 65536 calls to EvaluatePositionInternal at
//    startup. With cached polygon data this completes in < 2ms on any
//    modern CPU. Zero per-frame CPU cost after that.
//    GPU cost: one R16F texture sample per vertex in the WPO chain.
//
//  Texture coordinate convention (for the material):
//    UV = (PixelWorldXY - MapMin) / (MapMax - MapMin)
//    Clamped to [0,1] -- positions outside the map get the boundary value.
//
//  Mesh bounds:
//    WPO can displace vertices beyond the mesh's original bounding box,
//    causing incorrect GPU culling. This component expands the owner mesh
//    bounds by MaxExpectedHeightDisplacement at BeginPlay.
// ---------------------------------------------------------------------------
UCLASS(ClassGroup = (Ocean), meta = (BlueprintSpawnableComponent))
class SUBMARINEPROJECT_API UOceanHeightFieldGenerator : public UActorComponent
{
    GENERATED_BODY()

public:

    UOceanHeightFieldGenerator();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // -----------------------------------------------------------------------
    //  Configuration (set on AOceanSurfaceActor Details panel)
    // -----------------------------------------------------------------------

    // World-space XY minimum corner of the height field coverage.
    // Set this to your map's minimum XY extent + margin.
    // Example: FVector2D(-150000, -150000) for a 300k x 300k map.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|HeightField")
    FVector2D MapMin = FVector2D(-150000.f, -150000.f);

    // World-space XY maximum corner of the height field coverage.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|HeightField")
    FVector2D MapMax = FVector2D(150000.f, 150000.f);

    // Texture resolution (both dimensions). Must be power of two.
    // 128 = 16384 samples, fast. 256 = 65536 samples, recommended.
    // 512 = 262144 samples, only needed for very tight region boundaries.
    // At 256x256 covering 300k x 300k UU: 1 texel = ~1170 UU (~12 meters).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|HeightField",
        meta = (ClampMin = "64", ClampMax = "1024"))
    int32 HeightFieldResolution = 256;

    // Maximum expected WPO height displacement (absolute value, UU).
    // Used to expand static mesh bounds so large displacements are not
    // incorrectly culled by the GPU. Set to the largest height difference
    // between your regions + max Gerstner wave amplitude.
    // Example: if your tallest region is 5000 UU above the default and
    // waves add 500 UU, set this to 5500.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|HeightField",
        meta = (ClampMin = "100.0"))
    float MaxExpectedHeightDisplacement = 5000.f;

    // Name of the texture parameter on the surface material MID.
    // Must match the parameter name in M_Ocean_Surface exactly.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|HeightField")
    FName HeightFieldTextureParamName = TEXT("Ocean_HeightField");

    // Name of the MapMin vector parameter on the surface material MID.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|HeightField")
    FName MapMinParamName = TEXT("Ocean_HeightField_MapMin");

    // Name of the MapMax vector parameter on the surface material MID.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean|HeightField")
    FName MapMaxParamName = TEXT("Ocean_HeightField_MapMax");

    // -----------------------------------------------------------------------
    //  Runtime access (read-only)
    // -----------------------------------------------------------------------

    // Returns the generated height field texture, or null if not yet built.
    UFUNCTION(BlueprintPure, Category = "Ocean|HeightField")
    UTexture2D* GetHeightFieldTexture() const { return HeightFieldTexture; }

    // -----------------------------------------------------------------------
    //  CPU readback (validation tooling)
    //  Retains a CPU copy of the baked height data so debug code can sample
    //  exactly what the GPU samples (bilinear), to measure visual-vs-physics
    //  error without screen-reading the GPU.
    // -----------------------------------------------------------------------

    // Bilinearly samples the baked height field at world XY.
    // Returns the same value the material's Texture2DSample produces.
    // Returns false if the field hasn't been built yet.
    bool SampleHeightFieldBilinear(const FVector2D& WorldXY, float& OutHeight) const;

    // True once the CPU-side copy is available.
    bool HasCPUReadback() const { return CPUHeightData.Num() > 0; }

    // Manually trigger a rebuild (e.g. after runtime region changes).
    UFUNCTION(BlueprintCallable, Category = "Ocean|HeightField")
    void RebuildHeightField();

private:

    // -----------------------------------------------------------------------
    //  Internal
    // -----------------------------------------------------------------------

    // Deferred build: fired one tick after BeginPlay to guarantee all
    // WaterRegionActors have registered with OceanSubsystem first.
    FTimerHandle DeferredBuildTimer;

    // The generated texture. Created once at startup, updated on region change.
    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> HeightFieldTexture;

    // CPU-side copy of the baked height data (validation/readback).
    // Retained so debug code can bilinearly sample exactly what the GPU sees.
    // Stored as float (not FFloat16) for simpler CPU math. ~4MB at 1024.
    TArray<float> CPUHeightData;
    int32 CPUHeightRes = 0;

    // Cached reference to the subsystem. Set at BeginPlay.
    TWeakObjectPtr<UOceanSubsystem> OceanSubsystem;

    // Delegate handle for OceanSubsystem::OnRegionChanged.
    FDelegateHandle RegionChangedHandle;

    // Builds or rebuilds the height field texture from current region data.
    // Samples the CPU subsystem on a HeightFieldResolution x HeightFieldResolution
    // grid, packs results into a UTexture2D (PF_G16, interpreted as R16F).
    void BuildHeightFieldTexture(UOceanSubsystem* OceanSys);

    // Passes the texture and map bounds to all surface material MIDs on
    // the owner AOceanSurfaceActor.
    void PushTextureToMaterials();

    // Expands the owner actor's static mesh component bounds to accommodate
    // MaxExpectedHeightDisplacement WPO displacement.
    void ExpandMeshBounds();

    // Called when OceanSubsystem fires OnRegionChanged.
    void OnRegionChanged();
};