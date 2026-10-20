// Copyright MassDestruction. All Rights Reserved.

#include "Attributes/MassbilityAttributeAccess.h"

#include "Attributes/MassbilityAttributeSchema.h"
#include "Core/MassbilitySubsystem.h"
#include "Entity/MassbilityFragments.h"
#include "MassEntityManager.h"
//检查 Tag 是否对应 Vital 属性 如果不是，在已编译 Schema 的扩展属性中查找,最后输出 Storage + Index
bool FMassbilityAttributeAccess::ResolveHandle(const UMassbilitySubsystem& Subsystem, const UMassbilityAttributeSchema& Schema,
	const FGameplayTag AttributeTag, FMassbilityAttributeHandle& OutHandle)
{
	OutHandle = {};
	if (!AttributeTag.IsValid())
	{
		return false;
	}
	for (uint8 Index = 0; Index < static_cast<uint8>(EMassbilityVitalAttribute::MoveSpeed) + 1; ++Index)
	{
		if (Subsystem.VitalAttributeTags[Index] == AttributeTag)
		{
			OutHandle.Storage = EMassbilityAttributeStorage::Vital;
			OutHandle.Index = Index;
			return true;
		}
	}

	const uint16* SchemaId = Subsystem.SchemaIds.Find(&Schema);
	if (!SchemaId || !Subsystem.CompiledSchemas.IsValidIndex(*SchemaId))
	{
		return false;
	}
	const FMassbilityCompiledAttributeSchema& Compiled = Subsystem.CompiledSchemas[*SchemaId];
	for (uint8 Index = 0; Index < Compiled.AttributeCount; ++Index)
	{
		if (Compiled.Attributes[Index].AttributeTag == AttributeTag)
		{
			OutHandle.Storage = EMassbilityAttributeStorage::Extended;
			OutHandle.Index = Index;
			return true;
		}
	}
	return false;
}

bool FMassbilityAttributeAccess::Get(const UMassbilitySubsystem& Subsystem, const FMassEntityManager& EntityManager,
	const FMassEntityHandle Entity, const FMassbilityAttributeHandle Handle, float& OutValue)
{
	OutValue = 0.0f;
	if (!Handle.IsValid() || !EntityManager.IsEntityValid(Entity))
	{
		return false;
	}
	const FMassbilityVitalFragment* Vitals = EntityManager.GetFragmentDataPtr<FMassbilityVitalFragment>(Entity);
	if (!Vitals)
	{
		return false;
	}
	if (Handle.Storage == EMassbilityAttributeStorage::Vital)
	{
		switch (static_cast<EMassbilityVitalAttribute>(Handle.Index))
		{
		case EMassbilityVitalAttribute::Health: OutValue = Vitals->Health; return true;
		case EMassbilityVitalAttribute::MaxHealth: OutValue = Vitals->MaxHealth; return true;
		case EMassbilityVitalAttribute::MoveSpeed: OutValue = Vitals->MoveSpeed; return true;
		default: return false;
		}
	}
	const FMassbilityAttributeFragment* Attributes = EntityManager.GetFragmentDataPtr<FMassbilityAttributeFragment>(Entity);
	if (!Attributes || Handle.Storage != EMassbilityAttributeStorage::Extended || Handle.Index >= Attributes->AttributeCount)
	{
		return false;
	}
	OutValue = Attributes->Values[Handle.Index];
	return true;
}

