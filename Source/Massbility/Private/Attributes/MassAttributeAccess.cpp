// Copyright MassDestruction. All Rights Reserved.

#include "Attributes/MassAttributeAccess.h"

#include "Attributes/MassAttributeSet.h"
#include "Core/MassRuntimeSubsystem.h"
#include "Entity/MassFragments.h"
#include "MassEntityManager.h"

//把AttributeTag 解析为 {Storage, Index}，
bool FMassAttributeAccess::ResolveHandle(const UMassRuntimeSubsystem& Subsystem, const UMassUnitDefinition& UnitDefinition,
	const FGameplayTag AttributeTag, FMassAttributeHandle& OutHandle)
{
	OutHandle = {};
	if (!AttributeTag.IsValid())
	{
		return false;
	}
	for (uint8 Index = 0; Index < static_cast<uint8>(EMassVitalAttribute::MoveSpeed) + 1; ++Index)
	{
		if (Subsystem.VitalAttributeTags[Index] == AttributeTag)
		{
		    //{Storage, Index}
			OutHandle.Storage = EMassAttributeStorage::Vital;
			OutHandle.Index = Index;
			return true;
		}
	}

	const uint16* LayoutId = Subsystem.LayoutIds.Find(&UnitDefinition);
	if (!LayoutId || !Subsystem.CompiledLayouts.IsValidIndex(*LayoutId))
	{
		return false;
	}
	const FMassCompiledAttributeLayout& Compiled = Subsystem.CompiledLayouts[*LayoutId];
	for (uint8 Index = 0; Index < Compiled.AttributeCount; ++Index)
	{
		if (Compiled.Attributes[Index].AttributeTag == AttributeTag)
		{
			OutHandle.Storage = EMassAttributeStorage::Extended;
			OutHandle.Index = Index;
			return true;
		}
	}
	return false;
}

// 读取解析好的属性
bool FMassAttributeAccess::Get(const UMassRuntimeSubsystem& Subsystem, const FMassEntityManager& EntityManager,
	const FMassEntityHandle Entity, const FMassAttributeHandle Handle, float& OutValue)
{
	OutValue = 0.0f;
	if (!Handle.IsValid() || !EntityManager.IsEntityValid(Entity))
	{
		return false;
	}
	const FMassVitalFragment* Vitals = EntityManager.GetFragmentDataPtr<FMassVitalFragment>(Entity);
	if (!Vitals)
	{
		return false;
	}
	if (Handle.Storage == EMassAttributeStorage::Vital)
	{
		switch (static_cast<EMassVitalAttribute>(Handle.Index))
		{
		case EMassVitalAttribute::Health: OutValue = Vitals->Health; return true;
		case EMassVitalAttribute::MaxHealth: OutValue = Vitals->MaxHealth; return true;
		case EMassVitalAttribute::MoveSpeed: OutValue = Vitals->MoveSpeed; return true;
		default: return false;
		}
	}
	const FMassAttributeFragment* Attributes = EntityManager.GetFragmentDataPtr<FMassAttributeFragment>(Entity);
	if (!Attributes || Handle.Storage != EMassAttributeStorage::Extended || Handle.Index >= Attributes->AttributeCount)
	{
		return false;
	}
	OutValue = Attributes->Values[Handle.Index];
	return true;
}

//写入属性，同时保证属性合法
bool FMassAttributeAccess::SetClamped(const UMassRuntimeSubsystem& Subsystem, FMassEntityManager& EntityManager,
	const FMassEntityHandle Entity, const FMassAttributeHandle Handle, const float Value)
{
	if (!Handle.IsValid() || !FMath::IsFinite(Value) || !EntityManager.IsEntityValid(Entity))
	{
		return false;
	}
	FMassUnitFragment* Unit = EntityManager.GetFragmentDataPtr<FMassUnitFragment>(Entity);
	FMassVitalFragment* Vitals = EntityManager.GetFragmentDataPtr<FMassVitalFragment>(Entity);
	FMassAttributeFragment* Attributes = EntityManager.GetFragmentDataPtr<FMassAttributeFragment>(Entity);
	if (!Unit || !Vitals || !Attributes || !Subsystem.CompiledLayouts.IsValidIndex(Unit->AttributeLayoutId))
	{
		return false;
	}
	if (Handle.Storage == EMassAttributeStorage::Vital)
	{
		switch (static_cast<EMassVitalAttribute>(Handle.Index))
		{
		case EMassVitalAttribute::Health:
			Vitals->Health = FMath::Clamp(Value, 0.0f, Vitals->MaxHealth);
			return true;
		case EMassVitalAttribute::MaxHealth:
			Vitals->MaxHealth = FMath::Max(0.0f, Value);
			Vitals->Health = FMath::Min(Vitals->Health, Vitals->MaxHealth);
			return true;
		case EMassVitalAttribute::MoveSpeed:
			Vitals->MoveSpeed = FMath::Max(0.0f, Value);
			return true;
		default:
			return false;
		}
	}
	if (Handle.Storage != EMassAttributeStorage::Extended || Handle.Index >= Attributes->AttributeCount
		|| Handle.Index >= Subsystem.CompiledLayouts[Unit->AttributeLayoutId].AttributeCount)
	{
		return false;
	}
	const FMassCompiledAttribute& Definition = Subsystem.CompiledLayouts[Unit->AttributeLayoutId].Attributes[Handle.Index];
	Attributes->Values[Handle.Index] = FMath::Clamp(Value, Definition.MinValue, Definition.MaxValue);
	return true;
}

//在当前值上加增量
bool FMassAttributeAccess::Add(const UMassRuntimeSubsystem& Subsystem, FMassEntityManager& EntityManager,
	const FMassEntityHandle Entity, const FMassAttributeHandle Handle, const float Delta)
{
	float Value = 0.0f;
	return Get(Subsystem, EntityManager, Entity, Handle, Value)
		&& SetClamped(Subsystem, EntityManager, Entity, Handle, Value + Delta);
}

// 同Add( 主要用于Mass Effect
bool FMassAttributeAccess::AddResolved(const FMassCompiledAttributeLayout& Schema, FMassVitalFragment& Vitals,
	FMassAttributeFragment& Attributes, const FMassAttributeHandle Handle, const float Delta)
{
	if (!Handle.IsValid() || !FMath::IsFinite(Delta))
	{
		return false;
	}
	if (Handle.Storage == EMassAttributeStorage::Vital)
	{
		switch (static_cast<EMassVitalAttribute>(Handle.Index))
		{
		case EMassVitalAttribute::Health:
			Vitals.Health = FMath::Clamp(Vitals.Health + Delta, 0.0f, Vitals.MaxHealth);
			return true;
		case EMassVitalAttribute::MaxHealth:
			Vitals.MaxHealth = FMath::Max(0.0f, Vitals.MaxHealth + Delta);
			Vitals.Health = FMath::Min(Vitals.Health, Vitals.MaxHealth);
			return true;
		case EMassVitalAttribute::MoveSpeed:
			Vitals.MoveSpeed = FMath::Max(0.0f, Vitals.MoveSpeed + Delta);
			return true;
		default:
			return false;
		}
	}
	if (Handle.Storage != EMassAttributeStorage::Extended || Handle.Index >= Attributes.AttributeCount || Handle.Index >= Schema.AttributeCount)
	{
		return false;
	}
	const FMassCompiledAttribute& Definition = Schema.Attributes[Handle.Index];
	Attributes.Values[Handle.Index] = FMath::Clamp(Attributes.Values[Handle.Index] + Delta, Definition.MinValue, Definition.MaxValue);
	return true;
}

