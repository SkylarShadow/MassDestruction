// Copyright MassDestruction. All Rights Reserved.

#include "Core/MassGASBridgeActor.h"

#include "AbilitySystemComponent.h"

AMassGASBridgeActor::AMassGASBridgeActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(false);
}

void AMassGASBridgeActor::BeginPlay()
{
	Super::BeginPlay();
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
}

