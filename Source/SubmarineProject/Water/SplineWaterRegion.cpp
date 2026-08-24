#include "SplineWaterRegion.h"
#include "WaterRegionDataAsset.h"
#include "Components/SplineComponent.h"
#include "ProceduralMeshComponent.h"

ASplineWaterRegion::ASplineWaterRegion()
{
    PrimaryActorTick.bCanEverTick = false;

    SplineComponent = CreateDefaultSubobject<USplineComponent>(TEXT("SplineComponent"));
    SplineComponent->SetupAttachment(GetRootComponent());

    // Default to closed loop. The designer should not change this.
    SplineComponent->SetClosedLoop(true);

    // Add a default diamond shape so the region is immediately visible
    // and usable when first placed. Designer replaces these points.
    SplineComponent->ClearSplinePoints(false);
    SplineComponent->AddSplineLocalPoint(FVector(2000.f, 0.f, 0.f));
    SplineComponent->AddSplineLocalPoint(FVector(0.f, 2000.f, 0.f));
    SplineComponent->AddSplineLocalPoint(FVector(-2000.f, 0.f, 0.f));
    SplineComponent->AddSplineLocalPoint(FVector(0.f, -2000.f, 0.f));
    SplineComponent->UpdateSpline();
}

// ---------------------------------------------------------------------------
//  BeginPlay
// ---------------------------------------------------------------------------
void ASplineWaterRegion::BeginPlay()
{
    // Build the polygon cache before Super::BeginPlay() calls RegisterRegion,
    // so the subsystem gets a fully valid region on first registration.
    RebuildPolygonCache();

    Super::BeginPlay();

    if (!SplineComponent->IsClosedLoop())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[SplineWaterRegion] '%s': SplineComponent is not set to ClosedLoop. ")
            TEXT("EvaluateWeight() will always return 0. ")
            TEXT("Enable bClosedLoop on the SplineComponent in the editor."),
            *GetName());
    }
}

// ---------------------------------------------------------------------------
//  RebuildPolygonCache
//  Samples the spline into a flat 2D polygon in actor local space.
//  Stores points as FVector2D (XY only) for fast 2D containment tests.
// ---------------------------------------------------------------------------
void ASplineWaterRegion::RebuildPolygonCache()
{
    bPolygonCacheValid = false;
    PolygonCache.Reset();

    if (!SplineComponent) return;

    const float SplineLength = SplineComponent->GetSplineLength();
    if (SplineLength <= 0.f) return;

    PolygonCache.Reserve(SplineSampleCount);

    for (int32 i = 0; i < SplineSampleCount; ++i)
    {
        const float T = (float)i / (float)SplineSampleCount;
        const float Distance = T * SplineLength;

        // Sample in local space so the polygon is actor-relative.
        // This means the point-in-polygon test only needs to transform
        // the query point into local space, not rebuild the polygon per-query.
        const FVector LocalPoint = SplineComponent->GetLocationAtDistanceAlongSpline(
            Distance, ESplineCoordinateSpace::Local);

        PolygonCache.Add(FVector2D(LocalPoint.X, LocalPoint.Y));
    }

    bPolygonCacheValid = (PolygonCache.Num() >= 3);
}

// ---------------------------------------------------------------------------
//  EvaluateWeight
// ---------------------------------------------------------------------------
float ASplineWaterRegion::EvaluateWeight(const FVector& WorldPos) const
{
    if (!RegionData || !bPolygonCacheValid) return 0.f;
    if (!SplineComponent->IsClosedLoop()) return 0.f;

    // Z axis check: same half-extent logic as box region.
    const FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldPos);
    if (FMath::Abs(LocalPos.Z) > HeightHalfExtent) return 0.f;

    const FVector2D LocalPos2D(LocalPos.X, LocalPos.Y);

    // Point-in-polygon test.
    if (!IsPointInPolygon2D(LocalPos2D)) return 0.f;

    // Inside the polygon. Compute distance to nearest edge for blending.
    const float BlendRadius = FMath::Max(RegionData->BlendRadius, 1.f);
    const float EdgeDist = DistanceToPolygonEdge2D(LocalPos2D);

    if (EdgeDist >= BlendRadius) return 1.f;

    return FMath::SmoothStep(0.f, BlendRadius, EdgeDist);
}

