// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "External/MassbilityExternalTargetTypes.h"
#include "MassEntityTypes.h"

#include "MassbilityExternalTargetFragments.generated.h"

USTRUCT()
struct MASSBILITY_API FMassbilityExternalTargetFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 由外部目标子系统持有的句柄；不能通过代理实体的索引推断目标身份。 */
	FMassbilityExternalTargetHandle Handle;
};

USTRUCT()
struct MASSBILITY_API FMassbilityExternalTargetTag : public FMassTag
{
	GENERATED_BODY()
};

