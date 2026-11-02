// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/MassTypes.h"
#include "GameplayTagContainer.h"
#include "Mass/EntityHandle.h"

class UMassAttributeSchema;
class UMassRuntimeSubsystem;
struct FMassEntityManager;
struct FMassAttributeFragment;
struct FMassCompiledAttributeSchema;
struct FMassVitalFragment;

/** Centralized attribute resolution and clamped access for cold and hot paths. */
struct MASSBILITY_API FMassAttributeAccess
{
	static bool ResolveHandle(const UMassRuntimeSubsystem& Subsystem, const UMassAttributeSchema& Schema,
		FGameplayTag AttributeTag, FMassAttributeHandle& OutHandle);

	static bool Get(const UMassRuntimeSubsystem& Subsystem, const FMassEntityManager& EntityManager,
		FMassEntityHandle Entity, FMassAttributeHandle Handle, float& OutValue);
	static bool SetClamped(const UMassRuntimeSubsystem& Subsystem, FMassEntityManager& EntityManager,
		FMassEntityHandle Entity, FMassAttributeHandle Handle, float Value);
	static bool Add(const UMassRuntimeSubsystem& Subsystem, FMassEntityManager& EntityManager,
		FMassEntityHandle Entity, FMassAttributeHandle Handle, float Delta);

	static bool AddResolved(const FMassCompiledAttributeSchema& Schema, FMassVitalFragment& Vitals,
		FMassAttributeFragment& Attributes, FMassAttributeHandle Handle, float Delta);
};
