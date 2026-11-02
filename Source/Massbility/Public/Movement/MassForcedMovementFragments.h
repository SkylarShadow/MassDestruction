// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "Movement/MassForcedMovementTypes.h"

#include "MassForcedMovementFragments.generated.h"

/** One fixed-capacity forced movement state per participating Mass unit. */
USTRUCT()
struct MASSBILITY_API FMassForcedMovementFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector StartLocation = FVector::ZeroVector;
	FVector Vector = FVector::ZeroVector;
	uint32 StartTick = 0;
	uint32 EndTick = 0;
	uint32 AppliedTicks = 0;
	uint32 RequestSerial = 0;
	uint8 Priority = 0;
	EMassForcedMovementMode Mode = EMassForcedMovementMode::Displacement;
	EMassForcedMovementCollisionPolicy CollisionPolicy = EMassForcedMovementCollisionPolicy::Ignore;
	bool bActive = false;

	void Reset() { *this = {}; }
};

/** Limits forced movement processing to entities with an active request. */
USTRUCT()
struct MASSBILITY_API FMassHasForcedMovementTag : public FMassTag
{
	GENERATED_BODY()
};
