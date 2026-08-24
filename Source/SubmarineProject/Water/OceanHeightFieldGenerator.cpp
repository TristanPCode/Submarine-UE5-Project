#include "OceanHeightFieldGenerator.h"
#include "OceanSubsystem.h"
#include "OceanSurfaceActor.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "RenderingThread.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"

UOceanHeightFieldGenerator::UOceanHeightFieldGenerator()
{
    PrimaryComponentTick.bCanEverTick = false;
}

// ---------------------------------------------------------------------------
//  BeginPlay
//  Defers the actual build by one tick so all WaterRegionActors have had
//  time to register with OceanSubsystem in their own BeginPlay calls.
// ---------------------------------------------------------------------------
void UOceanHeightFieldGenerator::BeginPlay()
{
    Super::BeginPlay();

    UWorld* World = GetWorld();
    UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
    if (!GI)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[OceanHeightField] '%s': No GameInstance found. Cannot build height field."),
            *GetOwner()->GetName());
        return;
    }

    OceanSubsystem = GI->GetSubsystem<UOceanSubsystem>();
    if (!OceanSubsystem.IsValid())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[OceanHeightField] '%s': OceanSubsystem not found."),
            *GetOwner()->GetName());
        return;
    }

    // Bind to the region changed delegate so we rebuild if the map changes.
    RegionChangedHandle = OceanSubsystem->OnRegionChanged.AddUObject(
        this, &UOceanHeightFieldGenerator::OnRegionChanged);

    // Defer one tick: WaterRegionActors call RegisterRegion in their
    // BeginPlay. If we build immediately we may miss regions that haven't
    // registered yet. A 0.0s timer fires at the start of the next tick.
    World->GetTimerManager().SetTimer(
        DeferredBuildTimer,
        this, &UOceanHeightFieldGenerator::RebuildHeightField,
        0.05f,  // 50ms -- safe margin for all region actors to register
        false);
}

// ---------------------------------------------------------------------------
//  EndPlay
// ---------------------------------------------------------------------------
void UOceanHeightFieldGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (OceanSubsystem.IsValid())
    {
        OceanSubsystem->OnRegionChanged.Remove(RegionChangedHandle);
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(DeferredBuildTimer);
    }

    Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
//  RebuildHeightField
//  Public entry point. Also called by the deferred timer and OnRegionChanged.
// ---------------------------------------------------------------------------
void UOceanHeightFieldGenerator::RebuildHeightField()
{
    UOceanSubsystem* OceanSys = OceanSubsystem.Get();
    if (!OceanSys)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[OceanHeightField] '%s': OceanSubsystem is null during rebuild."),
            *GetOwner()->GetName());
        return;
    }

    BuildHeightFieldTexture(OceanSys);
    ExpandMeshBounds();
    PushTextureToMaterials();
}

