#pragma once

#include "CoreMinimal.h"
#include "WaterRegionActor.h"
#include "CylinderWaterRegion.generated.h"

// ---------------------------------------------------------------------------
//  ACylinderWaterRegion
//  Vertical cylinder region. The most practical shape for most ocean use
//  cases: storm columns, whirlpool zones, underwater vents, circular bays.
//
//  Unlike ASphereWaterRegion, the cylinder has independent XY radius and Z
//  height extent. This means you can define a region that covers any vertical
//  range while keeping a circular horizontal boundary -- which matches how
//  real ocean phenomena behave (a storm cell is tall, not spherical).
//
//  Weight evaluation:
//    1. Z check: if the point is outside [ActorZ - HalfHeight, ActorZ + HalfHeight],
//       weight = 0 immediately (fast vertical reject).
//    2. XY check: distance from point to actor XY center vs Radius.
//    3. BlendRadius feathering applied to BOTH axes independently.
//       The final weight is the minimum of the two blend factors, so a point
//       near the cylinder edge AND near the top/bottom cap gets correctly
//       feathered by whichever boundary is closest.
// ---------------------------------------------------------------------------
UCLASS(BlueprintType, Blueprintable)
class SUBMARINEPROJECT_API ACylinderWaterRegion : public AWaterRegionActor
{
    GENERATED_BODY()

public:

    ACylinderWaterRegion();

    // Horizontal radius of the cylinder (world units, cm).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Cylinder",
        meta = (ClampMin = "1.0"))
    float Radius = 5000.f;

    // Total height of the cylinder (world units, cm).
    // The cylinder is centered on the actor's Z position.
    // Set this large enough to encompass the full gameplay depth range.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water Region|Cylinder",
        meta = (ClampMin = "1.0"))
    float Height = 100000.f;

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
    void RefreshEditorCylinder();
#endif

#if WITH_EDITORONLY_DATA
    // Capsule component used as editor visualization proxy.
    // A true cylinder isn't a built-in shape component in UE5, but a capsule
    // with zero hemisphere radius visually approximates a cylinder well enough
    // for placement purposes. The actual EvaluateWeight logic is a true cylinder.
    UPROPERTY()
    TObjectPtr<class UCapsuleComponent> EditorVisualizerCapsule;
#endif
};