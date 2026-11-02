// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Mass/EntityHandle.h"
#include "Math/Transform.h"

class UMassRuntimeSubsystem;
class UMassUnitDefinition;

/** Internal factory; all externally visible creation goes through UMassRuntimeSubsystem. */
struct MASSBILITY_API FMassEntityFactory
{
	static FMassEntityHandle Create(UMassRuntimeSubsystem& Subsystem, const UMassUnitDefinition& UnitDefinition,
		const FTransform& Transform, int16 TeamIdOverride = INDEX_NONE);
};
