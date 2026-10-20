// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Effect/MassbilityEffectTypes.h"
#include "Mass/EntityHandle.h"

#include "MassbilityCombatTypes.generated.h"

USTRUCT(BlueprintType)
struct MASSBILITY_API FMassbilityDamageContext
{
	GENERATED_BODY()

	FMassEntityHandle Source;
	FMassbilityEffectId EffectId;
	float RawMagnitude = 0.0f;
	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ZeroVector;
};

struct MASSBILITY_API FMassbilityDamageRequest
{
	FMassEntityHandle Target;
	FMassbilityDamageContext Context;
};

struct MASSBILITY_API FMassbilityDeathEvent
{
	FMassEntityHandle Victim;
	FMassEntityHandle Source;
	FMassbilityEffectId EffectId;
	uint32 DeathTick = 0;
};

