// Copyright 2018 Sticks & Stones. All Rights Reserved.

#include "TestCharacter.h"

#include "DrawDebugHelpers.h"
#include "Gameplay/Movement/TrajectoryComponent.h"

ATestCharacter::ATestCharacter(): TrajectoryComponent(nullptr)
{
	CurrentIndex = 0;
	RequiredDistanceToWaypoint = 100.f;

	TrajectoryComponent = CreateDefaultSubobject<UTrajectoryComponent>(TEXT("TrajectoryComponent"));
}

void ATestCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (Waypoints.Num() == 0) 
	{
		CurrentIndex = -1;
	}
	else 
	{
		CurrentIndex = 0;
	}
}

void ATestCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	MoveBetweenTargets();
	DebugWaypoints();
}

UTrajectoryComponent* ATestCharacter::GetTrajectoryComponent() 
{
	return TrajectoryComponent;
}

void ATestCharacter::MoveBetweenTargets() 
{
	if (CurrentIndex != -1) 
	{
		MoveToWaypoint();
		if (FVector::Dist(GetActorLocation(), Waypoints[CurrentIndex].Location) < RequiredDistanceToWaypoint) 
		{
			IncrementCurrentIndex();
		}
	}
}

void ATestCharacter::IncrementCurrentIndex() 
{
	CurrentIndex++;
	if (CurrentIndex >= Waypoints.Num()) 
	{
		CurrentIndex = 0;
	}
}

void ATestCharacter::DebugWaypoints()
{
	for (size_t i = 0; i < Waypoints.Num(); i++)
	{
		DrawDebugSphere(GetWorld(), Waypoints[i].Location, RequiredDistanceToWaypoint, 12, FColor::Purple);
		if (Waypoints.Num() > 1) 
		{
			int NextIndex = i + 1;
			if (NextIndex >= Waypoints.Num()) 
			{
				NextIndex = 0;
			}
			DrawDebugLine(GetWorld(), Waypoints[i].Location, Waypoints[NextIndex].Location, FColor::Purple);
		}
	}
}
