#include "BoxWaterRegion.h"
#include "WaterRegionDataAsset.h"
#include "ProceduralMeshComponent.h"

#if WITH_EDITOR
#include "Components/BoxComponent.h"
#endif

ABoxWaterRegion::ABoxWaterRegion()
{
    PrimaryActorTick.bCanEverTick = false;

#if WITH_EDITOR
    // Create a box component used only for editor visualization.
    // No collision, no physics - purely a visual aid for region placement.
    EditorVisualizerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("EditorVisualizerBox"));
    EditorVisualizerBox->SetupAttachment(GetRootComponent());
    EditorVisualizerBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    EditorVisualizerBox->SetLineThickness(WireframeThickness);
    EditorVisualizerBox->ShapeColor = FColor(0, 180, 255);
    EditorVisualizerBox->SetBoxExtent(BoxHalfExtent);
    EditorVisualizerBox->SetHiddenInGame(true); // never visible at runtime
#endif
}

// ---------------------------------------------------------------------------
//  EvaluateWeight
//
//  For a position P and a box centered at actor origin with half-extent E:
//
//  1. Transform P into local space relative to the actor.
//  2. Compute per-axis signed distances from the box surface.
//     Negative = inside on that axis, positive = outside.
//  3. The minimum signed distance across all axes tells us:
//     - How deep inside the box we are (negative value = margin from edge)
//     - Whether we are outside (positive value on any axis)
//  4. Blend using BlendRadius from the DataAsset.
// ---------------------------------------------------------------------------
float ABoxWaterRegion::EvaluateWeight(const FVector& WorldPos) const
{
    if (!RegionData) return 0.f;

    // Transform world position to actor local space.
    // Using InverseTransformPosition handles actor translation correctly.
    // The box is always axis-aligned so we ignore rotation (see header note).
    const FVector LocalPos = GetActorTransform().InverseTransformPosition(WorldPos);

    // Per-axis: how far outside the box is the position?
    // Positive = outside that face, negative = inside.
    const float DX = FMath::Abs(LocalPos.X) - BoxHalfExtent.X;
    const float DY = FMath::Abs(LocalPos.Y) - BoxHalfExtent.Y;
    const float DZ = FMath::Abs(LocalPos.Z) - BoxHalfExtent.Z;

    // If any axis is positive the point is outside the box entirely.
    if (DX > 0.f || DY > 0.f || DZ > 0.f) return 0.f;

    // Point is inside. Compute margin: how far from the nearest face.
    // All values are negative here; the least negative = nearest face.
    const float MarginFromEdge = -FMath::Max3(DX, DY, DZ);

    const float BlendRadius = FMath::Max(RegionData->BlendRadius, 1.f);

    // If we are deeper than BlendRadius from any edge: full weight.
    if (MarginFromEdge >= BlendRadius) return 1.f;

    // Within the blend zone: smooth ramp from 0 at the edge to 1 at BlendRadius.
    return FMath::SmoothStep(0.f, BlendRadius, MarginFromEdge);
}

// ---------------------------------------------------------------------------
//  Editor
// ---------------------------------------------------------------------------
#if WITH_EDITOR
void ABoxWaterRegion::BuildFillPreviewMesh()
{
    if (!ProceduralMeshFill) return;

    ProceduralMeshFill->ClearAllMeshSections();

    const float X = BoxHalfExtent.X;
    const float Y = BoxHalfExtent.Y;

    // Four corners of the XY footprint at Z=0 local space.
    TArray<FVector> Vertices = {
        FVector(-X, -Y, 0.f),
        FVector(X, -Y, 0.f),
        FVector(X,  Y, 0.f),
        FVector(-X,  Y, 0.f)
    };

    // Two triangles forming the quad.
    TArray<int32> Triangles = { 0, 1, 2,  0, 2, 3 };

    TArray<FVector> Normals = { FVector::UpVector, FVector::UpVector,
                                   FVector::UpVector, FVector::UpVector };
    TArray<FVector2D> UVs = { {0,0}, {1,0}, {1,1}, {0,1} };
    TArray<FColor> Colors;
    TArray<FProcMeshTangent> Tangents;

    ProceduralMeshFill->CreateMeshSection(0, Vertices, Triangles, Normals,
        UVs, Colors, Tangents, false);
}

void ABoxWaterRegion::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    RefreshEditorBox();
    RefreshEditorComponents();
}

void ABoxWaterRegion::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);
}

void ABoxWaterRegion::RefreshEditorBox()
{
#if WITH_EDITORONLY_DATA
    if (EditorVisualizerBox)
    {
        EditorVisualizerBox->SetBoxExtent(BoxHalfExtent);
        EditorVisualizerBox->ShapeColor = EditorBoundsColor;
        EditorVisualizerBox->SetLineThickness(WireframeThickness);
    }
#endif
}
#endif