// ---------------------------------------------------------------------------
//  BuildHeightFieldTexture
//
//  Iterates a HeightFieldResolution x HeightFieldResolution grid.
//  For each cell (ix, iy), the world XY position is:
//      WorldX = MapMin.X + (ix + 0.5) / Resolution * (MapMax.X - MapMin.X)
//      WorldY = MapMin.Y + (iy + 0.5) / Resolution * (MapMax.Y - MapMin.Y)
//
//  The Z for the query is the default region's BaseWaterHeight. This gives
//  a surface-level XY query. Because EvaluateWeight uses the full 3D XYZ
//  for volumetric regions, we use the default surface Z as a neutral probe.
//  This correctly captures the surface-layer region blending because:
//    - LayeredSplineWaterRegion clamps to outer layers for Z outside range,
//      returning full weight for the surface layer at surface Z.
//    - Box/sphere regions that don't reach the surface return 0 weight.
//  So the height field correctly represents what the surface sees.
//
//  Output format: UTexture2D, PF_R16F (half-float), 1 channel.
//  Each texel stores the blended water height at that map position.
// ---------------------------------------------------------------------------
void UOceanHeightFieldGenerator::BuildHeightFieldTexture(UOceanSubsystem* OceanSys)
{
    const int32 Res = FMath::Clamp(HeightFieldResolution, 64, 1024);
    const FVector2D MapSize = MapMax - MapMin;

    if (MapSize.X <= 0.f || MapSize.Y <= 0.f)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[OceanHeightField] '%s': MapMax must be greater than MapMin. "
                "Check MapMin/MapMax on the OceanSurfaceActor."),
            *GetOwner()->GetName());
        return;
    }

    // The default surface Z for probing: use the default region height.
    // This gives us a neutral Z that the surface evaluation layers will
    // treat as "at the surface" -- outer layer clamping returns weight=1.
    const float DefaultSurfaceZ = OceanSys->GetDefaultWaterHeight();

    // Allocate pixel data. PF_R16F = 2 bytes per pixel.
    const int32 PixelCount = Res * Res;
    TArray<FFloat16> HeightData;
    HeightData.SetNumUninitialized(PixelCount);

    double StartTime = FPlatformTime::Seconds();

    for (int32 iy = 0; iy < Res; ++iy)
    {
        const float V = (iy + 0.5f) / (float)Res;
        const float WorldY = MapMin.Y + V * MapSize.Y;

        for (int32 ix = 0; ix < Res; ++ix)
        {
            const float U = (ix + 0.5f) / (float)Res;
            const float WorldX = MapMin.X + U * MapSize.X;

            // Query the authoritative CPU subsystem at this surface XY.
            const FVector QueryPos(WorldX, WorldY, DefaultSurfaceZ);
            const float Height = OceanSys->GetWaterHeightAtPosition(QueryPos);

            HeightData[iy * Res + ix] = FFloat16(Height);
        }
    }

    // Retain a CPU-side float copy for validation readback.
    CPUHeightData.SetNumUninitialized(PixelCount);
    for (int32 i = 0; i < PixelCount; ++i)
    {
        CPUHeightData[i] = HeightData[i].GetFloat();
    }
    CPUHeightRes = Res;

    double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
    UE_LOG(LogTemp, Log,
        TEXT("[OceanHeightField] '%s': Built %dx%d height field in %.2f ms. "
            "Coverage: (%.0f,%.0f) to (%.0f,%.0f). DefaultSurfaceZ=%.1f"),
        *GetOwner()->GetName(), Res, Res, ElapsedMs,
        MapMin.X, MapMin.Y, MapMax.X, MapMax.Y, DefaultSurfaceZ);

    // Create or recreate the texture.
    // We use TRANSIENT and no mipmaps -- this texture is rebuilt at runtime,
    // never saved to disk, and doesn't need a mip chain.
    HeightFieldTexture = UTexture2D::CreateTransient(Res, Res, PF_R16F,
        TEXT("T_OceanHeightField"));

    if (!HeightFieldTexture)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[OceanHeightField] '%s': Failed to create transient UTexture2D."),
            *GetOwner()->GetName());
        return;
    }

    HeightFieldTexture->Filter = TF_Bilinear;
    HeightFieldTexture->AddressX = TA_Clamp;
    HeightFieldTexture->AddressY = TA_Clamp;
    HeightFieldTexture->SRGB = false;
    HeightFieldTexture->CompressionSettings = TC_HDR;

    // Upload pixel data to the texture.
    {
        FTexture2DMipMap& Mip = HeightFieldTexture->GetPlatformData()->Mips[0];
        void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(MipData, HeightData.GetData(),
            PixelCount * sizeof(FFloat16));
        Mip.BulkData.Unlock();
    }

    HeightFieldTexture->UpdateResource();

    UE_LOG(LogTemp, Log,
        TEXT("[OceanHeightField] '%s': Texture uploaded to GPU. Size: %d KB."),
        *GetOwner()->GetName(),
        (PixelCount * sizeof(FFloat16)) / 1024);
}

