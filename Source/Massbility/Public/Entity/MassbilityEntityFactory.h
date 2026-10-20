// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Mass/EntityHandle.h"
#include "Math/Transform.h"

class UMassbilitySubsystem;
class UMassbilityUnitDefinition;

/** Internal factory; all externally visible creation goes through UMassbilitySubsystem. */
struct MASSBILITY_API FMassbilityEntityFactory
{
	static FMassEntityHandle Create(UMassbilitySubsystem& Subsystem, const UMassbilityUnitDefinition& UnitDefinition,
		const FTransform& Transform);
};
