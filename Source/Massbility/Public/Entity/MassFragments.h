// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Ability/MassAbilityTypes.h"
#include "Containers/StaticArray.h"
#include "Core/MassTypes.h"
#include "Combat/MassCombatFragments.h"
#include "Effect/MassEffectTypes.h"
#include "MassEntityTypes.h"

#include "MassFragments.generated.h"

/** IDs and team data shared by all Phase 1 Mass unit archetypes. */
USTRUCT()
struct MASSBILITY_API FMassUnitFragment : public FMassFragment
{
	GENERATED_BODY()

	uint16 UnitDefinitionId = Massbility::InvalidId;
	uint16 AttributeSchemaId = Massbility::InvalidId;
	int16 TeamId = 0;
};

/** High-frequency attributes kept in direct fragment fields. */
//TODO: 可继承？让他支持更多属性， 更改语义
USTRUCT()
struct MASSBILITY_API FMassVitalFragment : public FMassFragment
{
	GENERATED_BODY()

	float Health = 1.0f;
	float MaxHealth = 1.0f;
	float MoveSpeed = 0.0f;
};

/** Fixed-capacity extensible attributes resolved through a compiled schema. */
USTRUCT()
struct MASSBILITY_API FMassAttributeFragment : public FMassFragment
{
	GENERATED_BODY()

	uint8 AttributeCount = 0;
	TStaticArray<float, Massbility::MaxExtendedAttributes> Values = {};
};

/** State flags and their compiled gameplay-tag representation. */
USTRUCT()
struct MASSBILITY_API FMassStateFragment : public FMassFragment
{
	GENERATED_BODY()

	EMassStateFlags StateFlags = EMassStateFlags::Targetable;
	FMassTagMask BaseTags;
	FMassTagMask EffectTags;
	FMassTagMask RuntimeTags;
};

/** Fixed-capacity active-effect storage. Present on every Mass unit. */
USTRUCT()
struct MASSBILITY_API FMassActiveEffectFragment : public FMassFragment
{
	GENERATED_BODY()

	uint8 ActiveEffectCount = 0;
	TStaticArray<FMassActiveEffectSlot, Massbility::MaxActiveEffects> Slots;
};

/** Enables the periodic processor only for archetypes that currently have effects. */
USTRUCT()
struct MASSBILITY_API FMassHasActiveEffectsTag : public FMassTag
{
	GENERATED_BODY()
};

/** Fixed-capacity ability loadout and per-slot cooldown deadlines. */
USTRUCT()
struct MASSBILITY_API FMassAbilityFragment : public FMassFragment
{
	GENERATED_BODY()

	uint16 AbilityLoadoutId = Massbility::InvalidId;
	TStaticArray<uint32, Massbility::MaxAbilitiesPerUnit> NextReadyTicks = {};
};

/** Cached target selected by the low-frequency targeting pass. */
USTRUCT()
struct MASSBILITY_API FMassTargetFragment : public FMassFragment
{
	GENERATED_BODY()

	FMassEntityHandle Target;
	FVector LastKnownLocation = FVector::ZeroVector;
	uint32 NextRefreshTick = 0;
};

/** Limits ability processing to units whose compiled loadout is non-empty. */
USTRUCT()
struct MASSBILITY_API FMassHasAbilitiesTag : public FMassTag
{
	GENERATED_BODY()
};
