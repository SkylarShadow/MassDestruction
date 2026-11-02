// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Effect/MassEffectTypes.h"
#include "MassEntityTypes.h"

#include "MassCombatFragments.generated.h"

USTRUCT()
struct MASSBILITY_API FMassCombatFragment : public FMassFragment
{
	GENERATED_BODY()

	FMassEntityHandle LastDamageSource;
	FMassEffectId LastDamageEffect;
	uint32 LastDamageTick = 0;
	uint32 DeathStartedTick = 0;
};

USTRUCT()
struct MASSBILITY_API FMassDyingTag : public FMassTag
{
	GENERATED_BODY()
};