// ---------------------------------------------------------------------------
//  IsPointInPolygon2D
//  Ray casting algorithm (Jordan curve theorem).
//  Casts a ray in the +X direction from Point and counts edge crossings.
//  Odd count = inside, even count = outside.
// ---------------------------------------------------------------------------
bool ASplineWaterRegion::IsPointInPolygon2D(const FVector2D& Point) const
{
    const int32 N = PolygonCache.Num();
    if (N < 3) return false;

    bool bInside = false;
    int32 j = N - 1;

    for (int32 i = 0; i < N; ++i)
    {
        const FVector2D& Pi = PolygonCache[i];
        const FVector2D& Pj = PolygonCache[j];

        // Check if the horizontal ray from Point crosses edge (Pj, Pi).
        if (((Pi.Y > Point.Y) != (Pj.Y > Point.Y)) &&
            (Point.X < (Pj.X - Pi.X) * (Point.Y - Pi.Y) / (Pj.Y - Pi.Y) + Pi.X))
        {
            bInside = !bInside;
        }

        j = i;
    }

    return bInside;
}

// ---------------------------------------------------------------------------
//  DistanceToPolygonEdge2D
//  Returns the minimum distance from Point to any edge of the polygon.
//  Used for BlendRadius feathering. O(N) over the polygon cache.
// ---------------------------------------------------------------------------
float ASplineWaterRegion::DistanceToPolygonEdge2D(const FVector2D& Point) const
{
    const int32 N = PolygonCache.Num();
    float MinDistSq = FLT_MAX;

    int32 j = N - 1;
    for (int32 i = 0; i < N; ++i)
    {
        const FVector2D& A = PolygonCache[j];
        const FVector2D& B = PolygonCache[i];

        // Project Point onto segment AB, clamped to [0,1].
        const FVector2D AB = B - A;
        const float LenSq = AB.SizeSquared();

        float DistSq;
        if (LenSq < SMALL_NUMBER)
        {
            // Degenerate edge (A == B): distance to point A.
            DistSq = FVector2D::DistSquared(Point, A);
        }
        else
        {
            const float T = FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LenSq, 0.f, 1.f);
            const FVector2D Closest = A + T * AB;
            DistSq = FVector2D::DistSquared(Point, Closest);
        }

        MinDistSq = FMath::Min(MinDistSq, DistSq);
        j = i;
    }

    return FMath::Sqrt(MinDistSq);
}

