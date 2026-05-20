// Fill out your copyright notice in the Description page of Project Settings.

#include "CPU/SubmarineCPUController.h"

ASubmarineCPUController::ASubmarineCPUController()
{
    // No tick needed at this stage — no behavior
    PrimaryActorTick.bCanEverTick = false;

    // Future: Brain = CreateDefaultSubobject<USubmarineBrainComponent>(TEXT("Brain"));
}