#pragma once

#include "CoreMinimal.h"
#include "WaterRegionActor.h"
#include "SphereWaterRegion.generated.h"

// ---------------------------------------------------------------------------
//  ASphereWaterRegion
//  Sphere-shaped water region. Best suited for isolated circular zones:
//  deep trenches, underwater vents, localized storm cells, whirlpool areas.
//
//  Weight evaluation:
//    Computes the distance from WorldPos to the actor's center.
//    If distance <= (Radius - BlendRadius):  weight = 1.0  (fully inside)
//    If distance >= Radius:                  weight = 0.0  (fully outside)
//    Between those two:                      weight = 0..1 (smooth blend)
// ---------------------------------------------------------------------------
UCLASS(BlueprintType, Blueprintable)
class SUBMARINEPROJECT_API ASphereWaterRegion : public AWaterRegionActor
{
    GENERATED_BODY()

public:

    ASphereWaterRegion();

    // Radius of the sphere in world units.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Sphere",
        meta = (ClampMin = "1.0"))
    float Radius = 5000.f;

    // EvaluateWeight override - sphere shape logic
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
    void RefreshEditorSphere();
#endif

    // Sphere component used purely for editor visualization (not for collision).
    // WITH_EDITORONLY_DATA is required for UPROPERTY inside editor-only guards.
#if WITH_EDITORONLY_DATA
    UPROPERTY()
    TObjectPtr<class USphereComponent> EditorVisualizerSphere;
#endif
};