// ---------------------------------------------------------------------------
//  GetNearestBoundaryPoint (Spline polygon)
//  Finds the nearest point on the polygon edge to QueryXY.
//  Uses PolygonCache (actor local space) + actor transform.
// ---------------------------------------------------------------------------
bool ASplineWaterRegion::GetNearestBoundaryPoint(
    const FVector2D& QueryXY,
    FVector2D& OutBoundaryPoint,
    FVector2D& OutOutwardNormal) const
{
    if (!bPolygonCacheValid || PolygonCache.Num() < 3) return false;

    const FTransform ActorTransform = GetActorTransform();
    const FVector LocalPos3D = ActorTransform.InverseTransformPosition(
        FVector(QueryXY.X, QueryXY.Y, GetActorLocation().Z));
    const FVector2D LocalQuery(LocalPos3D.X, LocalPos3D.Y);

    const int32 N = PolygonCache.Num();
    float BestDistSq = FLT_MAX;
    FVector2D BestPoint = PolygonCache[0];
    FVector2D BestEdgeDir = FVector2D(1.f, 0.f);

    for (int32 i = 0, j = N - 1; i < N; j = i++)
    {
        const FVector2D& Pi = PolygonCache[i];
        const FVector2D& Pj = PolygonCache[j];
        const FVector2D Edge = Pi - Pj;
        const float EdgeLenSq = Edge.SizeSquared();
        if (EdgeLenSq < 0.001f) continue;

        const float T = FMath::Clamp(
            FVector2D::DotProduct(LocalQuery - Pj, Edge) / EdgeLenSq,
            0.f, 1.f);
        const FVector2D Closest = Pj + T * Edge;
        const float DistSq = (LocalQuery - Closest).SizeSquared();

        if (DistSq < BestDistSq)
        {
            BestDistSq = DistSq;
            BestPoint = Closest;
            BestEdgeDir = Edge.GetSafeNormal();
        }
    }

    const FVector WorldPoint3D = ActorTransform.TransformPosition(
        FVector(BestPoint.X, BestPoint.Y, 0.f));
    OutBoundaryPoint = FVector2D(WorldPoint3D.X, WorldPoint3D.Y);

    FVector2D CandidateNormal(-BestEdgeDir.Y, BestEdgeDir.X);
    FVector2D Centroid = FVector2D::ZeroVector;
    for (const FVector2D& P : PolygonCache) Centroid += P;
    Centroid /= (float)N;
    if (FVector2D::DotProduct(CandidateNormal, BestPoint - Centroid) < 0.f)
        CandidateNormal = -CandidateNormal;

    const FVector WorldNormal3D = ActorTransform.TransformVector(
        FVector(CandidateNormal.X, CandidateNormal.Y, 0.f));
    OutOutwardNormal = FVector2D(WorldNormal3D.X, WorldNormal3D.Y).GetSafeNormal();
    return true;
}

// ---------------------------------------------------------------------------
//  BuildFillPreviewMesh
//  Fan triangulation of the polygon cache around its centroid.
// ---------------------------------------------------------------------------
#if WITH_EDITOR
void ASplineWaterRegion::BuildFillPreviewMesh()
{
    if (!ProceduralMeshFill || !bPolygonCacheValid) return;

    ProceduralMeshFill->ClearAllMeshSections();

    TArray<FVector>    Vertices;
    TArray<int32>      Triangles;
    TArray<FVector>    Normals;
    TArray<FVector2D>  UVs;
    TArray<FColor>     Colors;
    TArray<FProcMeshTangent> Tangents;

    // Compute centroid of the polygon as the fan center.
    FVector2D Centroid = FVector2D::ZeroVector;
    for (const FVector2D& P : PolygonCache)
    {
        Centroid += P;
    }
    Centroid /= (float)PolygonCache.Num();

    // Center vertex.
    Vertices.Add(FVector(Centroid.X, Centroid.Y, 0.f));
    Normals.Add(FVector::UpVector);
    UVs.Add(FVector2D(0.5f, 0.5f));

    // Ring vertices from polygon cache.
    for (const FVector2D& P : PolygonCache)
    {
        Vertices.Add(FVector(P.X, P.Y, 0.f));
        Normals.Add(FVector::UpVector);
        UVs.Add(FVector2D(P.X / 2000.f + 0.5f, P.Y / 2000.f + 0.5f));
    }

    // Fan triangles.
    const int32 N = PolygonCache.Num();
    for (int32 i = 1; i <= N; ++i)
    {
        Triangles.Add(0);
        Triangles.Add(i);
        Triangles.Add((i % N) + 1);
    }

    ProceduralMeshFill->CreateMeshSection(0, Vertices, Triangles, Normals,
        UVs, Colors, Tangents, false);
}

void ASplineWaterRegion::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    RebuildPolygonCache();
    RefreshEditorComponents();
}

void ASplineWaterRegion::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);
    if (bFinished)
    {
        RebuildPolygonCache();
        RefreshEditorComponents();
    }
}
#endif