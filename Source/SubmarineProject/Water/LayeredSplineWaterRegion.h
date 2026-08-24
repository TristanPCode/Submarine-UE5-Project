#pragma once

#include "CoreMinimal.h"
#include "WaterRegionActor.h"
#include "LayeredSplineWaterRegion.generated.h"

class USplineComponent;

// ---------------------------------------------------------------------------
//  ELayerPairingOrphanMode
//  Controls how orphaned LeastLayer points are handled during vertex pairing.
//
//  DuplicateVertex (recommended):
//    When a LeastLayer orphan L1 cannot be resolved by simple reassignment
//    (because M1's current target L2 would become orphaned), a duplicate
//    vertex M1b is created in the pairing data only -- not in the spline.
//    M1b starts at M1's position and lerps toward L1. The interpolated
//    polygon gains one extra vertex per unresolvable orphan. Geometrically
//    clean, no angular distortion.
//
//  CascadeReassign:
//    Orphan L1 forces a reassignment cascade: M1->L1, then find new target
//    for L2 (excluding M1), and so on. Guaranteed to terminate since
//    LeastLayer.Num() < MostLayer.Num(). Polygon vertex count stays exactly
//    MostLayer.Num(), but distant pairings from the cascade may produce
//    slight angular distortion near the reassigned vertices.
// ---------------------------------------------------------------------------
UENUM(BlueprintType)
enum class ELayerPairingOrphanMode : uint8
{
    DuplicateVertex  UMETA(DisplayName = "Duplicate Vertex (recommended)"),
    CascadeReassign  UMETA(DisplayName = "Cascade Reassign"),
};


// ---------------------------------------------------------------------------
//  FSplineLayerInfo
//  Lightweight metadata for one layer. The actual spline component is owned
//  directly by the actor (visible in the component hierarchy) and looked up
//  by name at runtime. This avoids the struct-hides-component problem where
//  USplineComponent inside a USTRUCT array is not editable in the Details panel.
// ---------------------------------------------------------------------------
USTRUCT(BlueprintType)
struct FSplineLayer
{
    GENERATED_BODY()

    // World-space Z altitude of this layer.
    // Layers should be ordered from lowest to highest Z for correct interpolation.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Layer")
    float LayerAltitude = 0.f;

    // Name of the USplineComponent attached to the actor for this layer.
    // Used to look up the component at runtime via GetComponentByClass/FindComponent.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spline Layer")
    FName SplineComponentName = NAME_None;

    // Human-readable label for this layer (shown in debug and editor).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Layer")
    FName LayerLabel = NAME_None;
};


// ---------------------------------------------------------------------------
//  ALayeredSplineWaterRegion
//  A 3D water region defined by multiple horizontal spline layers at
//  different altitudes.
//
//  KEY DESIGN CHANGE from previous version:
//    Spline components are attached DIRECTLY to the actor and visible in
//    the UE5 component hierarchy (Components panel). Each spline is named
//    "LayerSpline_N" and is fully editable -- you can select it, move its
//    points, and see it in the viewport exactly like a normal SplineComponent.
//    The SplineLayers array only stores metadata (altitude, label, component
//    name). The actual spline geometry lives in the component.
//
//  How to edit layers in the editor:
//    1. Select the actor in the level.
//    2. In the Details panel, expand the Components section.
//    3. Click "LayerSpline_0", "LayerSpline_1", etc. to select each spline.
//    4. Edit the spline points directly in the viewport (Alt+Click to add,
//       drag to move, right-click to change tangent type to Linear).
//    5. Adjust LayerAltitude in the SplineLayers array for each layer's Z.
//
//  Weight evaluation:
//    Same as before: find bracketing layers, evaluate 2D polygon test for
//    each, lerp results by vertical blend factor T.
// ---------------------------------------------------------------------------
UCLASS(BlueprintType, Blueprintable)
class SUBMARINEPROJECT_API ALayeredSplineWaterRegion : public AWaterRegionActor
{
    GENERATED_BODY()

public:

    ALayeredSplineWaterRegion();

    // -----------------------------------------------------------------------
    //  Layer metadata
    // -----------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Layers")
    TArray<FSplineLayer> SplineLayers;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Layers",
        meta = (ClampMin = "8", ClampMax = "128"))
    int32 SplineSampleCount = 32;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Layers")
    bool bClampToOuterLayers = true;

    // Controls how orphaned LeastLayer vertices are resolved during pairing.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Layers")
    ELayerPairingOrphanMode OrphanMode = ELayerPairingOrphanMode::DuplicateVertex;

    // -----------------------------------------------------------------------
    //  Editor - Add/Remove layer buttons
    // -----------------------------------------------------------------------

    // Set these before clicking "Add Layer".
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Layers|Add Layer")
    float NewLayerAltitude = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Layers|Add Layer")
    FName NewLayerLabel = NAME_None;

    // Clicking this button in the Details panel calls AddLayerFromEditor().
    UFUNCTION(CallInEditor, Category = "Water Region|Layers|Add Layer")
    void AddLayerFromEditor();

    // Set this index before clicking "Remove Layer".
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Layers|Remove Layer",
        meta = (ClampMin = "0"))
    int32 LayerIndexToRemove = 0;

    UFUNCTION(CallInEditor, Category = "Water Region|Layers|Remove Layer")
    void RemoveLayerFromEditor();

    // -----------------------------------------------------------------------
    //  Editor - Layer connection wireframe
    // -----------------------------------------------------------------------

    // When true, draws lines connecting corresponding points between adjacent
    // layers in the editor viewport. Helps visualize the 3D lofted volume.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Debug")
    bool bDrawLayerConnections = true;

    // Show debug logs for Pairing association between 2 spline layers.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Debug")
    bool bLogPairing = true;

    // Log every EvaluateWeight call (very verbose, disable after diagnosis).
    // Shows LocalZ, T, interpolated polygon point count, and final weight.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Debug")
    bool bLogEvaluateWeight = false;

    // -----------------------------------------------------------------------
    //  Shape interface
    // -----------------------------------------------------------------------

    virtual float EvaluateWeight(const FVector& WorldPos) const override;
    virtual bool GetNearestBoundaryPoint(
        const FVector2D& QueryXY,
        FVector2D& OutBoundaryPoint,
        FVector2D& OutOutwardNormal) const override;
    virtual void BeginPlay() override;
    virtual void PostInitializeComponents() override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Destroyed() override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PostEditMove(bool bFinished) override;
    virtual void BuildFillPreviewMesh() override;
