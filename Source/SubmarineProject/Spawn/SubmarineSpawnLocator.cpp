// Fill out your copyright notice in the Description page of Project Settings.

#include "Spawn/SubmarineSpawnLocator.h"

#if WITH_EDITORONLY_DATA
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#endif

ASubmarineSpawnLocator::ASubmarineSpawnLocator()
{
    PrimaryActorTick.bCanEverTick = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

#if WITH_EDITORONLY_DATA
    DirectionArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("DirectionArrow"));
    DirectionArrow->SetupAttachment(RootComponent);
    DirectionArrow->ArrowSize = 2.f;
    DirectionArrow->ArrowColor = FColor::Cyan;
    DirectionArrow->bIsScreenSizeScaled = true;

    IconBillboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("IconBillboard"));
    IconBillboard->SetupAttachment(RootComponent);
    IconBillboard->SetRelativeLocation(FVector(0.f, 0.f, 80.f));
    IconBillboard->SetRelativeScale3D(FVector(2.f));
#endif
}

// ---------------------------------------------------------------------------
//  CanAccept
// ---------------------------------------------------------------------------
bool ASubmarineSpawnLocator::CanAccept(ESpawnOccupationType InType) const
{
    if (bOccupied) return false;

    switch (OccupationType)
    {
    case ESpawnOccupationType::PlayerOnly:
        return InType == ESpawnOccupationType::PlayerOnly;
    case ESpawnOccupationType::CPUOnly:
        return InType == ESpawnOccupationType::CPUOnly;
    case ESpawnOccupationType::Any:
    default:
        return true;
    }
}

// ---------------------------------------------------------------------------
//  Editor visualization
// ---------------------------------------------------------------------------
#if WITH_EDITOR
void ASubmarineSpawnLocator::PostEditChangeProperty(
    FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    UpdateEditorVisualization();
}

UTexture2D* ASubmarineSpawnLocator::ResolveBillboardIcon() const
{
    // 1. Per-instance override
    if (BillboardIconOverride) return BillboardIconOverride;

    // 2. Any-team icon
    if (bAnyTeam && AnyTeamIcon) return AnyTeamIcon;

    const int32 Idx = FMath::Clamp(GroupIndex, 0, 15);

    // 3. Type-specific icon by group
    switch (OccupationType)
    {
    case ESpawnOccupationType::PlayerOnly:
        if (PlayerIconsByGroup.IsValidIndex(Idx) && PlayerIconsByGroup[Idx])
            return PlayerIconsByGroup[Idx];
        break;
    case ESpawnOccupationType::CPUOnly:
        if (CPUIconsByGroup.IsValidIndex(Idx) && CPUIconsByGroup[Idx])
            return CPUIconsByGroup[Idx];
        break;
    default: break;
    }

    // 4. Any-type icon by group (fallback)
    if (AnyTypeIconsByGroup.IsValidIndex(Idx) && AnyTypeIconsByGroup[Idx])
        return AnyTypeIconsByGroup[Idx];

    return nullptr;
}

void ASubmarineSpawnLocator::UpdateEditorVisualization()
{
#if WITH_EDITORONLY_DATA
    if (!DirectionArrow) return;

    if (bAnyTeam)
    {
        // White = any team
        DirectionArrow->ArrowColor = FColor::White;
    }
    else
    {
        // Color by group index (0-7 get distinct colors, 8+ stay grey)
        static const FColor GroupColors[] = {
            FColor::Red,    FColor::Blue,   FColor::Green,  FColor::Yellow,
            FColor::Purple, FColor::Orange, FColor(0,255,255), FColor::White
        };
        const int32 ColorIdx = FMath::Clamp(GroupIndex, 0, 7);
        DirectionArrow->ArrowColor = GroupColors[ColorIdx];
    }

    // Arrow size by occupation type
    DirectionArrow->ArrowSize = 2.f;
    switch (OccupationType)
    {
    case ESpawnOccupationType::PlayerOnly: DirectionArrow->ArrowSize = 2.5f; break;
    case ESpawnOccupationType::CPUOnly:    DirectionArrow->ArrowSize = 1.5f; break;
    default: break;
    }

    // Billboard icon
    if (IconBillboard)
    {
        UTexture2D* Icon = ResolveBillboardIcon();
        if (Icon)
            IconBillboard->SetSprite(Icon);
        // If null, leave whatever sprite was set before (or the default)
    }
#endif
}
#endif