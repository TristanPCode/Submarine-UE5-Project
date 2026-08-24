#include "CylinderWaterRegion.h"
#include "WaterRegionDataAsset.h"
#include "ProceduralMeshComponent.h"

#if WITH_EDITORONLY_DATA
#include "Components/CapsuleComponent.h"
#endif

ACylinderWaterRegion::ACylinderWaterRegion()
{
    PrimaryActorTick.bCanEverTick = false;

#if WITH_EDITORONLY_DATA
    EditorVisualizerCapsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("EditorVisualizerCapsule"));
    EditorVisualizerCapsule->SetupAttachment(GetRootComponent());
    EditorVisualizerCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    EditorVisualizerCapsule->ShapeColor = EditorBoundsColor;
    // UCapsuleComponent takes HalfHeight and Radius.
    EditorVisualizerCapsule->SetCapsuleSize(Radius, Height * 0.5f);
    EditorVisualizerCapsule->SetHiddenInGame(true);
#endif
}

// ---------------------------------------------------------------------------
//  EvaluateWeight
//
//  Two-axis blend:
//    WeightXY: radial distance from actor XY center vs Radius
//    WeightZ:  distance from actor Z center vs HalfHeight
//
//  Final weight = min(WeightXY, WeightZ) so the point near ANY boundary
//  gets correctly feathered, not just the XY edge.
// ---------------------------------------------------------------------------
float ACylinderWaterRegion::EvaluateWeight(const FVector& WorldPos) const
{
    if (!RegionData) return 0.f;

    const FVector ActorPos = GetActorLocation();
    const float HalfHeight = Height * 0.5f;
    const float BlendRadius = FMath::Max(RegionData->BlendRadius, 1.f);

    // -----------------------------------------------------------------------
    //  Z axis check (fast vertical reject)
    // -----------------------------------------------------------------------
    const float DeltaZ = FMath::Abs(WorldPos.Z - ActorPos.Z);
    const float InnerHalf = FMath::Max(HalfHeight - BlendRadius, 0.f);

    if (DeltaZ >= HalfHeight) return 0.f; // fully outside vertically

    float WeightZ = 1.f;
    if (DeltaZ > InnerHalf)
    {
        const float ZBlendAlpha = (DeltaZ - InnerHalf) / FMath::Max(BlendRadius, 1.f);
        WeightZ = 1.f - FMath::SmoothStep(0.f, 1.f, ZBlendAlpha);
    }

    // -----------------------------------------------------------------------
    //  XY radial check
    // -----------------------------------------------------------------------
    const float DistXY = FVector2D::Distance(
        FVector2D(WorldPos.X, WorldPos.Y),
        FVector2D(ActorPos.X, ActorPos.Y));
    const float InnerRadius = FMath::Max(Radius - BlendRadius, 0.f);

    if (DistXY >= Radius) return 0.f; // fully outside radially

    float WeightXY = 1.f;
    if (DistXY > InnerRadius)
    {
        const float XYBlendAlpha = (DistXY - InnerRadius) / FMath::Max(BlendRadius, 1.f);
        WeightXY = 1.f - FMath::SmoothStep(0.f, 1.f, XYBlendAlpha);
    }

    // The most constraining boundary wins.
    return FMath::Min(WeightXY, WeightZ);
}

// ---------------------------------------------------------------------------
//  GetNearestBoundaryPoint (Cylinder)
//  Same as Sphere -- nearest point on the circular XY boundary.
// ---------------------------------------------------------------------------
bool ACylinderWaterRegion::GetNearestBoundaryPoint(
    const FVector2D& QueryXY,
    FVector2D& OutBoundaryPoint,
    FVector2D& OutOutwardNormal) const
{
    const FVector2D Center(GetActorLocation().X, GetActorLocation().Y);
    const FVector2D ToQuery = QueryXY - Center;
    const float Dist = ToQuery.Size();
    if (Dist < 0.01f)
    {
        OutBoundaryPoint = Center + FVector2D(Radius, 0.f);
        OutOutwardNormal = FVector2D(1.f, 0.f);
        return true;
    }
    OutOutwardNormal = ToQuery / Dist;
    OutBoundaryPoint = Center + OutOutwardNormal * Radius;
    return true;
}

// ---------------------------------------------------------------------------
//  BuildFillPreviewMesh
//  Circle at Z=0 local space representing the horizontal footprint.
// ---------------------------------------------------------------------------
#if WITH_EDITOR
void ACylinderWaterRegion::BuildFillPreviewMesh()
{
    if (!ProceduralMeshFill) return;

    ProceduralMeshFill->ClearAllMeshSections();

    const int32 Segments = 32;
    TArray<FVector>    Vertices;
    TArray<int32>      Triangles;
    TArray<FVector>    Normals;
    TArray<FVector2D>  UVs;
    TArray<FColor>     Colors;
    TArray<FProcMeshTangent> Tangents;

    Vertices.Add(FVector::ZeroVector);
    Normals.Add(FVector::UpVector);
    UVs.Add(FVector2D(0.5f, 0.5f));

    for (int32 i = 0; i <= Segments; ++i)
    {
        const float Angle = (float)i / (float)Segments * 2.f * PI;
        Vertices.Add(FVector(FMath::Cos(Angle) * Radius,
            FMath::Sin(Angle) * Radius, 0.f));
        Normals.Add(FVector::UpVector);
        UVs.Add(FVector2D(FMath::Cos(Angle) * 0.5f + 0.5f,
            FMath::Sin(Angle) * 0.5f + 0.5f));
    }

    for (int32 i = 1; i <= Segments; ++i)
    {
        Triangles.Add(0);
        Triangles.Add(i);
        Triangles.Add(i + 1);
    }

    ProceduralMeshFill->CreateMeshSection(0, Vertices, Triangles, Normals,
        UVs, Colors, Tangents, false);
}

void ACylinderWaterRegion::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    RefreshEditorCylinder();
    RefreshEditorComponents();
}

void ACylinderWaterRegion::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);
}

void ACylinderWaterRegion::RefreshEditorCylinder()
{
#if WITH_EDITORONLY_DATA
    if (EditorVisualizerCapsule)
    {
        EditorVisualizerCapsule->SetCapsuleSize(Radius, Height * 0.5f);
        EditorVisualizerCapsule->ShapeColor = EditorBoundsColor;
    }
#endif
}
#endif