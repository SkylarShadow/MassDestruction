// Copyright MassDestruction. All Rights Reserved.

#include "Core/MassbilitySubsystem.h"

#include "Core/MassbilitySettings.h"
#include "Attributes/MassbilityAttributeAccess.h"
#include "Engine/Engine.h"
#include "Entity/MassbilityEntityFactory.h"
#include "Entity/MassbilityFragments.h"
#include "Entity/MassbilityUnitDefinition.h"
#include "Massbility.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Mass/EntityFragments.h"
#include "Movement/MassbilityForcedMovementFragments.h"

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
	const UMassbilitySettings* Settings = GetDefault<UMassbilitySettings>();
	SimulationClock.Initialize(GetWorld(), Settings ? Settings->SimulationTicksPerSecond : 20);
	const int32 TargetingRate = Settings ? FMath::Clamp(Settings->TargetingTicksPerSecond, 1, 60) : 10;
	TargetRefreshTicks = FMath::Max(1u, SimulationClock.GetTicksPerSecond() / static_cast<uint32>(TargetingRate));
	SpatialCellSize = Settings ? FMath::Max(50.0f, Settings->SpatialCellSize) : 400.0f;
}

void UMassbilitySubsystem::Deinitialize()
{
	UnitArchetype = {};
	SimulationClock.Reset();
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
	return CreateUnit(UnitDefinition, FTransform::Identity);
}

