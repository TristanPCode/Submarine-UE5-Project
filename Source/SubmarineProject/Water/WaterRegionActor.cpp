#include "WaterRegionActor.h"
#include "OceanSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

#if WITH_EDITOR
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "UObject/ConstructorHelpers.h"
#endif

AWaterRegionActor::AWaterRegionActor()
{
    PrimaryActorTick.bCanEverTick = false;

    // Create an explicit scene root FIRST before any other components.
    // Without this, GetRootComponent() returns null in the constructor,
    // causing all SetupAttachment calls to fail silently -- components
    // end up unattached and stay at world origin (0,0,0) regardless of
    // where the actor is placed. This was the cause of the wireframe and
    // billboard icon not moving when the actor was dragged in the editor.
    USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

#if WITH_EDITORONLY_DATA

    // Wave direction arrow.
    // Orange color (distinct from the blue shape wireframe).
    // Long enough to be clearly readable even on large regions.
    WaveDirectionArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("WaveDirectionArrow"));
    WaveDirectionArrow->SetupAttachment(GetRootComponent());
    WaveDirectionArrow->ArrowColor = FColor(255, 140, 0);
    WaveDirectionArrow->ArrowSize = 8.f;
    WaveDirectionArrow->SetArrowFColor(FColor(255, 140, 0));
    WaveDirectionArrow->bIsScreenSizeScaled = false;
    WaveDirectionArrow->SetHiddenInGame(true);
    WaveDirectionArrow->SetRelativeLocation(FVector(0.f, 0.f, 50.f));

    // Region type icon billboard.
    // Visible in the editor viewport even when the region is not selected.
    RegionIconBillboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("RegionIconBillboard"));
    RegionIconBillboard->SetupAttachment(GetRootComponent());
    RegionIconBillboard->SetHiddenInGame(true);
    RegionIconBillboard->bIsScreenSizeScaled = true;
    RegionIconBillboard->ScreenSize = 0.0025f;

    // ProceduralMeshComponent for the filled region preview.
    // Populated by subclass BuildFillPreviewMesh() calls.
    // Hidden at runtime -- this is ONLY a designer visual aid.
    ProceduralMeshFill = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralMeshFill"));
    ProceduralMeshFill->SetupAttachment(GetRootComponent());
    ProceduralMeshFill->SetHiddenInGame(true);
    ProceduralMeshFill->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ProceduralMeshFill->SetCastShadow(false);
    ProceduralMeshFill->bUseComplexAsSimpleCollision = false;

#endif
}

// ---------------------------------------------------------------------------
//  BeginPlay - self-register with UOceanSubsystem
// ---------------------------------------------------------------------------
void AWaterRegionActor::BeginPlay()
{
    Super::BeginPlay();

    if (!RegionData)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WaterRegionActor] '%s' has no RegionData assigned - region will be ignored."),
            *GetName());
        return;
    }

    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    if (!GI) return;

    UOceanSubsystem* OceanSys = GI->GetSubsystem<UOceanSubsystem>();
    if (OceanSys)
    {
        OceanSys->RegisterRegion(this);
    }
}

// ---------------------------------------------------------------------------
//  EndPlay - self-unregister to avoid stale pointers
// ---------------------------------------------------------------------------
void AWaterRegionActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    if (GI)
    {
        UOceanSubsystem* OceanSys = GI->GetSubsystem<UOceanSubsystem>();
        if (OceanSys)
        {
            OceanSys->UnregisterRegion(this);
        }
    }

    Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
//  Editor
// ---------------------------------------------------------------------------
#if WITH_EDITOR

void AWaterRegionActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    RefreshEditorComponents();
    MarkComponentsRenderStateDirty();
}

void AWaterRegionActor::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);
    // Force all attached components to update their render state when the
    // actor is moved in the viewport. Without this the shape wireframe and
    // fill preview stay at the old position visually even though the actor
    // transform is correct. bFinished = false during drag, true on release --
    // we refresh on both so the preview moves live while dragging.
    MarkComponentsRenderStateDirty();

    if (bFinished)
    {
        // Full refresh on release to sync wave arrow rotation and fill mesh.
        RefreshEditorComponents();
    }
}

void AWaterRegionActor::RefreshEditorComponents()
{
    UpdateWaveDirectionArrow();
    UpdateFillPreview();

#if WITH_EDITORONLY_DATA
    if (RegionIconBillboard && RegionIconTexture)
    {
        RegionIconBillboard->SetSprite(RegionIconTexture);
    }
#endif
}

void AWaterRegionActor::UpdateWaveDirectionArrow()
{
#if WITH_EDITORONLY_DATA
    if (!WaveDirectionArrow) return;

    // WaveDirection is a 2D XY vector. Convert it to a world-space yaw rotation
    // so the arrow points in the dominant wave propagation direction.
    // Normalized before conversion to avoid scaling the arrow length with magnitude.
    if (RegionData)
    {
        const FVector2D Dir2D = RegionData->WaveDirection.GetSafeNormal();
        const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir2D.Y, Dir2D.X));
        WaveDirectionArrow->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
    }

    // Sync arrow color to the region's editor bounds color for visual consistency.
    WaveDirectionArrow->ArrowColor = FColor(255, 140, 0); // always orange for wave
    WaveDirectionArrow->SetHiddenInGame(true);
#endif
}

void AWaterRegionActor::UpdateFillPreview()
{
#if WITH_EDITORONLY_DATA
    if (!ProceduralMeshFill) return;

    if (!bShowFilledPreview)
    {
        // Clear the mesh when preview is toggled off.
        ProceduralMeshFill->ClearAllMeshSections();
        return;
    }

    // Ensure we have a translucent material for the fill.
    EnsureFillPreviewMaterial();

    // Let the subclass populate the mesh geometry.
    BuildFillPreviewMesh();

    // Update opacity from the current property value.
    if (FillPreviewMID)
    {
        FillPreviewMID->SetScalarParameterValue(TEXT("Opacity"), FilledPreviewOpacity);

        // Derive fill color from EditorBoundsColor.
        const FLinearColor FillColor = FLinearColor(EditorBoundsColor) * 0.6f;
        FillPreviewMID->SetVectorParameterValue(TEXT("Color"), FillColor);
    }
#endif
}

void AWaterRegionActor::EnsureFillPreviewMaterial()
{
#if WITH_EDITORONLY_DATA
    if (FillPreviewMID) return;
    if (!ProceduralMeshFill) return;

    // Use UE5's built-in widget material as a base for the translucent fill.
    // This is always available and supports Opacity + Color parameters.
    // Path: /Engine/EngineDebugMaterials/M_SimpleTranslucent
    UMaterial* BaseMat = LoadObject<UMaterial>(nullptr,
        TEXT("/Engine/EngineDebugMaterials/M_SimpleTranslucent"));

    if (!BaseMat)
    {
        // Fallback: use the wireframe material which is always present.
        BaseMat = LoadObject<UMaterial>(nullptr,
            TEXT("/Engine/EngineMaterials/WireframeMaterial"));
    }

    if (BaseMat)
    {
        FillPreviewMID = UMaterialInstanceDynamic::Create(BaseMat, this);
        ProceduralMeshFill->SetMaterial(0, FillPreviewMID);
    }
#endif
}

#endif