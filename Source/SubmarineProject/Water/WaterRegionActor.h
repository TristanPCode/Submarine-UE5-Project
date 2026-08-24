#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WaterRegionDataAsset.h"
#include "WaterRegionActor.generated.h"

class UArrowComponent;
class UBillboardComponent;
class UProceduralMeshComponent;

// ---------------------------------------------------------------------------
//  AWaterRegionActor
//  Abstract base class for all water region shape actors.
//
//  Ownership:
//    Placed in the level by the designer.
//    Self-registers with UOceanSubsystem at BeginPlay.
//    Self-unregisters at EndPlay.
//
//  Extensibility contract:
//    All shape logic lives entirely in subclasses via EvaluateWeight().
//    UOceanSubsystem never cares about the shape -- it only calls
//    EvaluateWeight() and reads RegionData.
//
//  EvaluateWeight() return value convention:
//    0.0  = position is fully outside this region
//    1.0  = position is fully inside this region
//    0..1 = position is within BlendRadius (soft edge / feathering)
//
//  Editor visualization (all editor-only, zero runtime cost):
//    - Shape wireframe via subclass shape component (UBoxComponent etc.)
//    - Wave direction arrow via UArrowComponent
//    - Region type icon via UBillboardComponent
//    - Optional filled semi-transparent preview via UProceduralMeshComponent
// ---------------------------------------------------------------------------
UCLASS(Abstract, BlueprintType, Blueprintable)
class SUBMARINEPROJECT_API AWaterRegionActor : public AActor
{
    GENERATED_BODY()

public:

    AWaterRegionActor();

    // -----------------------------------------------------------------------
    //  Data
    // -----------------------------------------------------------------------

    // The data asset defining this region's gameplay and rendering parameters.
    // Must be assigned in the editor. If null, the region is skipped entirely.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region")
    TObjectPtr<UWaterRegionDataAsset> RegionData;

    // -----------------------------------------------------------------------
    //  Shape interface (subclasses must implement)
    // -----------------------------------------------------------------------

    // Returns the blend weight of WorldPos relative to this region.
    //   0.0  = fully outside
    //   1.0  = fully inside
    //   0..1 = within BlendRadius, smoothly feathered
    UFUNCTION(BlueprintCallable, Category = "Water Region")
    virtual float EvaluateWeight(const FVector& WorldPos) const
        PURE_VIRTUAL(AWaterRegionActor::EvaluateWeight, return 0.f;);

    // Returns the nearest point on this region's boundary to QueryXY,
    // and the outward-facing normal at that point.
    // Used by OceanSubsystem to populate dynamic transition MPC slots.
    // OutBoundaryPoint and OutOutwardNormal are in world-space XY.
    // Returns false if the region cannot provide boundary data.
    virtual bool GetNearestBoundaryPoint(
        const FVector2D& QueryXY,
        FVector2D& OutBoundaryPoint,
        FVector2D& OutOutwardNormal) const
    {
        // Default: use actor location as center, outward normal = toward query.
        const FVector2D Center(GetActorLocation().X, GetActorLocation().Y);
        const FVector2D ToQuery = QueryXY - Center;
        const float Dist = ToQuery.Size();
        if (Dist < 0.01f) return false;
        OutBoundaryPoint = Center;
        OutOutwardNormal = ToQuery / Dist;
        return true;
    }

    // -----------------------------------------------------------------------
    //  Editor visualization toggles
    // -----------------------------------------------------------------------

    // When true, draws region bounds in the editor viewport at all times.
    // Bounds are NEVER visible at runtime regardless of this flag.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Debug")
    bool bAlwaysDrawBoundsInEditor = true;

    // When true, draws a semi-transparent fill inside the region bounds.
    // Useful when placing many regions to clearly see coverage.
    // Editor-only, zero runtime cost.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Debug")
    bool bShowFilledPreview = true;

    // Opacity of the filled preview mesh [0..1].
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Debug",
        meta = (ClampMin = "0.0", ClampMax = "1.0",
            EditCondition = "bShowFilledPreview"))
    float FilledPreviewOpacity = 0.15f;

    // Color used when drawing this region's wireframe and fill.
    // Defaults to a cyan that is clearly visible against most backgrounds.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Debug")
    FColor EditorBoundsColor = FColor(0, 180, 255);

    // Wireframe line thickness. Increase if the region is hard to select.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Debug",
        meta = (ClampMin = "0.5"))
    float WireframeThickness = 4.f;

    // Texture shown on the billboard icon in the editor viewport.
    // Assign a region-type sprite here (wave icon, storm icon, etc.)
    // to visually distinguish region types when many are placed.
#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Debug")
    TObjectPtr<UTexture2D> RegionIconTexture;
#endif

    // -----------------------------------------------------------------------
    //  Editor component accessors (subclasses call these to set up their
    //  shape visualizer and attach it to the scene root)
    // -----------------------------------------------------------------------

#if WITH_EDITOR
    // Called by subclasses after changing shape parameters to refresh
    // the wave direction arrow and filled preview.
    void RefreshEditorComponents();
#endif

protected:

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    // PostEditMove fires when the actor is dragged in the viewport.
    // Overriding this fixes the wireframe-doesn't-move bug.
    virtual void PostEditMove(bool bFinished) override;
#endif

    // -----------------------------------------------------------------------
    //  Fill preview interface (subclasses implement this)
    // -----------------------------------------------------------------------

    // Subclasses override this to populate the ProceduralMeshFill component
    // with vertices and triangles matching their shape.
    // Called automatically from RefreshEditorComponents when bShowFilledPreview is true.
    // Only called in editor builds -- never at runtime.
#if WITH_EDITOR
    virtual void BuildFillPreviewMesh() {}
#endif

    // -----------------------------------------------------------------------
    //  Editor-only components (all hidden in game, zero runtime cost)
    // -----------------------------------------------------------------------

#if WITH_EDITORONLY_DATA

    // Arrow pointing in the dominant wave direction from the RegionData asset.
    // Updated automatically when RegionData or WaveDirection changes.
    UPROPERTY()
    TObjectPtr<UArrowComponent> WaveDirectionArrow;

    // Billboard icon for region type identification in the editor viewport.
    UPROPERTY()
    TObjectPtr<UBillboardComponent> RegionIconBillboard;

    // Semi-transparent filled mesh showing the region's horizontal footprint.
    // Populated by subclass BuildFillPreviewMesh() implementations.
    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> ProceduralMeshFill;

#endif

private:

#if WITH_EDITOR
    void UpdateWaveDirectionArrow();
    void UpdateFillPreview();

    // Creates and assigns a simple unlit translucent dynamic material
    // for the fill preview mesh. Called once on first use.
    void EnsureFillPreviewMaterial();
#endif

#if WITH_EDITORONLY_DATA

    UPROPERTY(Transient)
    TObjectPtr<class UMaterialInstanceDynamic> FillPreviewMID;

#endif
};