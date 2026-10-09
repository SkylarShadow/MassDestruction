// Copyright MassDestruction. All Rights Reserved.

#include "Core/MassbilitySubsystem.h"

#include "Core/MassbilitySettings.h"
#include "Engine/Engine.h"
#include "Entity/MassbilityEntityFactory.h"
#include "Entity/MassbilityFragments.h"
#include "Entity/MassbilityUnitDefinition.h"
#include "Massbility.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"

void UMassbilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	if (!EntitySubsystem)
	{
		UE_LOG(LogMassbility, Error, TEXT("Unable to resolve UMassEntitySubsystem."));
		return;
	}

	CompileSettings();
}

void UMassbilitySubsystem::Deinitialize()
{
	UnitArchetype = {};
	StateTagMasks.Reset();
	UnitDefinitions.Reset();
	UnitDefinitionIds.Reset();
	CompiledSchemas.Reset();
	SchemaIds.Reset();
	RuntimeTagCatalog = {};
	StateTagMappingAsset = nullptr;
	RuntimeTagCatalogAsset = nullptr;
	EntitySubsystem = nullptr;
	Super::Deinitialize();
}

bool UMassbilitySubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

UMassbilitySubsystem* UMassbilitySubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassbilitySubsystem>();
	}
	return nullptr;
}

FMassEntityHandle UMassbilitySubsystem::CreateUnit(const UMassbilityUnitDefinition& UnitDefinition)
{
	return FMassbilityEntityFactory::Create(*this, UnitDefinition);
}

bool UMassbilitySubsystem::DestroyUnit(const FMassEntityHandle Entity)
{
	if (!EntitySubsystem)
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	if (!EntityManager.IsEntityValid(Entity))
	{
		return false;
	}
	EntityManager.DestroyEntity(Entity);
	return true;
}

bool UMassbilitySubsystem::ResolveAttributeHandle(const UMassbilityAttributeSchema& Schema, const FGameplayTag AttributeTag, FMassbilityAttributeHandle& OutHandle) const
{
	OutHandle = {};
	const uint16* SchemaId = SchemaIds.Find(&Schema);
	if (!SchemaId || !CompiledSchemas.IsValidIndex(*SchemaId))
	{
		return false;
	}
	const FMassbilityCompiledAttributeSchema& Compiled = CompiledSchemas[*SchemaId];
	for (uint8 Index = 0; Index < Compiled.AttributeCount; ++Index)
	{
		if (Compiled.Attributes[Index].AttributeTag == AttributeTag)
		{
			OutHandle.Index = Index;
			return true;
		}
	}
	return false;
}

