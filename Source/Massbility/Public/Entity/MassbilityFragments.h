// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Core/MassbilityTypes.h"
#include "MassEntityTypes.h"

#include "MassbilityFragments.generated.h"

/** IDs and team data shared by all Phase 1 Massbility unit archetypes. */
USTRUCT()
struct MASSBILITY_API FMassbilityUnitFragment : public FMassFragment
{
	GENERATED_BODY()

	uint16 UnitDefinitionId = Massbility::InvalidId;
	uint16 AttributeSchemaId = Massbility::InvalidId;
	int16 TeamId = 0;
};

/** High-frequency attributes kept in direct fragment fields. */
USTRUCT()
struct MASSBILITY_API FMassbilityVitalFragment : public FMassFragment
{
	GENERATED_BODY()

	float Health = 1.0f;
	float MaxHealth = 1.0f;
	float MoveSpeed = 0.0f;
};

/** Fixed-capacity extensible attributes resolved through a compiled schema. */
USTRUCT()
struct MASSBILITY_API FMassbilityAttributeFragment : public FMassFragment
{
	GENERATED_BODY()

	uint8 AttributeCount = 0;
	TStaticArray<float, Massbility::MaxExtendedAttributes> Values = {};
};

/** State flags and their compiled gameplay-tag representation. */
USTRUCT()
struct MASSBILITY_API FMassbilityStateFragment : public FMassFragment
{
	GENERATED_BODY()

	EMassbilityStateFlags StateFlags = EMassbilityStateFlags::Targetable;
	FMassbilityTagMask BaseTags;
	FMassbilityTagMask RuntimeTags;
};
