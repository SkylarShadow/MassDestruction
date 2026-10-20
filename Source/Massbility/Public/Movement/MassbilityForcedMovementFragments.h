// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "Movement/MassbilityForcedMovementTypes.h"

#include "MassbilityForcedMovementFragments.generated.h"

/** One fixed-capacity forced movement state per participating Massbility unit. */
USTRUCT()
struct MASSBILITY_API FMassbilityForcedMovementFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector StartLocation = FVector::ZeroVector;
	FVector Vector = FVector::ZeroVector;
	uint32 StartTick = 0;
	uint32 EndTick = 0;
	uint32 AppliedTicks = 0;
	uint32 RequestSerial = 0;
	uint8 Priority = 0;
	EMassbilityForcedMovementMode Mode = EMassbilityForcedMovementMode::Displacement;
	EMassbilityForcedMovementCollisionPolicy CollisionPolicy = EMassbilityForcedMovementCollisionPolicy::Ignore;
	bool bActive = false;

	void Reset() { *this = {}; }
};

/** Limits forced movement processing to entities with an active request. */
USTRUCT()
struct MASSBILITY_API FMassbilityHasForcedMovementTag : public FMassTag
{
	GENERATED_BODY()
};
