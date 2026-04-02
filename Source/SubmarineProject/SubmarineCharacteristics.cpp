// Fill out your copyright notice in the Description page of Project Settings.

#include "SubmarineCharacteristics.h"

USubmarineCharacteristics::USubmarineCharacteristics()
{
    // Populate default linear speed table
    LinearSpeedTable = {
        { ELinearSpeedState::BackwardMAX, -2000.f },
        { ELinearSpeedState::BackwardMED, -1200.f },
        { ELinearSpeedState::BackwardMIN,  -500.f },
        { ELinearSpeedState::Stand,           0.f },
        { ELinearSpeedState::ForwardMIN,    500.f },
        { ELinearSpeedState::ForwardMED,   1200.f },
        { ELinearSpeedState::ForwardMAX,   2000.f },
    };

    // Populate default collision bounce table
    // Landscape: softer bounce, loses 1 state
    CollisionBounceTable.Add({ ESubmarineCollisionType::Landscape,      400.f, 1 });
    // Static obstacles: medium bounce, loses 2 states
    CollisionBounceTable.Add({ ESubmarineCollisionType::StaticObstacle, 800.f, 2 });
    // Other submarines: strong bounce, loses 2 states
    CollisionBounceTable.Add({ ESubmarineCollisionType::OtherSubmarine, 900.f, 2 });
    // Torpedo: force only (damage handled by health system), loses 1 state
    CollisionBounceTable.Add({ ESubmarineCollisionType::Torpedo,        600.f, 1 });
    // Trigger zones: no bounce at all
    CollisionBounceTable.Add({ ESubmarineCollisionType::TriggerZone,      0.f, 0 });
    // Default fallback
    CollisionBounceTable.Add({ ESubmarineCollisionType::Default,        500.f, 1 });
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