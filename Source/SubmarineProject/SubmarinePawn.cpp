// Fill out your copyright notice in the Description page of Project Settings.


#include "SubmarinePawn.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/FloatingPawnMovement.h"


// Sets default values
ASubmarinePawn::ASubmarinePawn()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(RootComponent);
	Camera->SetRelativeLocation(FVector(-300.0f, 0.0f, 100.0f));

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->UpdatedComponent = RootComponent;

}

// Called when the game starts or when spawned
void ASubmarinePawn::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ASubmarinePawn::Tick(float DeltaTime)
{
	//Super::Tick(DeltaTime);

	ASubmarinePawn::TargetLinearSpeed = ASubmarinePawn::GetTargetSpeed();
	ASubmarinePawn::CurrentLinearSpeed = FMath::FInterpTo(ASubmarinePawn::CurrentLinearSpeed, ASubmarinePawn::TargetLinearSpeed, DeltaTime, ASubmarinePawn::LinearAcceleration);
	FVector Move = GetActorForwardVector() * ASubmarinePawn::CurrentLinearSpeed * DeltaTime;
	AddActorWorldOffset(Move, true);

	ASubmarinePawn::CurrentYawSpeed = FMath::FInterpTo(ASubmarinePawn::CurrentYawSpeed, TargetYawSpeed, DeltaTime, ASubmarinePawn::YawAcceleration);
	FRotator Rot = GetActorRotation();
	Rot.Yaw += ASubmarinePawn::CurrentYawSpeed * DeltaTime;
	SetActorRotation(Rot);

	Rot.Pitch = FMath::Clamp(Rot.Pitch, -45.0f, 45.0f);
}

// Called to bind functionality to input
void ASubmarinePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	PlayerInputComponent->BindAxis("MoveForward", this, &ASubmarinePawn::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &ASubmarinePawn::MoveRight);
	PlayerInputComponent->BindAxis("MoveUp", this, &ASubmarinePawn::MoveUp);

	PlayerInputComponent->BindAxis("Turn", this, &ASubmarinePawn::Turn);
	PlayerInputComponent->BindAxis("LookUp", this, &ASubmarinePawn::LookUp);

}

// SubMarine Movement
void ASubmarinePawn::MoveForward(float Value)
{
	//AddMovementInput(Value * GetActorForwardVector());
	ASubmarinePawn::LinearSpeedState = ESpeedState::ForwardMAX;
}

void ASubmarinePawn::MoveRight(float Value)
{
	AddMovementInput(Value * GetActorRightVector());
	ASubmarinePawn::LinearSpeedState = ESpeedState::Stand;
}

void ASubmarinePawn::MoveUp(float Value)
{
	AddMovementInput(Value * GetActorUpVector());
}

void ASubmarinePawn::Turn(float Value)
{
	FRotator Rot = GetActorRotation();
	Rot.Yaw += Value;
	SetActorRotation(Rot);
}

void ASubmarinePawn::LookUp(float Value)
{
	AddActorLocalRotation(FRotator(Value, 0.f, 0.f));
}

// SubMarine Speed and Inertia
float ASubmarinePawn::GetTargetSpeed()
{
	switch (ASubmarinePawn::LinearSpeedState)
	{
	case ESpeedState::ForwardMAX: return 2000.0f;
	case ESpeedState::ForwardMED: return 1200.0f;
	case ESpeedState::ForwardMIN: return 500.0f;
	case ESpeedState::Stand: return 0.0f;
	case ESpeedState::BackwardMIN: return -500.0f;
	case ESpeedState::BackwardMED: return -1200.0f;
	case ESpeedState::BackwardMAX: return -2000.0f;
	}

	return 0.0f;
}


