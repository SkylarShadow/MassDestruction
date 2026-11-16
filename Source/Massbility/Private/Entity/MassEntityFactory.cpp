// Copyright MassDestruction. All Rights Reserved.

// Entity Factory 将 Unit Definition 转换为 Mass Entity 初始化数据。

#include "Entity/MassEntityFactory.h"

#include "Ability/MassAbilitySubsystem.h"
#include "Core/MassRuntimeSubsystem.h"
#include "Entity/MassFragments.h"
#include "Entity/MassUnitDefinition.h"
#include "Mass.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "Mass/EntityFragments.h"
#include "Targeting/MassUnitHitShapeFragment.h"

FMassEntityHandle FMassEntityFactory::Create(UMassRuntimeSubsystem& Subsystem,
	const UMassUnitDefinition& UnitDefinition, const FTransform& Transform, const int16 TeamIdOverride,
	const bool bGrantAbilities)
{
	// 先注册单位相关运行时数据，再创建实体并初始化所有基础 Fragment。
	uint16 UnitDefinitionId = Massbility::InvalidId;
	uint16 SchemaId = Massbility::InvalidId;
	if (!Subsystem.RegisterUnitDefinition(UnitDefinition, UnitDefinitionId, SchemaId) || !Subsystem.EntitySubsystem)
	{
		return {};
	}
	uint16 AbilityLoadoutId = Massbility::InvalidId;
	UMassAbilitySubsystem* AbilitySubsystem = bGrantAbilities && Subsystem.GetWorld()
		? Subsystem.GetWorld()->GetSubsystem<UMassAbilitySubsystem>() : nullptr;
	if (bGrantAbilities && (!AbilitySubsystem || !AbilitySubsystem->RegisterLoadout(UnitDefinition, AbilityLoadoutId)))
	{
		UE_LOG(LogMassbility, Error, TEXT("Could not compile the ability loadout for '%s'."), *UnitDefinition.GetPathName());
		return {};
	}

	FString TagError;
	FMassTagMask BaseTags;
	if (!Subsystem.RuntimeTagCatalog.BuildMask(UnitDefinition.GetInitialRuntimeTags(), BaseTags, TagError))
	{
		UE_LOG(LogMassbility, Error, TEXT("Could not create '%s': %s"), *UnitDefinition.GetPathName(), *TagError);
		return {};
	}

	FMassEntityManager& EntityManager = Subsystem.EntitySubsystem->GetMutableEntityManager();
	// Archetype 决定实体拥有的 Fragment；创建失败时不继续写入实体数据。
	const FMassArchetypeHandle Archetype = Subsystem.GetOrCreateUnitArchetype();
	if (!Archetype.IsValid())
	{
		return {};
	}

	const FMassEntityHandle Entity = EntityManager.CreateEntity(Archetype);
	if (!EntityManager.IsEntityValid(Entity))
	{
		UE_LOG(LogMassbility, Error, TEXT("Mass failed to create a Mass entity."));
		return {};
	}

	{
		FMassEntityView EntityView(EntityManager, Entity);
		// 将 Definition 的初始属性、状态、能力、目标刷新时间和碰撞形状复制到实体。
		FMassUnitFragment& Unit = EntityView.GetFragmentData<FMassUnitFragment>();
		Unit.UnitDefinitionId = UnitDefinitionId;
		Unit.AttributeSchemaId = SchemaId;
		Unit.TeamId = TeamIdOverride != INDEX_NONE ? TeamIdOverride
			: static_cast<int16>(FMath::Clamp(UnitDefinition.GetTeamId(), static_cast<int32>(MIN_int16), static_cast<int32>(MAX_int16)));

		FMassVitalFragment& Vitals = EntityView.GetFragmentData<FMassVitalFragment>();
		Vitals.MaxHealth = FMath::Max(0.0f, UnitDefinition.GetInitialMaxHealth());
		Vitals.Health = FMath::Clamp(UnitDefinition.GetInitialHealth(), 0.0f, Vitals.MaxHealth);
		Vitals.MoveSpeed = FMath::Max(0.0f, UnitDefinition.GetInitialMoveSpeed());

		FMassAttributeFragment& Attributes = EntityView.GetFragmentData<FMassAttributeFragment>();
		const FMassCompiledAttributeSchema& Schema = Subsystem.CompiledSchemas[SchemaId];
		Attributes.AttributeCount = Schema.AttributeCount;
		for (int32 Index = 0; Index < Massbility::MaxExtendedAttributes; ++Index)
		{
			Attributes.Values[Index] = Index < Schema.AttributeCount ? Schema.Attributes[Index].DefaultValue : 0.0f;
		}

		FMassStateFragment& State = EntityView.GetFragmentData<FMassStateFragment>();
		State.StateFlags = EMassStateFlags::Targetable;
		State.BaseTags = BaseTags;
		State.EffectTags.Reset();
		State.RuntimeTags = BaseTags;
		FMassTagMask StateMask;
		if (Subsystem.BuildStateMask(State.StateFlags, StateMask))
		{
			State.RuntimeTags.Append(StateMask);
		}

		FMassAbilityFragment& AbilityState = EntityView.GetFragmentData<FMassAbilityFragment>();
		AbilityState.AbilityLoadoutId = AbilityLoadoutId;
		EntityView.GetFragmentData<FMassTargetFragment>().NextRefreshTick =
			Subsystem.GetSimulationClock().GetCurrentTick()
			+ static_cast<uint32>(Entity.Index) % Subsystem.GetTargetRefreshTicks();
		EntityView.GetFragmentData<FTransformFragment>().SetTransform(Transform);
		FMassUnitHitShapeFragment& HitShape = EntityView.GetFragmentData<FMassUnitHitShapeFragment>();
		HitShape.Radius = FMath::Max(0.0f, UnitDefinition.GetHitRadius());
		HitShape.HalfHeight = FMath::Max(HitShape.Radius, UnitDefinition.GetHitHalfHeight());
		HitShape.bEnabled = HitShape.Radius > 0.0f;
	}

	const FMassCompiledAbilityLoadout* Loadout = AbilitySubsystem
		? AbilitySubsystem->FindLoadout(AbilityLoadoutId) : nullptr;
	if (Loadout && Loadout->AbilityCount > 0)
	{
		EntityManager.AddTagToEntity(Entity, FMassHasAbilitiesTag::StaticStruct());
	}

	return Entity;
}
