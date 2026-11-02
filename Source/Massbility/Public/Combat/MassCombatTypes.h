// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Effect/MassEffectTypes.h"
#include "Mass/EntityHandle.h"

#include "MassCombatTypes.generated.h"

USTRUCT(BlueprintType)
struct MASSBILITY_API FMassDamageContext
{
	GENERATED_BODY()

	FMassEntityHandle Source;
	FMassEffectId EffectId;
	float RawMagnitude = 0.0f;
	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ZeroVector;
};

struct MASSBILITY_API FMassDamageRequest
{
	FMassEntityHandle Target;
	FMassDamageContext Context;
};

struct MASSBILITY_API FMassDeathEvent
{
	FMassEntityHandle Victim;
	FMassEntityHandle Source;
	FMassEffectId EffectId;
	uint32 DeathTick = 0;
};