FMassEntityHandle UMassbilitySubsystem::CreateUnit(const UMassbilityUnitDefinition& UnitDefinition, const FTransform& Transform)
{
	return FMassbilityEntityFactory::Create(*this, UnitDefinition, Transform);
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

// 先查固定 Vital 字段，再查 Unit Definition 对应 Schema 中的 Extended 字段。
bool UMassbilitySubsystem::ResolveAttributeHandle(const UMassbilityAttributeSchema& Schema, const FGameplayTag AttributeTag, FMassbilityAttributeHandle& OutHandle) const
{
	return FMassbilityAttributeAccess::ResolveHandle(*this, Schema, AttributeTag, OutHandle);
}

bool UMassbilitySubsystem::ResolveAttributeHandle(const uint16 SchemaId, const FGameplayTag AttributeTag, FMassbilityAttributeHandle& OutHandle) const
{
	OutHandle = {};
	if (!AttributeTag.IsValid())
	{
		return false;
	}
	// Vital 字段不属于某个独立 Schema，而是所有单位共享的固定存储。
	for (uint8 Index = 0; Index < VitalAttributeTags.Num(); ++Index)
	{
		if (VitalAttributeTags[Index].IsValid() && VitalAttributeTags[Index] == AttributeTag)
		{
			OutHandle.Storage = EMassbilityAttributeStorage::Vital;
			OutHandle.Index = Index;
			return true;
		}
	}
	const FMassbilityCompiledAttributeSchema* Schema = FindCompiledSchema(SchemaId);
	if (!Schema)
	{
		return false;
	}
	for (uint8 Index = 0; Index < Schema->AttributeCount; ++Index)
	{
		if (Schema->Attributes[Index].AttributeTag == AttributeTag)
		{
			OutHandle.Storage = EMassbilityAttributeStorage::Extended;
			OutHandle.Index = Index;
			return true;
		}
	}
	return false;
}

bool UMassbilitySubsystem::GetAttribute(const FMassEntityHandle Entity, const FMassbilityAttributeHandle Handle, float& OutValue) const
{
	return EntitySubsystem && FMassbilityAttributeAccess::Get(*this, EntitySubsystem->GetEntityManager(), Entity, Handle, OutValue);
}

bool UMassbilitySubsystem::SetAttribute(const FMassEntityHandle Entity, const FMassbilityAttributeHandle Handle, const float Value)
{
	return EntitySubsystem && FMassbilityAttributeAccess::SetClamped(*this, EntitySubsystem->GetMutableEntityManager(), Entity, Handle, Value);
}

bool UMassbilitySubsystem::AddAttribute(const FMassEntityHandle Entity, const FMassbilityAttributeHandle Handle, const float Delta)
{
	return EntitySubsystem && FMassbilityAttributeAccess::Add(*this, EntitySubsystem->GetMutableEntityManager(), Entity, Handle, Delta);
}

bool UMassbilitySubsystem::GetExtendedAttribute(const FMassEntityHandle Entity, const FMassbilityAttributeHandle Handle, float& OutValue) const
{
	OutValue = 0.0f;
	if (!EntitySubsystem || !Handle.IsValid() || Handle.Storage != EMassbilityAttributeStorage::Extended)
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
	return Handle.Storage == EMassbilityAttributeStorage::Extended && SetAttribute(Entity, Handle, Value);
}

bool UMassbilitySubsystem::AddExtendedAttribute(const FMassEntityHandle Entity, const FMassbilityAttributeHandle Handle, const float Delta)
{
	return Handle.Storage == EMassbilityAttributeStorage::Extended && AddAttribute(Entity, Handle, Delta);
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
	// RuntimeTags 是派生缓存，任何状态变化都必须同步重建，避免查询读到旧状态。
	State->StateFlags = NewFlags;
	State->RuntimeTags = State->BaseTags;
	State->RuntimeTags.Append(StateMask);
	State->RuntimeTags.Append(State->EffectTags);
	return true;
}

bool UMassbilitySubsystem::RebuildRuntimeTags(const FMassEntityHandle Entity)
{
	if (!EntitySubsystem)
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	FMassbilityStateFragment* State = EntityManager.GetFragmentDataPtr<FMassbilityStateFragment>(Entity);
	if (!State)
	{
		return false;
	}
	// 该接口用于 Effect 或其他系统修改 Base/Effect Tags 后的显式同步。
	FMassbilityTagMask StateMask;
	BuildStateMask(State->StateFlags, StateMask);
	State->RuntimeTags = State->BaseTags;
	State->RuntimeTags.Append(StateMask);
	State->RuntimeTags.Append(State->EffectTags);
	return true;
}

const FMassbilityCompiledAttributeSchema* UMassbilitySubsystem::FindCompiledSchema(const uint16 SchemaId) const
{
	return CompiledSchemas.IsValidIndex(SchemaId) ? &CompiledSchemas[SchemaId] : nullptr;
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
	VitalAttributeTags[static_cast<uint8>(EMassbilityVitalAttribute::Health)] = Settings ? Settings->HealthAttributeTag : FGameplayTag();
	VitalAttributeTags[static_cast<uint8>(EMassbilityVitalAttribute::MaxHealth)] = Settings ? Settings->MaxHealthAttributeTag : FGameplayTag();
	VitalAttributeTags[static_cast<uint8>(EMassbilityVitalAttribute::MoveSpeed)] = Settings ? Settings->MoveSpeedAttributeTag : FGameplayTag();
	for (int32 Left = 0; Left < VitalAttributeTags.Num(); ++Left)
	{
		if (!VitalAttributeTags[Left].IsValid())
		{
			UE_LOG(LogMassbility, Warning, TEXT("Vital attribute mapping %d is not configured; effects cannot address that vital by tag."), Left);
			continue;
		}
		for (int32 Right = Left + 1; Right < VitalAttributeTags.Num(); ++Right)
		{
			if (VitalAttributeTags[Left] == VitalAttributeTags[Right])
			{
				UE_LOG(LogMassbility, Error, TEXT("Vital attribute tag '%s' is assigned to more than one vital field."), *VitalAttributeTags[Left].ToString());
				return false;
			}
		}
	}
	// 资产只在 World 初始化时同步加载；运行时实体不再读取 Data Asset。
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

	// 资产中只允许配置单个 StateFlag，组合状态由运行时按位合并。
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

	// Schema 编译一次后缓存，避免热路径遍历 UObject 和作者数组。
	FMassbilityCompiledAttributeSchema Compiled;
	FString Error;
	if (!Schema.Compile(Compiled, Error))
	{
		UE_LOG(LogMassbility, Error, TEXT("Failed to compile attribute schema '%s': %s"), *Schema.GetPathName(), *Error);
		return false;
	}
	for (uint8 AttributeIndex = 0; AttributeIndex < Compiled.AttributeCount; ++AttributeIndex)
	{
		for (const FGameplayTag VitalTag : VitalAttributeTags)
		{
			if (VitalTag.IsValid() && Compiled.Attributes[AttributeIndex].AttributeTag == VitalTag)
			{
				UE_LOG(LogMassbility, Error, TEXT("Schema '%s' redeclares vital attribute '%s' as an extended attribute."),
					*Schema.GetPathName(), *VitalTag.ToString());
				return false;
			}
		}
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
	// Unit Definition 必须先绑定 Schema，实体初始化阶段只携带紧凑 ID。
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
	// 逐个检查 Flags 中的 bit，并把对应映射追加到结果 Mask。
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
		// 所有普通单位共享同一 Fragment 布局；首次使用时才向 EntityManager 注册。
		FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
		const TArray<const UScriptStruct*> UnitFragments = {
			FMassbilityUnitFragment::StaticStruct(),
			FMassbilityVitalFragment::StaticStruct(),
			FMassbilityAttributeFragment::StaticStruct(),
			FMassbilityStateFragment::StaticStruct(),
			FMassbilityCombatFragment::StaticStruct(),
			FMassbilityActiveEffectFragment::StaticStruct(),
			FMassbilityAbilityFragment::StaticStruct(),
			FMassbilityTargetFragment::StaticStruct(),
			FMassbilityForcedMovementFragment::StaticStruct(),
			FTransformFragment::StaticStruct(),
		};
		UnitArchetype = EntityManager.CreateArchetype(UnitFragments);
	}
	return UnitArchetype;
}
