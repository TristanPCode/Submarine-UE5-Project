// Fill out your copyright notice in the Description page of Project Settings.

#include "TorpedoPawn.h"
#include "TorpedoCharacteristics.h"
#include "TorpedoPhysicsComponent.h"
#include "SubmarineCollisionComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"

ATorpedoPawn::ATorpedoPawn()
{
    PrimaryActorTick.bCanEverTick = true;

    TorpedoBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TorpedoBody"));
    RootComponent = TorpedoBody;
    // Generate hit events so we can react to collisions
    TorpedoBody->SetNotifyRigidBodyCollision(true);
    TorpedoBody->SetGenerateOverlapEvents(false);

    // POV camera — attached to root, inherits torpedo rotation
    POVCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraPOV"));
    POVCamera->SetupAttachment(RootComponent);
    POVCamera->bUsePawnControlRotation = false;

    // 3rd person — detached at BeginPlay, positioned manually each tick
    ThirdPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraThirdPerson"));
    ThirdPersonCamera->SetupAttachment(RootComponent);
    ThirdPersonCamera->bUsePawnControlRotation = false;
    ThirdPersonCamera->SetAutoActivate(false);

    PhysicsHandler = CreateDefaultSubobject<UTorpedoPhysicsComponent>(TEXT("PhysicsHandler"));

    // Default tag so submarines can identify incoming torpedoes
    Tags.Add(FName("Torpedo"));

    // Torpedoes don't use controller rotation
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;
}

// -----------------------------------------------------------------------------
//  BeginPlay
// -----------------------------------------------------------------------------
void ATorpedoPawn::BeginPlay()
{
    Super::BeginPlay();

    // Detach 3rd person camera so it doesn't inherit torpedo rotation
    ThirdPersonCamera->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

    // Bind hit delegate
    TorpedoBody->OnComponentHit.AddDynamic(this, &ATorpedoPawn::OnTorpedoHit);

    // Initialise camera offsets if Characteristics is already set
    if (const UTorpedoCharacteristics* Stats = GetStats())
    {
        POVCamera->SetRelativeLocation(Stats->POVCameraOffset);
        ThirdPersonOrbitYaw = Stats->ThirdPersonInitialYaw;
        ThirdPersonOrbitPitch = Stats->ThirdPersonInitialPitch;
        ThirdPersonRadius = Stats->ThirdPersonInitialRadius;
    }

    // Start in POV
    ActivatePOVCamera();
}

// -----------------------------------------------------------------------------
//  Setup (called by SubmarineTorpedoComponent)
// -----------------------------------------------------------------------------
void ATorpedoPawn::SetCharacteristics(UTorpedoCharacteristics* InCharacteristics)
{
    Characteristics = InCharacteristics;

    if (PhysicsHandler)
        PhysicsHandler->Characteristics = Characteristics;
}

void ATorpedoPawn::SetInitialVelocity(const FVector& WorldVelocity)
{
    if (PhysicsHandler)
        PhysicsHandler->SetInitialVelocity(WorldVelocity);
}

// -----------------------------------------------------------------------------
//  Tick
// -----------------------------------------------------------------------------
void ATorpedoPawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (bExploded) return;

    const UTorpedoCharacteristics* Stats = GetStats();

    // -- Lifetime check ----------------------------------------------------
    LifetimeElapsed += DeltaTime;
    if (Stats && LifetimeElapsed >= Stats->MaxLifetime)
    {
        OnExpired.Broadcast();
        Explode(nullptr, GetActorLocation());
        return;
    }

    // -- Apply physics velocity --------------------------------------------
    if (PhysicsHandler)
    {
        const FVector MoveDelta = PhysicsHandler->PhysicsVelocity * DeltaTime;
        FHitResult Hit;
        AddActorWorldOffset(MoveDelta, true, &Hit);

        if (Hit.IsValidBlockingHit() && Hit.GetActor())
        {
            OnTorpedoHit(TorpedoBody, Hit.GetActor(),
                Hit.GetComponent(), FVector::ZeroVector, Hit);
        }
    }

    // -- Update rotation to match velocity direction -----------------------
    if (PhysicsHandler && !PhysicsHandler->PhysicsVelocity.IsNearlyZero(1.f))
    {
        const FRotator TargetRot = PhysicsHandler->PhysicsVelocity.Rotation();
        SetActorRotation(FMath::RInterpTo(GetActorRotation(), TargetRot, DeltaTime, 8.f));
    }

    // -- Camera -----------------------------------------------------------
    if (ThirdPersonCamera->IsActive())
        TickThirdPersonCamera();
}

