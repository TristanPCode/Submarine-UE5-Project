#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TorpedoCharacteristics.generated.h"

UCLASS(BlueprintType)
class SUBMARINEPROJECT_API UTorpedoCharacteristics : public UDataAsset
{
    GENERATED_BODY()

public:
    // -- Movement -----------------------------------------------------------

    /** Speed of the torpedo in cm/s */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Movement")
    float Speed = 3000.f;

    /** Lifetime in seconds before the torpedo self-destructs */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Movement")
    float Lifetime = 10.f;

    // -- Impact ------------------------------------------------------------

    /**
     * Bounce/push force applied to the submarine on impact (cm/s).
     * Lower than rocks since torpedoes are smaller.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Impact")
    float ImpactBounceForce = 800.f;

    /**
     * How many linear speed states the submarine loses on impact.
     * 1 = drops one state (ForwardMAX -> ForwardMED), 2 = drops two, etc.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Impact",
        meta = (ClampMin = "0", ClampMax = "6"))
    int32 ImpactSpeedStatePenalty = 1;

    // -- Damage ------------------------------------------------------------

    /** Damage dealt to the submarine's health on impact */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Damage")
    float Damage = 100.f;

    /** Radius of explosion damage (0 = no splash damage) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Damage")
    float SplashRadius = 300.f;

    /** Damage falloff at the edge of splash radius (0 = no damage, 1 = full damage) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Torpedo|Damage",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SplashDamageFalloff = 0.2f;
};