// Fill out your copyright notice in the Description page of Project Settings.

#include "TorpedoPawn.h"
#include "TorpedoCharacteristics.h"
#include "TorpedoPhysicsComponent.h"
#include "SubmarineCollisionComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

ATorpedoPawn::ATorpedoPawn()
{
    PrimaryActorTick.bCanEverTick = true;

    TorpedoBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TorpedoBody"));
    RootComponent = TorpedoBody;
    // Generate hit events so we can react to collisions
    TorpedoBody->SetNotifyRigidBodyCollision(true);
    TorpedoBody->SetGenerateOverlapEvents(false);

    // Collision profile: block pawns/world so hit events fire,
    // but we'll ignore the specific firing submarine at spawn.
    TorpedoBody->SetCollisionProfileName(TEXT("BlockAll"));

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

    // Make the torpedo pass completely through the firing submarine.
    // This is the primary passthrough mechanism — does not rely on actor checks.
    // Ignore firing submarine — called here AND at SetCharacteristics for safety
    if (IsValid(FiringShooter))
    {
        TorpedoBody->IgnoreActorWhenMoving(FiringShooter.Get(), true);
        UE_LOG(LogTemp, Warning, TEXT("[Torpedo] %s ignoring %s"),
            *GetName(), *FiringShooter->GetName());
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[Torpedo] %s — FiringSubmarine is NULL at BeginPlay!"), *GetName());
    }


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

    UE_LOG(LogTemp, Warning, TEXT("[Torpedo] %s spawned at %s, InitialVel=(%.0f,%.0f,%.0f)"),
        *GetName(),
        *GetActorLocation().ToString(),
        PhysicsHandler ? PhysicsHandler->PhysicsVelocity.X : 0.f,
        PhysicsHandler ? PhysicsHandler->PhysicsVelocity.Y : 0.f,
        PhysicsHandler ? PhysicsHandler->PhysicsVelocity.Z : 0.f);
}

