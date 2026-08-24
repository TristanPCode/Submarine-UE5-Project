#include "SphereWaterRegion.h"
#include "WaterRegionDataAsset.h"
#include "ProceduralMeshComponent.h"

#if WITH_EDITOR
#include "Components/SphereComponent.h"
#endif

ASphereWaterRegion::ASphereWaterRegion()
{
    PrimaryActorTick.bCanEverTick = false;

#if WITH_EDITOR
    EditorVisualizerSphere = CreateDefaultSubobject<USphereComponent>(TEXT("EditorVisualizerSphere"));
    EditorVisualizerSphere->SetupAttachment(GetRootComponent());
    EditorVisualizerSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    EditorVisualizerSphere->ShapeColor = FColor(0, 180, 255);
    EditorVisualizerSphere->SetSphereRadius(Radius);
    EditorVisualizerSphere->SetHiddenInGame(true); // never visible at runtime
#endif
}

// ---------------------------------------------------------------------------
//  EvaluateWeight
//
//  Distance from query point to actor center vs Radius and BlendRadius.
//
//  Inner zone  [0 .. Radius - BlendRadius]:  weight = 1.0
//  Blend zone  [Radius - BlendRadius .. Radius]: weight = smooth 1..0
//  Outer zone  [Radius .. inf]:              weight = 0.0
// ---------------------------------------------------------------------------
float ASphereWaterRegion::EvaluateWeight(const FVector& WorldPos) const
{
    if (!RegionData) return 0.f;

    const float DistSq = FVector::DistSquared(WorldPos, GetActorLocation());
    const float RadiusSq = Radius * Radius;

    // Fast reject: outside the sphere entirely.
    if (DistSq >= RadiusSq) return 0.f;

    const float Dist = FMath::Sqrt(DistSq);
    const float BlendRadius = FMath::Clamp(RegionData->BlendRadius, 0.f, Radius - 1.f);
    const float InnerRadius = Radius - BlendRadius;

    // Fully inside the inner core.
    if (Dist <= InnerRadius) return 1.f;

    // Within the blend zone: smooth ramp from 1 at InnerRadius to 0 at Radius.
    // Remap Dist from [InnerRadius, Radius] to [0, 1] then invert.
    const float BlendAlpha = (Dist - InnerRadius) / FMath::Max(BlendRadius, 1.f);
    return 1.f - FMath::SmoothStep(0.f, 1.f, BlendAlpha);
}

// ---------------------------------------------------------------------------
//  GetNearestBoundaryPoint (Sphere)
//  The nearest boundary point is simply the point on the sphere's XY circle
//  closest to QueryXY. The outward normal is the radial direction.
// ---------------------------------------------------------------------------
bool ASphereWaterRegion::GetNearestBoundaryPoint(
    const FVector2D& QueryXY,
    FVector2D& OutBoundaryPoint,
    FVector2D& OutOutwardNormal) const
{
    const FVector2D Center(GetActorLocation().X, GetActorLocation().Y);
    const FVector2D ToQuery = QueryXY - Center;
    const float Dist = ToQuery.Size();
    if (Dist < 0.01f)
    {
        // Query is at the center -- pick arbitrary outward direction.
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
//  Generates a circle polygon approximation at Z=0 local space.
// ---------------------------------------------------------------------------
#if WITH_EDITOR
void ASphereWaterRegion::BuildFillPreviewMesh()
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

    // Center vertex.
    Vertices.Add(FVector::ZeroVector);
    Normals.Add(FVector::UpVector);
    UVs.Add(FVector2D(0.5f, 0.5f));

    // Ring vertices.
    for (int32 i = 0; i <= Segments; ++i)
    {
        const float Angle = (float)i / (float)Segments * 2.f * PI;
        const float X = FMath::Cos(Angle) * Radius;
        const float Y = FMath::Sin(Angle) * Radius;
        Vertices.Add(FVector(X, Y, 0.f));
        Normals.Add(FVector::UpVector);
        UVs.Add(FVector2D(FMath::Cos(Angle) * 0.5f + 0.5f,
            FMath::Sin(Angle) * 0.5f + 0.5f));
    }

    // Fan triangles from center.
    for (int32 i = 1; i <= Segments; ++i)
    {
        Triangles.Add(0);
        Triangles.Add(i);
        Triangles.Add(i + 1);
    }

    ProceduralMeshFill->CreateMeshSection(0, Vertices, Triangles, Normals,
        UVs, Colors, Tangents, false);
}

void ASphereWaterRegion::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    RefreshEditorSphere();
    RefreshEditorComponents();
}

void ASphereWaterRegion::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);
}

void ASphereWaterRegion::RefreshEditorSphere()
{
#if WITH_EDITORONLY_DATA
    if (EditorVisualizerSphere)
    {
        EditorVisualizerSphere->SetSphereRadius(Radius);
        EditorVisualizerSphere->ShapeColor = EditorBoundsColor;
    }
#endif
}
#endif