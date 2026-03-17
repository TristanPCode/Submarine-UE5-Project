// Fill out your copyright notice in the Description page of Project Settings.


#include "SubmarineGameMode.h"
#include "SubmarinePawn.h"

ASubmarineGameMode::ASubmarineGameMode()
{
	DefaultPawnClass = ASubmarinePawn::StaticClass();
}