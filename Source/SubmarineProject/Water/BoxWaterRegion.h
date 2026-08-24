#pragma once

#include "CoreMinimal.h"
#include "WaterRegionActor.h"
#include "BoxWaterRegion.generated.h"

// ---------------------------------------------------------------------------
//  ABoxWaterRegion
//  Axis-aligned box region. The most common shape for open-ocean zones,
//  rectangular sea lanes, or rectangular storm areas.
//
//  The box extent is defined by BoxHalfExtent.
//  BlendRadius from the DataAsset feathers the weight near the box edges.
//
//  Weight evaluation:
//    Computes the minimum distance from WorldPos to the box surface.
//    If inside and beyond BlendRadius from the edge: weight = 1.0
//    If inside and within BlendRadius of the edge:   weight = 0..1 (smooth)
//    If outside:                                     weight = 0.0
//
//  Note: the box uses world-space AABB (no rotation).
//  If you need rotated boxes, derive a new subclass - do not add rotation
//  complexity here, it would break the AABB fast-path.
// ---------------------------------------------------------------------------
UCLASS(BlueprintType, Blueprintable)
class SUBMARINEPROJECT_API ABoxWaterRegion : public AWaterRegionActor
{
    GENERATED_BODY()

public:

    ABoxWaterRegion();

    // Half-extents of the box in world units (X, Y, Z).
    // Z is intentionally large by default: water regions span full depth.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Box",
        meta = (ClampMin = "1.0"))
    FVector BoxHalfExtent = FVector(10000.f, 10000.f, 50000.f);

    // EvaluateWeight override - box shape logic
    virtual float EvaluateWeight(const FVector& WorldPos) const override;

    virtual bool GetNearestBoundaryPoint(
        const FVector2D& QueryXY,
        FVector2D& OutBoundaryPoint,
        FVector2D& OutOutwardNormal) const override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PostEditMove(bool bFinished) override;
    virtual void BuildFillPreviewMesh() override;
#endif

private:

#if WITH_EDITOR
    void RefreshEditorBox();
#endif

    // Box component used purely for editor visualization (not for collision).
    // WITH_EDITORONLY_DATA is required for UPROPERTY inside editor-only guards.
#if WITH_EDITORONLY_DATA
    UPROPERTY()
    TObjectPtr<class UBoxComponent> EditorVisualizerBox;
#endif
};