// ---------------------------------------------------------------------------
//  PushTextureToMaterials
//  Passes the height field texture and map bounds to all surface MIDs
//  on the owning AOceanSurfaceActor.
// ---------------------------------------------------------------------------
void UOceanHeightFieldGenerator::PushTextureToMaterials()
{
    if (!HeightFieldTexture) return;

    AOceanSurfaceActor* SurfaceActor = Cast<AOceanSurfaceActor>(GetOwner());
    if (!SurfaceActor) return;

    // Push to the surface MID.
    if (UMaterialInstanceDynamic* SurfaceMID = SurfaceActor->GetSurfaceMID())
    {
        SurfaceMID->SetTextureParameterValue(HeightFieldTextureParamName,
            HeightFieldTexture);

        // Pass map bounds as vector parameters so the material can compute
        // the correct UV from world XY:
        //   UV = (WorldXY - MapMin) / (MapMax - MapMin)
        SurfaceMID->SetVectorParameterValue(MapMinParamName,
            FLinearColor(MapMin.X, MapMin.Y, 0.f, 0.f));
        SurfaceMID->SetVectorParameterValue(MapMaxParamName,
            FLinearColor(MapMax.X, MapMax.Y, 0.f, 0.f));

        UE_LOG(LogTemp, Log,
            TEXT("[OceanHeightField] '%s': Height field texture pushed to SurfaceMID."),
            *GetOwner()->GetName());
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[OceanHeightField] '%s': SurfaceMID is null. "
                "Ensure M_Ocean_Surface is assigned on the OceanSurfaceActor "
                "BEFORE BeginPlay runs."),
            *GetOwner()->GetName());
    }

    // Also push to the underside MID if it has the same parameters.
    // The underside material typically doesn't need WPO height data,
    // but we push it anyway in case the material references it.
    if (UMaterialInstanceDynamic* UndersideMID = SurfaceActor->GetUndersideMID())
    {
        // Only set if the parameter exists -- SetTextureParameterValue is
        // silently ignored for parameters that don't exist in the material.
        UndersideMID->SetTextureParameterValue(HeightFieldTextureParamName,
            HeightFieldTexture);
        UndersideMID->SetVectorParameterValue(MapMinParamName,
            FLinearColor(MapMin.X, MapMin.Y, 0.f, 0.f));
        UndersideMID->SetVectorParameterValue(MapMaxParamName,
            FLinearColor(MapMax.X, MapMax.Y, 0.f, 0.f));
    }
}

// ---------------------------------------------------------------------------
//  ExpandMeshBounds
//  Expands the SurfaceMeshComponent's bounds by MaxExpectedHeightDisplacement
//  so WPO-displaced vertices are not incorrectly culled by the GPU.
//  Must be called after ApplyMeshScale (which sets the mesh on the component).
// ---------------------------------------------------------------------------
void UOceanHeightFieldGenerator::ExpandMeshBounds()
{
    AOceanSurfaceActor* SurfaceActor = Cast<AOceanSurfaceActor>(GetOwner());
    if (!SurfaceActor) return;

    // We expand both the surface and underside mesh components.
    TArray<UStaticMeshComponent*> MeshComps;
    SurfaceActor->GetComponents<UStaticMeshComponent>(MeshComps);

    for (UStaticMeshComponent* Comp : MeshComps)
    {
        if (!Comp) continue;

        // SetBoundsScale multiplies the component's existing bounds sphere
        // radius uniformly. The flat ocean plane has a very small Z extent
        // (typically 1 UU), so its sphere radius is dominated by its XY
        // half-extent. We compute the scale factor needed so that the
        // expanded sphere radius covers MaxExpectedHeightDisplacement in Z.
        //
        // Let R = current bounds sphere radius (approx = XY half-extent).
        // We want the scaled radius to be at least
        //   sqrt(R^2 + MaxExpectedHeightDisplacement^2).
        // Scale = sqrt(R^2 + D^2) / R
        //
        // This is conservative: it over-expands slightly in XY, which is
        // harmless. It guarantees the WPO-displaced vertices are never
        // incorrectly culled by the GPU frustum/occlusion system.
        const FBoxSphereBounds CurrentBounds = Comp->CalcLocalBounds();
        const float R = FMath::Max(CurrentBounds.SphereRadius, 1.f);
        const float D = MaxExpectedHeightDisplacement;
        const float NewRadius = FMath::Sqrt(R * R + D * D);
        const float Scale = NewRadius / R;

        Comp->bUseAttachParentBound = false;
        Comp->SetBoundsScale(Scale);

        UE_LOG(LogTemp, Log,
            TEXT("[OceanHeightField] '%s': SetBoundsScale(%.2f) on '%s'. "
                "Original sphere radius=%.0f, expanded to %.0f UU "
                "(MaxExpectedHeightDisplacement=%.0f)."),
            *GetOwner()->GetName(), Scale, *Comp->GetName(),
            R, NewRadius, D);
    }
}

