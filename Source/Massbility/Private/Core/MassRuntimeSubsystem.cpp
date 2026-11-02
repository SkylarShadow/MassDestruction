// Copyright MassDestruction. All Rights Reserved.

#include "Core/MassRuntimeSubsystem.h"

#include "Core/MassRuntimeSettings.h"
#include "Attributes/MassAttributeAccess.h"
#include "Engine/Engine.h"
#include "Entity/MassEntityFactory.h"
#include "Entity/MassFragments.h"
#include "Entity/MassUnitDefinition.h"
#include "Mass.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Mass/EntityFragments.h"
#include "Movement/MassForcedMovementFragments.h"

void UMassRuntimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	if (!EntitySubsystem)
	{
		UE_LOG(LogMassbility, Error, TEXT("Unable to resolve UMassEntitySubsystem."));
		return;
	}

	CompileSettings();
	const UMassRuntimeSettings* Settings = GetDefault<UMassRuntimeSettings>();
	SimulationClock.Initialize(GetWorld(), Settings ? Settings->SimulationTicksPerSecond : 20);
	const int32 TargetingRate = Settings ? FMath::Clamp(Settings->TargetingTicksPerSecond, 1, 60) : 10;
	TargetRefreshTicks = FMath::Max(1u, SimulationClock.GetTicksPerSecond() / static_cast<uint32>(TargetingRate));
	SpatialCellSize = Settings ? FMath::Max(50.0f, Settings->SpatialCellSize) : 400.0f;
}

void UMassRuntimeSubsystem::Deinitialize()
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

bool UMassRuntimeSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

UMassRuntimeSubsystem* UMassRuntimeSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassRuntimeSubsystem>();
	}
	return nullptr;
}

FMassEntityHandle UMassRuntimeSubsystem::CreateUnit(const UMassUnitDefinition& UnitDefinition)
{
	return CreateUnit(UnitDefinition, FTransform::Identity);
}

FMassEntityHandle UMassRuntimeSubsystem::CreateUnit(const UMassUnitDefinition& UnitDefinition, const FTransform& Transform)
{
	return CreateUnit(UnitDefinition, Transform, INDEX_NONE);
}

FMassEntityHandle UMassRuntimeSubsystem::CreateUnit(const UMassUnitDefinition& UnitDefinition,
	const FTransform& Transform, const int16 TeamIdOverride)
{
	return FMassEntityFactory::Create(*this, UnitDefinition, Transform, TeamIdOverride);
}

bool UMassRuntimeSubsystem::DestroyUnit(const FMassEntityHandle Entity)
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
bool UMassRuntimeSubsystem::ResolveAttributeHandle(const UMassAttributeSchema& Schema, const FGameplayTag AttributeTag, FMassAttributeHandle& OutHandle) const
{
	return FMassAttributeAccess::ResolveHandle(*this, Schema, AttributeTag, OutHandle);
}

bool UMassRuntimeSubsystem::ResolveAttributeHandle(const uint16 SchemaId, const FGameplayTag AttributeTag, FMassAttributeHandle& OutHandle) const
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
			OutHandle.Storage = EMassAttributeStorage::Vital;
			OutHandle.Index = Index;
			return true;
		}
	}
	const FMassCompiledAttributeSchema* Schema = FindCompiledSchema(SchemaId);
	if (!Schema)
	{
		return false;
	}
	for (uint8 Index = 0; Index < Schema->AttributeCount; ++Index)
	{
		if (Schema->Attributes[Index].AttributeTag == AttributeTag)
		{
			OutHandle.Storage = EMassAttributeStorage::Extended;
			OutHandle.Index = Index;
			return true;
		}
	}
	return false;
}

bool UMassRuntimeSubsystem::GetAttribute(const FMassEntityHandle Entity, const FMassAttributeHandle Handle, float& OutValue) const
{
	return EntitySubsystem && FMassAttributeAccess::Get(*this, EntitySubsystem->GetEntityManager(), Entity, Handle, OutValue);
}

bool UMassRuntimeSubsystem::SetAttribute(const FMassEntityHandle Entity, const FMassAttributeHandle Handle, const float Value)
{
	return EntitySubsystem && FMassAttributeAccess::SetClamped(*this, EntitySubsystem->GetMutableEntityManager(), Entity, Handle, Value);
}

