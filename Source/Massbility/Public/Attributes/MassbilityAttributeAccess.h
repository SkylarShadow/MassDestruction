// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/MassbilityTypes.h"
#include "GameplayTagContainer.h"
#include "Mass/EntityHandle.h"

class UMassbilityAttributeSchema;
class UMassbilitySubsystem;
struct FMassEntityManager;
struct FMassbilityAttributeFragment;
struct FMassbilityCompiledAttributeSchema;
struct FMassbilityVitalFragment;

/** Centralized attribute resolution and clamped access for cold and hot paths. */
struct MASSBILITY_API FMassbilityAttributeAccess
{
	static bool ResolveHandle(const UMassbilitySubsystem& Subsystem, const UMassbilityAttributeSchema& Schema,
		FGameplayTag AttributeTag, FMassbilityAttributeHandle& OutHandle);

	static bool Get(const UMassbilitySubsystem& Subsystem, const FMassEntityManager& EntityManager,
		FMassEntityHandle Entity, FMassbilityAttributeHandle Handle, float& OutValue);
	static bool SetClamped(const UMassbilitySubsystem& Subsystem, FMassEntityManager& EntityManager,
		FMassEntityHandle Entity, FMassbilityAttributeHandle Handle, float Value);
	static bool Add(const UMassbilitySubsystem& Subsystem, FMassEntityManager& EntityManager,
		FMassEntityHandle Entity, FMassbilityAttributeHandle Handle, float Delta);

	static bool AddResolved(const FMassbilityCompiledAttributeSchema& Schema, FMassbilityVitalFragment& Vitals,
		FMassbilityAttributeFragment& Attributes, FMassbilityAttributeHandle Handle, float Delta);
};
