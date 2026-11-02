// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "External/MassExternalTargetTypes.h"
#include "MassEntityTypes.h"

#include "MassExternalTargetFragments.generated.h"

USTRUCT()
struct MASSBILITY_API FMassExternalTargetFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 由外部目标子系统持有的句柄；不能通过代理实体的索引推断目标身份。 */
	FMassExternalTargetHandle Handle;
};

USTRUCT()
struct MASSBILITY_API FMassExternalTargetTag : public FMassTag
{
	GENERATED_BODY()
};