#endif

    // -----------------------------------------------------------------------
    //  Layer management API
    // -----------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Water Region|Layers")
    int32 AddLayer(float Altitude, FName Label = NAME_None);

    UFUNCTION(BlueprintCallable, Category = "Water Region|Layers")
    void RemoveLayer(int32 LayerIndex);

    UFUNCTION(BlueprintCallable, Category = "Water Region|Layers")
    USplineComponent* GetLayerSpline(int32 LayerIndex) const;

private:

    // -----------------------------------------------------------------------
    //  Per-layer polygon cache
    // -----------------------------------------------------------------------

    struct FLayerPolygonCache
    {
        TArray<FVector2D> Points;
        // Centroid of this layer's polygon (used for angle-based pairing).
        FVector2D Centroid = FVector2D::ZeroVector;
        float Altitude = 0.f;
        bool bValid = false;
    };

    TArray<FLayerPolygonCache> PolygonCaches;

    // -----------------------------------------------------------------------
    //  Inter-layer vertex pairing
    // -----------------------------------------------------------------------

    // One extra vertex entry used by O1 (DuplicateVertex mode).
    // Exists only in pairing data -- never in the spline or polygon cache.
    struct FExtraPairingVertex
    {
        // Insert M1b immediately after this index in MostLayer's point order.
        int32 InsertAfterMostIndex = -1;
        // The LeastLayer point index this duplicate vertex lerps toward.
        int32 TargetLeastIndex = -1;
    };

    struct FLayerPairing
    {
        // MostLayer point index -> LeastLayer point index.
        // One entry per MostLayer point. This is the primary interpolation map.
        TArray<int32> MostToLeast;

        // Which of Lower/Upper is the MostLayer in this pair.
        // true  = Lower is MostLayer, Upper is LeastLayer.
        // false = Upper is MostLayer, Lower is LeastLayer.
        bool bLowerIsMost = true;

        // Extra duplicate vertices added by O1 to cover unresolvable orphans.
        // Empty when OrphanMode == CascadeReassign.
        TArray<FExtraPairingVertex> ExtraVertices;
    };

    TArray<FLayerPairing> LayerPairings;

    // -----------------------------------------------------------------------
    //  Cache and pairing build
    // -----------------------------------------------------------------------

    void RebuildAllCaches();
    void RebuildLayerCache(int32 Index);
    void RebuildLayerPairings();
    void SortAndRebuild();
    void SyncSplineAltitudes();
    void RepairSplineComponentNames();

    // -----------------------------------------------------------------------
    //  Interpolated polygon evaluation (the correct 3D approach)
    // -----------------------------------------------------------------------

    // Builds an interpolated polygon between LowerCache and UpperCache at
    // blend factor T, using the precomputed pairing.
    // OutPolygon is populated with the interpolated 2D points.
    void BuildInterpolatedPolygon(int32 PairIdx, float T,
        TArray<FVector2D>& OutPolygon) const;

    // -----------------------------------------------------------------------
    //  Polygon math (static helpers)
    // -----------------------------------------------------------------------

    static bool IsPointInPolygon2D(const TArray<FVector2D>& Polygon,
        const FVector2D& Point);

    static float DistanceToPolygonEdge2D(const TArray<FVector2D>& Polygon,
        const FVector2D& Point);

    static FVector2D ComputeCentroid(const TArray<FVector2D>& Points);

    // Returns the angle of Point relative to Origin, in radians [-PI, PI].
    static float AngleFromOrigin(const FVector2D& Point, const FVector2D& Origin);

    // -----------------------------------------------------------------------
    //  Default spline components (created in constructor)
    // -----------------------------------------------------------------------

    UPROPERTY(VisibleAnywhere, Category = "Components")
    TObjectPtr<USplineComponent> LayerSpline_0;

    UPROPERTY(VisibleAnywhere, Category = "Components")
    TObjectPtr<USplineComponent> LayerSpline_1;

#if WITH_EDITOR
    // Draws connection lines between paired vertices of adjacent layers.
    void DrawLayerConnectionLines() const;
#endif
};