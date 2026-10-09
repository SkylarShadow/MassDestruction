// Copyright MassDestruction. All Rights Reserved.

#include "Entity/MassbilityEntityFactory.h"

#include "Core/MassbilitySubsystem.h"
#include "Entity/MassbilityFragments.h"
#include "Entity/MassbilityUnitDefinition.h"
#include "Massbility.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"

FMassEntityHandle FMassbilityEntityFactory::Create(UMassbilitySubsystem& Subsystem, const UMassbilityUnitDefinition& UnitDefinition)
{
	uint16 UnitDefinitionId = Massbility::InvalidId;
	uint16 SchemaId = Massbility::InvalidId;
	if (!Subsystem.RegisterUnitDefinition(UnitDefinition, UnitDefinitionId, SchemaId) || !Subsystem.EntitySubsystem)
	{
		return {};
	}

	FString TagError;
	FMassbilityTagMask BaseTags;
	if (!Subsystem.RuntimeTagCatalog.BuildMask(UnitDefinition.GetInitialRuntimeTags(), BaseTags, TagError))
	{
		UE_LOG(LogMassbility, Error, TEXT("Could not create '%s': %s"), *UnitDefinition.GetPathName(), *TagError);
		return {};
	}

	FMassEntityManager& EntityManager = Subsystem.EntitySubsystem->GetMutableEntityManager();
	const FMassArchetypeHandle Archetype = Subsystem.GetOrCreateUnitArchetype();
	if (!Archetype.IsValid())
	{
		return {};
	}

	const FMassEntityHandle Entity = EntityManager.CreateEntity(Archetype);
	if (!EntityManager.IsEntityValid(Entity))
	{
		UE_LOG(LogMassbility, Error, TEXT("Mass failed to create a Massbility entity."));
		return {};
	}

	FMassEntityView EntityView(EntityManager, Entity);
	FMassbilityUnitFragment& Unit = EntityView.GetFragmentData<FMassbilityUnitFragment>();
	Unit.UnitDefinitionId = UnitDefinitionId;
	Unit.AttributeSchemaId = SchemaId;
	Unit.TeamId = static_cast<int16>(FMath::Clamp(UnitDefinition.GetTeamId(), static_cast<int32>(MIN_int16), static_cast<int32>(MAX_int16)));

	FMassbilityVitalFragment& Vitals = EntityView.GetFragmentData<FMassbilityVitalFragment>();
	Vitals.MaxHealth = FMath::Max(0.0f, UnitDefinition.GetInitialMaxHealth());
	Vitals.Health = FMath::Clamp(UnitDefinition.GetInitialHealth(), 0.0f, Vitals.MaxHealth);
	Vitals.MoveSpeed = FMath::Max(0.0f, UnitDefinition.GetInitialMoveSpeed());

	FMassbilityAttributeFragment& Attributes = EntityView.GetFragmentData<FMassbilityAttributeFragment>();
	const FMassbilityCompiledAttributeSchema& Schema = Subsystem.CompiledSchemas[SchemaId];
	Attributes.AttributeCount = Schema.AttributeCount;
	for (int32 Index = 0; Index < Massbility::MaxExtendedAttributes; ++Index)
	{
		Attributes.Values[Index] = Index < Schema.AttributeCount ? Schema.Attributes[Index].DefaultValue : 0.0f;
	}

	FMassbilityStateFragment& State = EntityView.GetFragmentData<FMassbilityStateFragment>();
	State.StateFlags = EMassbilityStateFlags::Targetable;
	State.BaseTags = BaseTags;
	State.RuntimeTags = BaseTags;
	FMassbilityTagMask StateMask;
	if (Subsystem.BuildStateMask(State.StateFlags, StateMask))
	{
		State.RuntimeTags.Append(StateMask);
	}

	return Entity;
}
