#pragma once

#include "CoreMinimal.h"
#include "WaterRegionActor.h"
#include "SplineWaterRegion.generated.h"

class USplineComponent;

// ---------------------------------------------------------------------------
//  ASplineWaterRegion
//  Closed-spline polygon water region. The spline defines an arbitrary 2D
//  boundary (XY plane). Depth extent is handled by HeightExtent, centered
//  on the actor's Z position, mirroring the box region's Z behavior.
//
//  The spline MUST be set to closed (bClosedLoop = true) in the editor.
//  If it is not closed, EvaluateWeight() always returns 0 with a warning.
//
//  Weight evaluation (2D point-in-polygon + blend):
//    1. Flatten WorldPos and spline points to XY (ignore Z for containment).
//    2. Run a ray-casting point-in-polygon test against spline sample points.
//    3. If inside, compute approximate distance to the nearest spline edge
//       and apply BlendRadius feathering exactly as box/sphere do.
//    4. Apply HeightExtent check on Z independently (same logic as box Z axis).
//
//  Performance note:
//    EvaluateWeight() samples the spline into a polygon cache (SplinePoints)
//    at BeginPlay and whenever the spline is modified in the editor.
//    Runtime evaluation is O(N) where N = SplineSampleCount. Keep N <= 64
//    for complex shapes. For simple shapes (8-16 points) this is negligible.
//
//  When to use vs Box/Sphere:
//    Use ASplineWaterRegion when you need non-rectangular, non-circular
//    boundaries: bays, archipelagos, irregularly shaped storm fronts.
//    For regular shapes, ABoxWaterRegion and ASphereWaterRegion are faster.
// ---------------------------------------------------------------------------
UCLASS(BlueprintType, Blueprintable)
class SUBMARINEPROJECT_API ASplineWaterRegion : public AWaterRegionActor
{
    GENERATED_BODY()

public:

    ASplineWaterRegion();

    // The spline component defining the region boundary.
    // Set bClosedLoop = true in the editor. Shape is defined in XY; Z is ignored
    // for containment (HeightExtent handles vertical bounds separately).
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Water Region|Spline")
    TObjectPtr<USplineComponent> SplineComponent;

    // Number of points sampled from the spline to build the polygon cache.
    // Higher values = smoother boundary at higher O(N) evaluation cost.
    // 32 is a good default for most organic shapes.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Spline",
        meta = (ClampMin = "8", ClampMax = "128"))
    int32 SplineSampleCount = 32;

    // Half-height of the region on the Z axis, centered on the actor's Z.
    // Matches the box region's vertical unboundedness default.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Spline",
        meta = (ClampMin = "1.0"))
    float HeightHalfExtent = 50000.f;

    // EvaluateWeight override - spline polygon logic
    virtual float EvaluateWeight(const FVector& WorldPos) const override;

    // Returns the nearest polygon edge point and its outward normal.
    // Uses the same PolygonCache as EvaluateWeight for consistency.
    virtual bool GetNearestBoundaryPoint(
        const FVector2D& QueryXY,
        FVector2D& OutBoundaryPoint,
        FVector2D& OutOutwardNormal) const override;

    virtual void BeginPlay() override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PostEditMove(bool bFinished) override;
    virtual void BuildFillPreviewMesh() override;
#endif

private:

    // Rebuilds PolygonCache from the current spline state.
    // Called at BeginPlay and on any editor spline modification.
    void RebuildPolygonCache();

    // 2D point-in-polygon test using ray casting (Jordan curve theorem).
    // Tests against PolygonCache in local XY space.
    bool IsPointInPolygon2D(const FVector2D& Point) const;

    // Returns the approximate minimum distance from Point to the nearest
    // polygon edge. Used for BlendRadius feathering.
    float DistanceToPolygonEdge2D(const FVector2D& Point) const;

    // Cached 2D polygon points in actor local space (XY only).
    // Rebuilt whenever the spline changes.
    TArray<FVector2D> PolygonCache;

    // True once PolygonCache has been built and is valid for queries.
    bool bPolygonCacheValid = false;
};