// -----------------------------------------------------------------------------
//  Setup (called by SubmarineTorpedoComponent)
// -----------------------------------------------------------------------------
void ATorpedoPawn::SetCharacteristics(UTorpedoCharacteristics* InCharacteristics)
{
    Characteristics = InCharacteristics;

    if (PhysicsHandler)
        PhysicsHandler->Characteristics = Characteristics;

    // Set ignore as early as possible — before FinishSpawning / BeginPlay
    if (IsValid(FiringShooter))
        TorpedoBody->IgnoreActorWhenMoving(FiringShooter.Get(), true);
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
        UE_LOG(LogTemp, Warning, TEXT("[Torpedo] %s lifetime expired after %.2fs"), *GetName(), LifetimeElapsed);
        OnExpired.Broadcast();
        Explode(nullptr, GetActorLocation());
        return;
    }

    // -- Apply physics velocity --------------------------------------------
    if (PhysicsHandler)
    {
        const FVector Vel = PhysicsHandler->PhysicsVelocity;
        const FVector MoveDelta = Vel * DeltaTime;

        // Log every 0.2s
        static float DbgT = 0.f;
        DbgT += DeltaTime;
        if (DbgT >= 0.2f)
        {
            DbgT = 0.f;
            UE_LOG(LogTemp, Warning,
                TEXT("[Torpedo] %s | Vel=(%.0f,%.0f,%.0f) Speed=%.0f | ActorFwd=(%.2f,%.2f,%.2f)"),
                *GetName(),
                Vel.X, Vel.Y, Vel.Z, Vel.Size(),
                GetActorForwardVector().X,
                GetActorForwardVector().Y,
                GetActorForwardVector().Z);
        }

        FHitResult Hit;
        AddActorWorldOffset(MoveDelta, true, &Hit);

        if (Hit.IsValidBlockingHit() && Hit.GetActor())
        {
            UE_LOG(LogTemp, Warning, TEXT("[Torpedo] %s HIT %s"),
                *GetName(), *Hit.GetActor()->GetName());
            OnTorpedoHit(TorpedoBody, Hit.GetActor(),
                Hit.GetComponent(), FVector::ZeroVector, Hit);
        }
    }

    // -- Update rotation to match velocity direction -----------------------
    if (PhysicsHandler && !PhysicsHandler->PhysicsVelocity.IsNearlyZero(1.f))
    {
        const FVector Vel = PhysicsHandler->PhysicsVelocity;

        // If velocity is pointing mostly backward relative to our facing,
        // mirror it so the torpedo always rotates as if facing forward.
        // This means gravity/lateral drift still tilts the nose correctly,
        // but a negative forward speed doesn't flip the torpedo 180°.
        const float ForwardDot = FVector::DotProduct(Vel.GetSafeNormal(), GetActorForwardVector());
        const FVector RotationVel = (ForwardDot >= 0.f) ? Vel : -Vel;

        const FRotator TargetRot = RotationVel.Rotation();
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
void ATorpedoPawn::Explode_Implementation(AActor* DirectHitActor, const FVector& ImpactLocation)
{
    if (bExploded) return;
    bExploded = true;

    const UTorpedoCharacteristics* Stats = GetStats();

    UE_LOG(LogTemp, Warning, TEXT("[Torpedo] %s EXPLODE at %s | DirectHit=%s | Radius=%.0f | Damage=%.0f"),
        *GetName(),
        *ImpactLocation.ToString(),
        DirectHitActor ? *DirectHitActor->GetName() : TEXT("None"),
        Stats ? Stats->ExplosionRadius : 0.f,
        Stats ? Stats->AttackDamage : 0.f);

    // -- Niagara explosion VFX ---------------------------------------------
    if (Stats && Stats->ExplosionEffect)
    {
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(
            GetWorld(),
            Stats->ExplosionEffect,
            ImpactLocation,
            FRotator::ZeroRotator,
            FVector(Stats->ExplosionEffectScale),
            true,   // auto-destroy
            true,   // auto-activate
            ENCPoolMethod::None);
        UE_LOG(LogTemp, Warning, TEXT("[Torpedo] Niagara effect spawned"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[Torpedo] No Niagara effect assigned in DA"));
    }

    // Notify listeners (Blueprint can add SFX / camera shake here)

    // Notify listeners
    OnImpact.Broadcast(DirectHitActor, ImpactLocation);

    // -- Direct hit --------------------------------------------------------
    if (DirectHitActor && Stats)
    {
        USubmarineCollisionComponent* ColComp =
            DirectHitActor->FindComponentByClass<USubmarineCollisionComponent>();

        UE_LOG(LogTemp, Warning, TEXT("[Torpedo] Direct hit on %s | ColComp=%s"),
            *DirectHitActor->GetName(),
            ColComp ? TEXT("FOUND") : TEXT("NOT FOUND — no USubmarineCollisionComponent!"));

        if (ColComp)
        {
            UE_LOG(LogTemp, Warning, TEXT("[Torpedo] Applying direct damage %.0f"), Stats->AttackDamage);
            ColComp->ApplyDamage(Stats->AttackDamage, this);
            UE_LOG(LogTemp, Warning, TEXT("[Torpedo] After damage, target health ratio = %.2f"),
                ColComp->GetHealthRatio());
        }
    }

    // -- Splash / indirect damage ------------------------------------------
    if (Stats && Stats->ExplosionRadius > 0.f)
    {
        TArray<FOverlapResult> Overlaps;
        FCollisionShape Sphere = FCollisionShape::MakeSphere(Stats->ExplosionRadius);
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(this);
        if (DirectHitActor) Params.AddIgnoredActor(DirectHitActor);

        // Try ECC_Pawn first
        int32 HitCount = GetWorld()->OverlapMultiByChannel(
            Overlaps, ImpactLocation, FQuat::Identity, ECC_Pawn, Sphere, Params);

        UE_LOG(LogTemp, Warning,
            TEXT("[Torpedo] Splash overlap (ECC_Pawn) found %d results in radius %.0f"),
            Overlaps.Num(), Stats->ExplosionRadius);

        // If nothing found on ECC_Pawn, also try ECC_WorldDynamic as fallback
        if (Overlaps.Num() == 0)
        {
            TArray<FOverlapResult> FallbackOverlaps;
            GetWorld()->OverlapMultiByChannel(
                FallbackOverlaps, ImpactLocation, FQuat::Identity,
                ECC_WorldDynamic, Sphere, Params);
            UE_LOG(LogTemp, Warning,
                TEXT("[Torpedo] Splash overlap (ECC_WorldDynamic fallback) found %d results"),
                FallbackOverlaps.Num());
            Overlaps.Append(FallbackOverlaps);
        }

        // Deduplicate: one damage call per actor
        TSet<AActor*> DamagedActors;

        for (const FOverlapResult& Overlap : Overlaps)
        {
            AActor* SplashTarget = Overlap.GetActor();
            if (!SplashTarget || SplashTarget == this) continue;
            if (DamagedActors.Contains(SplashTarget)) continue;

            UE_LOG(LogTemp, Warning, TEXT("[Torpedo] Splash candidate: %s (class: %s)"),
                *SplashTarget->GetName(), *SplashTarget->GetClass()->GetName());

            USubmarineCollisionComponent* SplashCol =
                SplashTarget->FindComponentByClass<USubmarineCollisionComponent>();

            if (!SplashCol)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[Torpedo]   -> Skipped: no USubmarineCollisionComponent"));
                continue;
            }

            const float Dist = FVector::Dist(ImpactLocation, SplashTarget->GetActorLocation());
            const float SplashDmg = Stats->ComputeSplashDamage(Dist);

            UE_LOG(LogTemp, Warning,
                TEXT("[Torpedo]   -> Dist=%.0f SplashDmg=%.1f FiringSubMatch=%s"),
                Dist, SplashDmg,
                (SplashTarget == FiringShooter.Get()) ? TEXT("YES") : TEXT("NO"));

            if (SplashDmg > 0.f)
            {
                SplashCol->ApplySplashDamage(SplashDmg, this, FiringShooter.Get());
                DamagedActors.Add(SplashTarget);
                UE_LOG(LogTemp, Warning,
                    TEXT("[Torpedo]   -> Applied %.1f splash damage. New health ratio: %.2f"),
                    SplashDmg, SplashCol->GetHealthRatio());
            }
        }
    }

    Destroy();
}

// -----------------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------------

float ATorpedoPawn::GetCurrentSpeed() const
{
    return PhysicsHandler ? PhysicsHandler->PhysicsVelocity.Size() : 0.f;
}

const UTorpedoCharacteristics* ATorpedoPawn::GetStats() const
{
    if (Characteristics) return Characteristics;
    return GetDefault<UTorpedoCharacteristics>();
}