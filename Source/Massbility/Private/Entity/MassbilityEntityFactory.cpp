// Copyright MassDestruction. All Rights Reserved.

#include "Entity/MassbilityEntityFactory.h"

#include "Ability/MassbilityAbilitySubsystem.h"
#include "Core/MassbilitySubsystem.h"
#include "Entity/MassbilityFragments.h"
#include "Entity/MassbilityUnitDefinition.h"
#include "Massbility.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "Mass/EntityFragments.h"

FMassEntityHandle FMassbilityEntityFactory::Create(UMassbilitySubsystem& Subsystem,
	const UMassbilityUnitDefinition& UnitDefinition, const FTransform& Transform)
{
	uint16 UnitDefinitionId = Massbility::InvalidId;
	uint16 SchemaId = Massbility::InvalidId;
	if (!Subsystem.RegisterUnitDefinition(UnitDefinition, UnitDefinitionId, SchemaId) || !Subsystem.EntitySubsystem)
	{
		return {};
	}
	UMassbilityAbilitySubsystem* AbilitySubsystem = Subsystem.GetWorld()
		? Subsystem.GetWorld()->GetSubsystem<UMassbilityAbilitySubsystem>() : nullptr;
	uint16 AbilityLoadoutId = Massbility::InvalidId;
	if (!AbilitySubsystem || !AbilitySubsystem->RegisterLoadout(UnitDefinition, AbilityLoadoutId))
	{
		UE_LOG(LogMassbility, Error, TEXT("Could not compile the ability loadout for '%s'."), *UnitDefinition.GetPathName());
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

	{
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
		State.EffectTags.Reset();
		State.RuntimeTags = BaseTags;
		FMassbilityTagMask StateMask;
		if (Subsystem.BuildStateMask(State.StateFlags, StateMask))
		{
			State.RuntimeTags.Append(StateMask);
		}

		FMassbilityAbilityFragment& AbilityState = EntityView.GetFragmentData<FMassbilityAbilityFragment>();
		AbilityState.AbilityLoadoutId = AbilityLoadoutId;
		EntityView.GetFragmentData<FMassbilityTargetFragment>().NextRefreshTick =
			Subsystem.GetSimulationClock().GetCurrentTick()
			+ static_cast<uint32>(Entity.Index) % Subsystem.GetTargetRefreshTicks();
		EntityView.GetFragmentData<FTransformFragment>().SetTransform(Transform);
	}

	const FMassbilityCompiledAbilityLoadout* Loadout = AbilitySubsystem->FindLoadout(AbilityLoadoutId);
	if (Loadout && Loadout->AbilityCount > 0)
	{
		EntityManager.AddTagToEntity(Entity, FMassbilityHasAbilitiesTag::StaticStruct());
	}

	return Entity;
}
