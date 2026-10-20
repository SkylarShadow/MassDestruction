// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Ability/MassbilityAbilityTypes.h"
#include "Containers/StaticArray.h"
#include "Core/MassbilityTypes.h"
#include "Combat/MassbilityCombatFragments.h"
#include "Effect/MassbilityEffectTypes.h"
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
//TODO: 可继承？让他支持更多属性， 更改语义
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
	FMassbilityTagMask EffectTags;
	FMassbilityTagMask RuntimeTags;
};

/** Fixed-capacity active-effect storage. Present on every Massbility unit. */
USTRUCT()
struct MASSBILITY_API FMassbilityActiveEffectFragment : public FMassFragment
{
	GENERATED_BODY()

	uint8 ActiveEffectCount = 0;
	TStaticArray<FMassbilityActiveEffectSlot, Massbility::MaxActiveEffects> Slots;
};

/** Enables the periodic processor only for archetypes that currently have effects. */
USTRUCT()
struct MASSBILITY_API FMassbilityHasActiveEffectsTag : public FMassTag
{
	GENERATED_BODY()
};

/** Fixed-capacity ability loadout and per-slot cooldown deadlines. */
USTRUCT()
struct MASSBILITY_API FMassbilityAbilityFragment : public FMassFragment
{
	GENERATED_BODY()

	uint16 AbilityLoadoutId = Massbility::InvalidId;
	TStaticArray<uint32, Massbility::MaxAbilitiesPerUnit> NextReadyTicks = {};
};

/** Cached target selected by the low-frequency targeting pass. */
USTRUCT()
struct MASSBILITY_API FMassbilityTargetFragment : public FMassFragment
{
	GENERATED_BODY()

	FMassEntityHandle Target;
	FVector LastKnownLocation = FVector::ZeroVector;
	uint32 NextRefreshTick = 0;
};

/** Limits ability processing to units whose compiled loadout is non-empty. */
USTRUCT()
struct MASSBILITY_API FMassbilityHasAbilitiesTag : public FMassTag
{
	GENERATED_BODY()
};