bool FMassbilityAttributeAccess::SetClamped(const UMassbilitySubsystem& Subsystem, FMassEntityManager& EntityManager,
	const FMassEntityHandle Entity, const FMassbilityAttributeHandle Handle, const float Value)
{
	if (!Handle.IsValid() || !FMath::IsFinite(Value) || !EntityManager.IsEntityValid(Entity))
	{
		return false;
	}
	FMassbilityUnitFragment* Unit = EntityManager.GetFragmentDataPtr<FMassbilityUnitFragment>(Entity);
	FMassbilityVitalFragment* Vitals = EntityManager.GetFragmentDataPtr<FMassbilityVitalFragment>(Entity);
	FMassbilityAttributeFragment* Attributes = EntityManager.GetFragmentDataPtr<FMassbilityAttributeFragment>(Entity);
	if (!Unit || !Vitals || !Attributes || !Subsystem.CompiledSchemas.IsValidIndex(Unit->AttributeSchemaId))
	{
		return false;
	}
	if (Handle.Storage == EMassbilityAttributeStorage::Vital)
	{
		switch (static_cast<EMassbilityVitalAttribute>(Handle.Index))
		{
		case EMassbilityVitalAttribute::Health:
			Vitals->Health = FMath::Clamp(Value, 0.0f, Vitals->MaxHealth);
			return true;
		case EMassbilityVitalAttribute::MaxHealth:
			Vitals->MaxHealth = FMath::Max(0.0f, Value);
			Vitals->Health = FMath::Min(Vitals->Health, Vitals->MaxHealth);
			return true;
		case EMassbilityVitalAttribute::MoveSpeed:
			Vitals->MoveSpeed = FMath::Max(0.0f, Value);
			return true;
		default:
			return false;
		}
	}
	if (Handle.Storage != EMassbilityAttributeStorage::Extended || Handle.Index >= Attributes->AttributeCount)
	{
		return false;
	}
	const FMassbilityCompiledAttribute& Definition = Subsystem.CompiledSchemas[Unit->AttributeSchemaId].Attributes[Handle.Index];
	Attributes->Values[Handle.Index] = FMath::Clamp(Value, Definition.MinValue, Definition.MaxValue);
	return true;
}

bool FMassbilityAttributeAccess::Add(const UMassbilitySubsystem& Subsystem, FMassEntityManager& EntityManager,
	const FMassEntityHandle Entity, const FMassbilityAttributeHandle Handle, const float Delta)
{
	float Value = 0.0f;
	return Get(Subsystem, EntityManager, Entity, Handle, Value)
		&& SetClamped(Subsystem, EntityManager, Entity, Handle, Value + Delta);
}

bool FMassbilityAttributeAccess::AddResolved(const FMassbilityCompiledAttributeSchema& Schema, FMassbilityVitalFragment& Vitals,
	FMassbilityAttributeFragment& Attributes, const FMassbilityAttributeHandle Handle, const float Delta)
{
	if (!Handle.IsValid() || !FMath::IsFinite(Delta))
	{
		return false;
	}
	if (Handle.Storage == EMassbilityAttributeStorage::Vital)
	{
		switch (static_cast<EMassbilityVitalAttribute>(Handle.Index))
		{
		case EMassbilityVitalAttribute::Health:
			Vitals.Health = FMath::Clamp(Vitals.Health + Delta, 0.0f, Vitals.MaxHealth);
			return true;
		case EMassbilityVitalAttribute::MaxHealth:
			Vitals.MaxHealth = FMath::Max(0.0f, Vitals.MaxHealth + Delta);
			Vitals.Health = FMath::Min(Vitals.Health, Vitals.MaxHealth);
			return true;
		case EMassbilityVitalAttribute::MoveSpeed:
			Vitals.MoveSpeed = FMath::Max(0.0f, Vitals.MoveSpeed + Delta);
			return true;
		default:
			return false;
		}
	}
	if (Handle.Storage != EMassbilityAttributeStorage::Extended || Handle.Index >= Attributes.AttributeCount || Handle.Index >= Schema.AttributeCount)
	{
		return false;
	}
	const FMassbilityCompiledAttribute& Definition = Schema.Attributes[Handle.Index];
	Attributes.Values[Handle.Index] = FMath::Clamp(Attributes.Values[Handle.Index] + Delta, Definition.MinValue, Definition.MaxValue);
	return true;
}

