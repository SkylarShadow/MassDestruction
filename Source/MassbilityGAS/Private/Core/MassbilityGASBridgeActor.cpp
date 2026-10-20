// Copyright MassDestruction. All Rights Reserved.

#include "Core/MassbilityGASBridgeActor.h"

#include "AbilitySystemComponent.h"

AMassbilityGASBridgeActor::AMassbilityGASBridgeActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(false);
}

void AMassbilityGASBridgeActor::BeginPlay()
{
	Super::BeginPlay();
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
}

