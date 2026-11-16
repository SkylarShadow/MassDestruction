// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"

#include "MassUnitHitShapeFragment.generated.h"

/** Query-only shape; it does not create a Chaos body or collision events. */
USTRUCT()
struct MASSBILITY_API FMassUnitHitShapeFragment : public FMassFragment
{
	GENERATED_BODY()

	float Radius = 34.0f;
	float HalfHeight = 88.0f;
	bool bEnabled = true;
};