bool UMassbilitySubsystem::GetExtendedAttribute(const FMassEntityHandle Entity, const FMassbilityAttributeHandle Handle, float& OutValue) const
{
	OutValue = 0.0f;
	if (!EntitySubsystem || !Handle.IsValid())
	{
		return false;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	if (!EntityManager.IsEntityValid(Entity))
	{
		return false;
	}
	const FMassbilityAttributeFragment* Attributes = EntityManager.GetFragmentDataPtr<FMassbilityAttributeFragment>(Entity);
	if (!Attributes || Handle.Index >= Attributes->AttributeCount)
	{
		return false;
	}
	OutValue = Attributes->Values[Handle.Index];
	return true;
}

bool UMassbilitySubsystem::SetExtendedAttribute(const FMassEntityHandle Entity, const FMassbilityAttributeHandle Handle, const float Value)
{
	if (!EntitySubsystem || !Handle.IsValid())
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	if (!EntityManager.IsEntityValid(Entity))
	{
		return false;
	}
	FMassbilityUnitFragment* Unit = EntityManager.GetFragmentDataPtr<FMassbilityUnitFragment>(Entity);
	FMassbilityAttributeFragment* Attributes = EntityManager.GetFragmentDataPtr<FMassbilityAttributeFragment>(Entity);
	if (!Unit || !Attributes || !CompiledSchemas.IsValidIndex(Unit->AttributeSchemaId) || Handle.Index >= Attributes->AttributeCount)
	{
		return false;
	}
	const FMassbilityCompiledAttribute& Definition = CompiledSchemas[Unit->AttributeSchemaId].Attributes[Handle.Index];
	Attributes->Values[Handle.Index] = FMath::Clamp(Value, Definition.MinValue, Definition.MaxValue);
	return true;
}

bool UMassbilitySubsystem::AddExtendedAttribute(const FMassEntityHandle Entity, const FMassbilityAttributeHandle Handle, const float Delta)
{
	float CurrentValue = 0.0f;
	return GetExtendedAttribute(Entity, Handle, CurrentValue) && SetExtendedAttribute(Entity, Handle, CurrentValue + Delta);
}

bool UMassbilitySubsystem::SetStateFlags(const FMassEntityHandle Entity, const EMassbilityStateFlags NewFlags)
{
	if (!EntitySubsystem)
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	if (!EntityManager.IsEntityValid(Entity))
	{
		return false;
	}
	FMassbilityStateFragment* State = EntityManager.GetFragmentDataPtr<FMassbilityStateFragment>(Entity);
	if (!State)
	{
		return false;
	}
	FMassbilityTagMask StateMask;
	if (!BuildStateMask(NewFlags, StateMask))
	{
		return false;
	}
	State->StateFlags = NewFlags;
	State->RuntimeTags = State->BaseTags;
	State->RuntimeTags.Append(StateMask);
	return true;
}

bool UMassbilitySubsystem::HasRuntimeTags(const FMassEntityHandle Entity, const FMassbilityTagMask& Required, const FMassbilityTagMask& Blocked) const
{
	if (!EntitySubsystem)
	{
		return false;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	const FMassbilityStateFragment* State = EntityManager.GetFragmentDataPtr<FMassbilityStateFragment>(Entity);
	return State && State->RuntimeTags.ContainsAll(Required) && !State->RuntimeTags.ContainsAny(Blocked);
}

bool UMassbilitySubsystem::CompileSettings()
{
	const UMassbilitySettings* Settings = GetDefault<UMassbilitySettings>();
	RuntimeTagCatalogAsset = Settings ? Settings->RuntimeTagCatalog.LoadSynchronous() : nullptr;
	StateTagMappingAsset = Settings ? Settings->StateTagMapping.LoadSynchronous() : nullptr;
	if (!RuntimeTagCatalogAsset)
	{
		UE_LOG(LogMassbility, Warning, TEXT("No RuntimeTagCatalog is configured. Massbility unit creation is unavailable until one is assigned in Project Settings."));
		return false;
	}

	FString Error;
	if (!RuntimeTagCatalog.Compile(RuntimeTagCatalogAsset->GetRuntimeTags(), Error))
	{
		UE_LOG(LogMassbility, Error, TEXT("Failed to compile runtime tag catalog '%s': %s"), *RuntimeTagCatalogAsset->GetPathName(), *Error);
		return false;
	}

	StateTagMasks.Reset();
	if (!StateTagMappingAsset)
	{
		UE_LOG(LogMassbility, Warning, TEXT("No StateTagMapping is configured. State flags will not grant gameplay tags."));
		return true;
	}

	for (const FMassbilityStateTagBinding& Binding : StateTagMappingAsset->GetBindings())
	{
		const uint32 Flag = static_cast<uint32>(Binding.StateFlag);
		if (Flag == 0 || (Flag & (Flag - 1)) != 0)
		{
			UE_LOG(LogMassbility, Error, TEXT("State tag mapping '%s' contains a non-single state flag."), *StateTagMappingAsset->GetPathName());
			return false;
		}
		FMassbilityTagMask Mask;
		if (!RuntimeTagCatalog.BuildMask(Binding.GrantedTags, Mask, Error))
		{
			UE_LOG(LogMassbility, Error, TEXT("Failed to compile state tag mapping '%s': %s"), *StateTagMappingAsset->GetPathName(), *Error);
			return false;
		}
		StateTagMasks.Add(Flag, Mask);
	}
	return true;
}

bool UMassbilitySubsystem::RegisterSchema(const UMassbilityAttributeSchema& Schema, uint16& OutSchemaId)
{
	if (const uint16* Existing = SchemaIds.Find(&Schema))
	{
		OutSchemaId = *Existing;
		return true;
	}
	if (CompiledSchemas.Num() >= Massbility::InvalidId)
	{
		UE_LOG(LogMassbility, Error, TEXT("Massbility schema registry exhausted."));
		return false;
	}

	FMassbilityCompiledAttributeSchema Compiled;
	FString Error;
	if (!Schema.Compile(Compiled, Error))
	{
		UE_LOG(LogMassbility, Error, TEXT("Failed to compile attribute schema '%s': %s"), *Schema.GetPathName(), *Error);
		return false;
	}
	OutSchemaId = static_cast<uint16>(CompiledSchemas.Num());
	Compiled.SchemaId = OutSchemaId;
	CompiledSchemas.Add(Compiled);
	SchemaIds.Add(&Schema, OutSchemaId);
	return true;
}

bool UMassbilitySubsystem::RegisterUnitDefinition(const UMassbilityUnitDefinition& UnitDefinition, uint16& OutUnitDefinitionId, uint16& OutSchemaId)
{
	if (const uint16* Existing = UnitDefinitionIds.Find(&UnitDefinition))
	{
		OutUnitDefinitionId = *Existing;
		const UMassbilityUnitDefinition* ExistingDefinition = UnitDefinitions[*Existing];
		return ExistingDefinition && ExistingDefinition->GetAttributeSchema() && RegisterSchema(*ExistingDefinition->GetAttributeSchema(), OutSchemaId);
	}
	UMassbilityAttributeSchema* Schema = UnitDefinition.GetAttributeSchema();
	if (!Schema || !RegisterSchema(*Schema, OutSchemaId))
	{
		UE_LOG(LogMassbility, Error, TEXT("Unit definition '%s' requires a valid attribute schema."), *UnitDefinition.GetPathName());
		return false;
	}
	if (UnitDefinitions.Num() >= Massbility::InvalidId)
	{
		UE_LOG(LogMassbility, Error, TEXT("Massbility unit definition registry exhausted."));
		return false;
	}
	OutUnitDefinitionId = static_cast<uint16>(UnitDefinitions.Num());
	UnitDefinitions.Add(const_cast<UMassbilityUnitDefinition*>(&UnitDefinition));
	UnitDefinitionIds.Add(&UnitDefinition, OutUnitDefinitionId);
	return true;
}

bool UMassbilitySubsystem::BuildStateMask(const EMassbilityStateFlags Flags, FMassbilityTagMask& OutMask) const
{
	OutMask.Reset();
	for (const TPair<uint32, FMassbilityTagMask>& Pair : StateTagMasks)
	{
		if ((static_cast<uint32>(Flags) & Pair.Key) != 0)
		{
			OutMask.Append(Pair.Value);
		}
	}
	return true;
}

FMassArchetypeHandle UMassbilitySubsystem::GetOrCreateUnitArchetype()
{
	if (!EntitySubsystem)
	{
		return {};
	}
	if (!UnitArchetype.IsValid())
	{
		FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
		const TArray<const UScriptStruct*> UnitFragments = {
			FMassbilityUnitFragment::StaticStruct(),
			FMassbilityVitalFragment::StaticStruct(),
			FMassbilityAttributeFragment::StaticStruct(),
			FMassbilityStateFragment::StaticStruct(),
		};
		UnitArchetype = EntityManager.CreateArchetype(UnitFragments);
	}
	return UnitArchetype;
}