bool UMassRuntimeSubsystem::AddAttribute(const FMassEntityHandle Entity, const FMassAttributeHandle Handle, const float Delta)
{
	return EntitySubsystem && FMassAttributeAccess::Add(*this, EntitySubsystem->GetMutableEntityManager(), Entity, Handle, Delta);
}

bool UMassRuntimeSubsystem::GetExtendedAttribute(const FMassEntityHandle Entity, const FMassAttributeHandle Handle, float& OutValue) const
{
	OutValue = 0.0f;
	if (!EntitySubsystem || !Handle.IsValid() || Handle.Storage != EMassAttributeStorage::Extended)
	{
		return false;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	if (!EntityManager.IsEntityValid(Entity))
	{
		return false;
	}
	const FMassAttributeFragment* Attributes = EntityManager.GetFragmentDataPtr<FMassAttributeFragment>(Entity);
	if (!Attributes || Handle.Index >= Attributes->AttributeCount)
	{
		return false;
	}
	OutValue = Attributes->Values[Handle.Index];
	return true;
}

bool UMassRuntimeSubsystem::SetExtendedAttribute(const FMassEntityHandle Entity, const FMassAttributeHandle Handle, const float Value)
{
	return Handle.Storage == EMassAttributeStorage::Extended && SetAttribute(Entity, Handle, Value);
}

bool UMassRuntimeSubsystem::AddExtendedAttribute(const FMassEntityHandle Entity, const FMassAttributeHandle Handle, const float Delta)
{
	return Handle.Storage == EMassAttributeStorage::Extended && AddAttribute(Entity, Handle, Delta);
}

bool UMassRuntimeSubsystem::SetStateFlags(const FMassEntityHandle Entity, const EMassStateFlags NewFlags)
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
	FMassStateFragment* State = EntityManager.GetFragmentDataPtr<FMassStateFragment>(Entity);
	if (!State)
	{
		return false;
	}
	FMassTagMask StateMask;
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

bool UMassRuntimeSubsystem::RebuildRuntimeTags(const FMassEntityHandle Entity)
{
	if (!EntitySubsystem)
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	FMassStateFragment* State = EntityManager.GetFragmentDataPtr<FMassStateFragment>(Entity);
	if (!State)
	{
		return false;
	}
	// 该接口用于 Effect 或其他系统修改 Base/Effect Tags 后的显式同步。
	FMassTagMask StateMask;
	BuildStateMask(State->StateFlags, StateMask);
	State->RuntimeTags = State->BaseTags;
	State->RuntimeTags.Append(StateMask);
	State->RuntimeTags.Append(State->EffectTags);
	return true;
}

const FMassCompiledAttributeSchema* UMassRuntimeSubsystem::FindCompiledSchema(const uint16 SchemaId) const
{
	return CompiledSchemas.IsValidIndex(SchemaId) ? &CompiledSchemas[SchemaId] : nullptr;
}

bool UMassRuntimeSubsystem::HasRuntimeTags(const FMassEntityHandle Entity, const FMassTagMask& Required, const FMassTagMask& Blocked) const
{
	if (!EntitySubsystem)
	{
		return false;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	const FMassStateFragment* State = EntityManager.GetFragmentDataPtr<FMassStateFragment>(Entity);
	return State && State->RuntimeTags.ContainsAll(Required) && !State->RuntimeTags.ContainsAny(Blocked);
}

bool UMassRuntimeSubsystem::CompileSettings()
{
	const UMassRuntimeSettings* Settings = GetDefault<UMassRuntimeSettings>();
	VitalAttributeTags[static_cast<uint8>(EMassVitalAttribute::Health)] = Settings ? Settings->HealthAttributeTag : FGameplayTag();
	VitalAttributeTags[static_cast<uint8>(EMassVitalAttribute::MaxHealth)] = Settings ? Settings->MaxHealthAttributeTag : FGameplayTag();
	VitalAttributeTags[static_cast<uint8>(EMassVitalAttribute::MoveSpeed)] = Settings ? Settings->MoveSpeedAttributeTag : FGameplayTag();
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
		UE_LOG(LogMassbility, Warning, TEXT("No RuntimeTagCatalog is configured. Mass unit creation is unavailable until one is assigned in Project Settings."));
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
	for (const FMassStateTagBinding& Binding : StateTagMappingAsset->GetBindings())
	{
		const uint32 Flag = static_cast<uint32>(Binding.StateFlag);
		if (Flag == 0 || (Flag & (Flag - 1)) != 0)
		{
			UE_LOG(LogMassbility, Error, TEXT("State tag mapping '%s' contains a non-single state flag."), *StateTagMappingAsset->GetPathName());
			return false;
		}
		FMassTagMask Mask;
		if (!RuntimeTagCatalog.BuildMask(Binding.GrantedTags, Mask, Error))
		{
			UE_LOG(LogMassbility, Error, TEXT("Failed to compile state tag mapping '%s': %s"), *StateTagMappingAsset->GetPathName(), *Error);
			return false;
		}
		StateTagMasks.Add(Flag, Mask);
	}
	return true;
}

bool UMassRuntimeSubsystem::RegisterSchema(const UMassAttributeSchema& Schema, uint16& OutSchemaId)
{
	if (const uint16* Existing = SchemaIds.Find(&Schema))
	{
		OutSchemaId = *Existing;
		return true;
	}
	if (CompiledSchemas.Num() >= Massbility::InvalidId)
	{
		UE_LOG(LogMassbility, Error, TEXT("Mass schema registry exhausted."));
		return false;
	}

	// Schema 编译一次后缓存，避免热路径遍历 UObject 和作者数组。
	FMassCompiledAttributeSchema Compiled;
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

bool UMassRuntimeSubsystem::RegisterUnitDefinition(const UMassUnitDefinition& UnitDefinition, uint16& OutUnitDefinitionId, uint16& OutSchemaId)
{
	if (const uint16* Existing = UnitDefinitionIds.Find(&UnitDefinition))
	{
		OutUnitDefinitionId = *Existing;
		const UMassUnitDefinition* ExistingDefinition = UnitDefinitions[*Existing];
		return ExistingDefinition && ExistingDefinition->GetAttributeSchema() && RegisterSchema(*ExistingDefinition->GetAttributeSchema(), OutSchemaId);
	}
	// Unit Definition 必须先绑定 Schema，实体初始化阶段只携带紧凑 ID。
	UMassAttributeSchema* Schema = UnitDefinition.GetAttributeSchema();
	if (!Schema || !RegisterSchema(*Schema, OutSchemaId))
	{
		UE_LOG(LogMassbility, Error, TEXT("Unit definition '%s' requires a valid attribute schema."), *UnitDefinition.GetPathName());
		return false;
	}
	if (UnitDefinitions.Num() >= Massbility::InvalidId)
	{
		UE_LOG(LogMassbility, Error, TEXT("Mass unit definition registry exhausted."));
		return false;
	}
	OutUnitDefinitionId = static_cast<uint16>(UnitDefinitions.Num());
	UnitDefinitions.Add(const_cast<UMassUnitDefinition*>(&UnitDefinition));
	UnitDefinitionIds.Add(&UnitDefinition, OutUnitDefinitionId);
	return true;
}

bool UMassRuntimeSubsystem::BuildStateMask(const EMassStateFlags Flags, FMassTagMask& OutMask) const
{
	OutMask.Reset();
	// 逐个检查 Flags 中的 bit，并把对应映射追加到结果 Mask。
	for (const TPair<uint32, FMassTagMask>& Pair : StateTagMasks)
	{
		if ((static_cast<uint32>(Flags) & Pair.Key) != 0)
		{
			OutMask.Append(Pair.Value);
		}
	}
	return true;
}

FMassArchetypeHandle UMassRuntimeSubsystem::GetOrCreateUnitArchetype()
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
			FMassUnitFragment::StaticStruct(),
			FMassVitalFragment::StaticStruct(),
			FMassAttributeFragment::StaticStruct(),
			FMassStateFragment::StaticStruct(),
			FMassCombatFragment::StaticStruct(),
			FMassActiveEffectFragment::StaticStruct(),
			FMassAbilityFragment::StaticStruct(),
			FMassTargetFragment::StaticStruct(),
			FMassForcedMovementFragment::StaticStruct(),
			FTransformFragment::StaticStruct(),
		};
		UnitArchetype = EntityManager.CreateArchetype(UnitFragments);
	}
	return UnitArchetype;
}