// -----------------------------------------------------------------------------
//  Input (minimal — torpedo cameras can be mouse-driven if possessed)
// -----------------------------------------------------------------------------
void ATorpedoPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    // Camera control bindings would be added here if this pawn is ever possessed
}

// -----------------------------------------------------------------------------
//  Camera
// -----------------------------------------------------------------------------
void ATorpedoPawn::ActivatePOVCamera()
{
    POVCamera->SetActive(true);
    ThirdPersonCamera->SetActive(false);

    if (const UTorpedoCharacteristics* Stats = GetStats())
        POVCamera->SetRelativeLocation(Stats->POVCameraOffset);
}

void ATorpedoPawn::ActivateThirdPersonCamera()
{
    POVCamera->SetActive(false);
    ThirdPersonCamera->SetActive(true);
}

void ATorpedoPawn::TickThirdPersonCamera()
{
    const UTorpedoCharacteristics* Stats = GetStats();
    if (!Stats) return;

    const FVector Pivot = GetActorLocation() +
        FVector(0.f, 0.f, Stats->ThirdPersonPivotOffsetZ);

    const float YawRad = FMath::DegreesToRadians(ThirdPersonOrbitYaw);
    const float PitchRad = FMath::DegreesToRadians(ThirdPersonOrbitPitch);

    const FVector Offset(
        ThirdPersonRadius * FMath::Cos(PitchRad) * FMath::Cos(YawRad),
        ThirdPersonRadius * FMath::Cos(PitchRad) * FMath::Sin(YawRad),
        ThirdPersonRadius * FMath::Sin(PitchRad)
    );

    const FVector CamPos = Pivot + Offset;
    ThirdPersonCamera->SetWorldLocation(CamPos);
    ThirdPersonCamera->SetWorldRotation((Pivot - CamPos).Rotation());
}

// -----------------------------------------------------------------------------
//  Hit detection
// -----------------------------------------------------------------------------
void ATorpedoPawn::OnTorpedoHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
    if (bExploded) return;
    if (!OtherActor || OtherActor == this) return;

    // Don't detonate on the submarine that fired us
    if (OtherActor == FiringShooter) return;

    Explode(OtherActor, Hit.ImpactPoint);
}

// -----------------------------------------------------------------------------
//  Explode — override in Blueprint for VFX/SFX
// -----------------------------------------------------------------------------
void ATorpedoPawn::Explode_Implementation(AActor* HitActor, const FVector& ImpactLocation)
{
    if (bExploded) return;
    bExploded = true;

    const UTorpedoCharacteristics* Stats = GetStats();

    // Notify listeners
    OnImpact.Broadcast(HitActor, ImpactLocation);

    // Apply damage + collision response to direct hit target
    if (HitActor && Stats)
    {
        // Try to reach the submarine's collision component for proper damage handling
        if (USubmarineCollisionComponent* ColComp =
            HitActor->FindComponentByClass<USubmarineCollisionComponent>())
        {
            ColComp->ApplyDamage(Stats->AttackDamage, this);
        }

        // Splash damage: find all submarines in radius
        if (Stats->ExplosionRadius > 0.f)
        {
            TArray<FOverlapResult> Overlaps;
            FCollisionShape Sphere = FCollisionShape::MakeSphere(Stats->ExplosionRadius);
            FCollisionQueryParams Params;
            Params.AddIgnoredActor(this);
            Params.AddIgnoredActor(HitActor); // direct hit already handled

            GetWorld()->OverlapMultiByChannel(Overlaps, ImpactLocation,
                FQuat::Identity, ECC_Pawn, Sphere, Params);

            for (const FOverlapResult& Overlap : Overlaps)
            {
                AActor* SplashTarget = Overlap.GetActor();
                if (!SplashTarget) continue;

                if (USubmarineCollisionComponent* SplashCol =
                    SplashTarget->FindComponentByClass<USubmarineCollisionComponent>())
                {
                    const float Dist = FVector::Dist(ImpactLocation, SplashTarget->GetActorLocation());
                    const float Alpha = 1.f - FMath::Clamp(
                        Dist / FMath::Max(Stats->ExplosionRadius, 1.f), 0.f, 1.f);
                    // Lerp between SplashDamageFalloff..1 based on proximity
                    const float DamageMult = FMath::Lerp(Stats->SplashDamageFalloff, 1.f, Alpha);
                    SplashCol->ApplyDamage(Stats->AttackDamage * DamageMult, this);
                }
            }
        }
    }

    // Destroy — Blueprint can delay this via OnImpact event to play FX first
    Destroy();
}

// -----------------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------------
const UTorpedoCharacteristics* ATorpedoPawn::GetStats() const
{
    if (Characteristics) return Characteristics;
    return GetDefault<UTorpedoCharacteristics>();
}