// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarineCharacteristics.h"

USubmarineCharacteristics::USubmarineCharacteristics()
{
}

// -----------------------------------------------------------------------------

void USubmarineCharacteristics::PostInitProperties()
{
    Super::PostInitProperties();

    // Remplir uniquement si vide (important pour ne pas écraser les DataAssets)
    if (LinearSpeedTable.Num() == 0)
    {
        LinearSpeedTable = {
            { ELinearSpeedState::BackwardMAX, -2000.f },
            { ELinearSpeedState::BackwardMED, -1200.f },
            { ELinearSpeedState::BackwardMIN,  -500.f },
            { ELinearSpeedState::Stand,           0.f },
            { ELinearSpeedState::ForwardMIN,    500.f },
            { ELinearSpeedState::ForwardMED,   1200.f },
            { ELinearSpeedState::ForwardMAX,   2000.f },
        };
    }

    if (CollisionBounceTable.Num() == 0)
    {
        CollisionBounceTable.Add({ ESubmarineCollisionType::Landscape,      400.f, 0.3f, false, 0.3f, 0.3f, 0.1f, 1 });
        CollisionBounceTable.Add({ ESubmarineCollisionType::StaticObstacle, 800.f, 0.5f, false, 0.5f, 0.5f, 0.2f, 2 });
        CollisionBounceTable.Add({ ESubmarineCollisionType::OtherSubmarine, 900.f, 0.5f, false, 0.5f, 0.5f, 0.2f, 2 });
        CollisionBounceTable.Add({ ESubmarineCollisionType::Torpedo,        600.f, 0.4f, false, 0.4f, 0.4f, 0.1f, 1 });
        CollisionBounceTable.Add({ ESubmarineCollisionType::TriggerZone,      0.f, 0.0f, false, 0.0f, 0.0f, 0.0f, 0 });
        CollisionBounceTable.Add({ ESubmarineCollisionType::Default,        500.f, 0.0f, false, 0.0f, 0.0f, 0.0f, 0 });
    }
}

float USubmarineCharacteristics::GetLinearTargetSpeed(ELinearSpeedState State) const
{
    for (const FLinearSpeedEntry& Entry : LinearSpeedTable)
    {
        if (Entry.State == State)
            return Entry.TargetSpeed;
    }
    return 0.f;
}

int32 USubmarineCharacteristics::GetSafeVerticalStateCount() const
{
    int32 Count = FMath::Max(3, VerticalStateCount);
    // Enforce odd number
    if (Count % 2 == 0)
        Count += 1;
    return Count;
}

float USubmarineCharacteristics::GetPitchForVerticalState(int32 StateIndex) const
{
    const int32 Count = GetSafeVerticalStateCount();
    // Clamp index to valid range
    const int32 SafeIndex = FMath::Clamp(StateIndex, 0, Count - 1);
    const int32 MidIndex = Count / 2;

    if (SafeIndex == MidIndex)
        return 0.f;

    // Map index to [-MaxPitchAngle, +MaxPitchAngle]
    // SafeIndex < MidIndex -> negative pitch (nose down)
    // SafeIndex > MidIndex -> positive pitch (nose up)
    const float Alpha = static_cast<float>(SafeIndex - MidIndex) / static_cast<float>(MidIndex);
    return Alpha * MaxPitchAngle;
}

int32 USubmarineCharacteristics::GetSafeGhostStateCount() const
{
    const int32 SafeTotal = GetSafeVerticalStateCount();
    // Must be even
    int32 Ghost = (GhostVerticalStateCount % 2 != 0)
        ? GhostVerticalStateCount + 1
        : GhostVerticalStateCount;
    // Clamp: at least 0, at most SafeTotal - 3 (keep Max, Stand, Min active)
    Ghost = FMath::Clamp(Ghost, 0, SafeTotal - 3);
    return Ghost;
}

bool USubmarineCharacteristics::IsGhostState(int32 StateIndex) const
{
    const int32 Count = GetSafeVerticalStateCount();
    const int32 MidIndex = Count / 2;
    const int32 GhostHalf = GetSafeGhostStateCount() / 2;
    if (GhostHalf <= 0) return false;

    // Ghost states are the GhostHalf states on each side of the center
    const int32 DistFromCenter = FMath::Abs(StateIndex - MidIndex);
    return (DistFromCenter > 0 && DistFromCenter <= GhostHalf);
}

FCollisionBounceEntry USubmarineCharacteristics::GetCollisionBounce(ESubmarineCollisionType CollisionType) const
{
    // First pass: look for exact type match
    for (const FCollisionBounceEntry& Entry : CollisionBounceTable)
    {
        if (Entry.CollisionType == CollisionType)
            return Entry;
    }

    // Second pass: fallback to Default entry
    for (const FCollisionBounceEntry& Entry : CollisionBounceTable)
    {
        if (Entry.CollisionType == ESubmarineCollisionType::Default)
            return Entry;
    }

    // Last resort: hardcoded fallback
    FCollisionBounceEntry Fallback;
    Fallback.BounceForce = 500.f;
    Fallback.SpeedStatePenalty = 1;
    return Fallback;
}