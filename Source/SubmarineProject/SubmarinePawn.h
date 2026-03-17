// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"

#include "SubmarinePawn.generated.h"

UENUM()
enum class ESpeedState : uint8
{
	BackwardMAX,
	BackwardMED,
	BackwardMIN,
	Stand,
	ForwardMIN,
	ForwardMED,
	ForwardMAX
};

UCLASS()
class SUBMARINEPROJECT_API ASubmarinePawn : public APawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	ASubmarinePawn();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

private:

	UPROPERTY(VisibleAnywhere)
	UStaticMeshComponent* Mesh;

	UPROPERTY(VisibleAnywhere)
	UCameraComponent* Camera;

	UPROPERTY(VisibleAnywhere)
	UFloatingPawnMovement* Movement;

	void MoveForward(float Value);
	void MoveRight(float Value);
	void MoveUp(float Value);

	void Turn(float Value);
	void LookUp(float Value);

	float GetTargetSpeed();

	ESpeedState LinearSpeedState = ESpeedState::Stand;
	float TargetLinearSpeed;
	float CurrentLinearSpeed;
	float LinearAcceleration;

	float TargetYawSpeed;
	float CurrentYawSpeed;
	float YawAcceleration;
};