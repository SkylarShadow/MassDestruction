// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Effect/MassbilityEffectTypes.h"
#include "MassEntityTypes.h"

#include "MassbilityCombatFragments.generated.h"

USTRUCT()
struct MASSBILITY_API FMassbilityCombatFragment : public FMassFragment
{
	GENERATED_BODY()

	FMassEntityHandle LastDamageSource;
	FMassbilityEffectId LastDamageEffect;
	uint32 LastDamageTick = 0;
	uint32 DeathStartedTick = 0;
};

USTRUCT()
struct MASSBILITY_API FMassbilityDyingTag : public FMassTag
{
	GENERATED_BODY()
};