// ---------------------------------------------------------------------------
//  OnRegionChanged
//  Called when OceanSubsystem fires OnRegionChanged (region added/removed
//  or runtime storm event). Triggers a full rebuild.
//  This is intentionally synchronous -- rebuilds are fast (~1ms) and
//  infrequent. An async rebuild would require double-buffering the texture,
//  adding significant complexity for negligible benefit.
// ---------------------------------------------------------------------------
void UOceanHeightFieldGenerator::OnRegionChanged()
{
    UE_LOG(LogTemp, Log,
        TEXT("[OceanHeightField] '%s': Region change detected. Rebuilding height field."),
        *GetOwner()->GetName());

    RebuildHeightField();
}

// ---------------------------------------------------------------------------
//  SampleHeightFieldBilinear
//  Mirrors the GPU bilinear texture sample exactly:
//    UV = (WorldXY - MapMin) / (MapMax - MapMin), clamped to [0,1]
//    then bilinear interpolation between the 4 nearest texels.
//  Texel centers are at (ix+0.5)/Res -- the same convention used in the bake.
// ---------------------------------------------------------------------------
bool UOceanHeightFieldGenerator::SampleHeightFieldBilinear(
    const FVector2D& WorldXY, float& OutHeight) const
{
    if (CPUHeightData.Num() == 0 || CPUHeightRes <= 0) return false;

    const int32 Res = CPUHeightRes;
    const FVector2D MapSize = MapMax - MapMin;
    if (MapSize.X <= 0.f || MapSize.Y <= 0.f) return false;

    // World -> UV [0,1], clamped (matches TA_Clamp).
    float U = (WorldXY.X - MapMin.X) / MapSize.X;
    float V = (WorldXY.Y - MapMin.Y) / MapSize.Y;
    U = FMath::Clamp(U, 0.f, 1.f);
    V = FMath::Clamp(V, 0.f, 1.f);

    // UV -> texel space. Texel centers at (i+0.5)/Res, so subtract 0.5.
    const float Fx = U * Res - 0.5f;
    const float Fy = V * Res - 0.5f;

    const int32 X0 = FMath::Clamp(FMath::FloorToInt(Fx), 0, Res - 1);
    const int32 Y0 = FMath::Clamp(FMath::FloorToInt(Fy), 0, Res - 1);
    const int32 X1 = FMath::Clamp(X0 + 1, 0, Res - 1);
    const int32 Y1 = FMath::Clamp(Y0 + 1, 0, Res - 1);

    const float Tx = FMath::Clamp(Fx - X0, 0.f, 1.f);
    const float Ty = FMath::Clamp(Fy - Y0, 0.f, 1.f);

    const float H00 = CPUHeightData[Y0 * Res + X0];
    const float H10 = CPUHeightData[Y0 * Res + X1];
    const float H01 = CPUHeightData[Y1 * Res + X0];
    const float H11 = CPUHeightData[Y1 * Res + X1];

    const float Top = FMath::Lerp(H00, H10, Tx);
    const float Bot = FMath::Lerp(H01, H11, Tx);
    OutHeight = FMath::Lerp(Top, Bot, Ty);
    return true;
}