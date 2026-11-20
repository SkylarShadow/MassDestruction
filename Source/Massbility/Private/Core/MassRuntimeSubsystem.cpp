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
#include "Targeting/MassUnitHitShapeFragment.h"

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
	InitialValuesByUnit.Reset();
	UnitDefinitionIds.Reset();
	CompiledLayouts.Reset();
	LayoutIds.Reset();
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

// 使用单位定义创建默认 Transform 的 Mass 实体。
FMassEntityHandle UMassRuntimeSubsystem::CreateUnit(const UMassUnitDefinition& UnitDefinition)
{
	// 使用单位定义创建实体，并初始化其 Fragment、属性和能力 Loadout。
	return CreateUnit(UnitDefinition, FTransform::Identity);
}

// 使用指定 Transform 创建并初始化 Mass 单位实体。
FMassEntityHandle UMassRuntimeSubsystem::CreateUnit(const UMassUnitDefinition& UnitDefinition, const FTransform& Transform)
{
	return CreateUnit(UnitDefinition, Transform, INDEX_NONE);
}

FMassEntityHandle UMassRuntimeSubsystem::CreateUnit(const UMassUnitDefinition& UnitDefinition,
	const FTransform& Transform, const int16 TeamIdOverride)
{
	return FMassEntityFactory::Create(*this, UnitDefinition, Transform, TeamIdOverride);
}

FMassEntityHandle UMassRuntimeSubsystem::CreateUnitWithoutAbilities(
	const UMassUnitDefinition& UnitDefinition, const FTransform& Transform)
{
	return FMassEntityFactory::Create(*this, UnitDefinition, Transform, INDEX_NONE, false);
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

// 按单位已注册的属性布局解析 Tag。
bool UMassRuntimeSubsystem::ResolveAttributeHandle(const UMassUnitDefinition& UnitDefinition, const FGameplayTag AttributeTag, FMassAttributeHandle& OutHandle) const
{
	return FMassAttributeAccess::ResolveHandle(*this, UnitDefinition, AttributeTag, OutHandle);
}

// 根据 LayoutId 解析属性句柄。
bool UMassRuntimeSubsystem::ResolveAttributeHandle(const uint16 LayoutId, const FGameplayTag AttributeTag, FMassAttributeHandle& OutHandle) const
{
	OutHandle = {};
	if (!AttributeTag.IsValid())
	{
		return false;
	}
	// Vital 字段由所有单位共享，不占扩展属性槽位。
	for (uint8 Index = 0; Index < VitalAttributeTags.Num(); ++Index)
	{
		if (VitalAttributeTags[Index].IsValid() && VitalAttributeTags[Index] == AttributeTag)
		{
			OutHandle.Storage = EMassAttributeStorage::Vital;
			OutHandle.Index = Index;
			return true;
		}
	}
	const FMassCompiledAttributeLayout* Layout = FindCompiledLayout(LayoutId);
	if (!Layout)
	{
		return false;
	}
	for (uint8 Index = 0; Index < Layout->AttributeCount; ++Index)
	{
		if (Layout->Attributes[Index].AttributeTag == AttributeTag)
		{
			OutHandle.Storage = EMassAttributeStorage::Extended;
			OutHandle.Index = Index;
			return true;
		}
	}
	return false;
}

// 读取实体固定属性数组中的一个属性值。
bool UMassRuntimeSubsystem::GetAttribute(const FMassEntityHandle Entity, const FMassAttributeHandle Handle, float& OutValue) const
{
	return EntitySubsystem && FMassAttributeAccess::Get(*this, EntitySubsystem->GetEntityManager(), Entity, Handle, OutValue);
}

// 设置实体固定属性数组中的一个属性值。
bool UMassRuntimeSubsystem::SetAttribute(const FMassEntityHandle Entity, const FMassAttributeHandle Handle, const float Value)
{
	return EntitySubsystem && FMassAttributeAccess::SetClamped(*this, EntitySubsystem->GetMutableEntityManager(), Entity, Handle, Value);
}

// 对实体固定属性执行增量修改。
bool UMassRuntimeSubsystem::AddAttribute(const FMassEntityHandle Entity, const FMassAttributeHandle Handle, const float Delta)
{
	return EntitySubsystem && FMassAttributeAccess::Add(*this, EntitySubsystem->GetMutableEntityManager(), Entity, Handle, Delta);
}

// 设置实体状态标志(Dead AbilityBlocked 之类)，并同步影响状态 Tag 的运行时数据
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
	// RuntimeTags 是派生缓存，任何状态变化都必须同步重建，避免查询读到旧状态
	State->StateFlags = NewFlags;
	State->RuntimeTags = State->BaseTags;
	State->RuntimeTags.Append(StateMask);
	State->RuntimeTags.Append(State->EffectTags);
	return true;
}

// 将基础 Tag、Effect Tag 和状态 Tag 合并成 RuntimeTags。
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

// 根据 LayoutId 查找编译后的属性布局。
const FMassCompiledAttributeLayout* UMassRuntimeSubsystem::FindCompiledLayout(const uint16 LayoutId) const
{
	return CompiledLayouts.IsValidIndex(LayoutId) ? &CompiledLayouts[LayoutId] : nullptr;
}

// 判断实体是否满足必需 Tag 且不包含阻塞 Tag
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

// 编译运行时设置并建立全局 Tag、状态和属性索引
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

// 编译单位属性配置，复用相同的布局；保留本单位的默认值用于初始化。
bool UMassRuntimeSubsystem::RegisterLayout(const UMassUnitDefinition& UnitDefinition, uint16& OutLayoutId, FMassCompiledAttributeLayout& OutUnitLayout)
{
	if (CompiledLayouts.Num() >= Massbility::InvalidId)
	{
		UE_LOG(LogMassbility, Error, TEXT("Mass attribute layout registry exhausted."));
		return false;
	}

	FString Error;
	if (!FMassAttributeLayoutCompiler::Compile(UnitDefinition.GetAttributeSets(), OutUnitLayout, Error))
	{
		UE_LOG(LogMassbility, Error, TEXT("Failed to compile attributes for '%s': %s"), *UnitDefinition.GetPathName(), *Error);
		return false;
	}
	for (uint8 AttributeIndex = 0; AttributeIndex < OutUnitLayout.AttributeCount; ++AttributeIndex)
	{
		for (const FGameplayTag VitalTag : VitalAttributeTags)
		{
			if (VitalTag.IsValid() && OutUnitLayout.Attributes[AttributeIndex].AttributeTag == VitalTag)
			{
				UE_LOG(LogMassbility, Error, TEXT("Unit '%s' redeclares vital attribute '%s' as an extended attribute."),
					*UnitDefinition.GetPathName(), *VitalTag.ToString());
				return false;
			}
		}
	}
	// Initial values are unit-specific and do not affect layout identity.
	for (const FMassCompiledAttributeLayout& Existing : CompiledLayouts)
	{
		if (Existing.AttributeCount != OutUnitLayout.AttributeCount)
		{
			continue;
		}
		bool bMatches = true;
		for (uint8 Index = 0; Index < OutUnitLayout.AttributeCount; ++Index)
		{
			const FMassCompiledAttribute& ExistingAttribute = Existing.Attributes[Index];
			const FMassCompiledAttribute& UnitAttribute = OutUnitLayout.Attributes[Index];
			if (ExistingAttribute.AttributeTag != UnitAttribute.AttributeTag
				|| ExistingAttribute.MinValue != UnitAttribute.MinValue
				|| ExistingAttribute.MaxValue != UnitAttribute.MaxValue)
			{
				bMatches = false;
				break;
			}
		}
		if (bMatches)
		{
			OutLayoutId = Existing.LayoutId;
			LayoutIds.Add(&UnitDefinition, OutLayoutId);
			return true;
		}
	}
	OutLayoutId = static_cast<uint16>(CompiledLayouts.Num());
	OutUnitLayout.LayoutId = OutLayoutId;
	CompiledLayouts.Add(OutUnitLayout);
	LayoutIds.Add(&UnitDefinition, OutLayoutId);
	return true;
}

// 注册单位定义并编译其初始属性。
bool UMassRuntimeSubsystem::RegisterUnitDefinition(const UMassUnitDefinition& UnitDefinition, uint16& OutUnitDefinitionId, uint16& OutLayoutId)
{
	if (const uint16* Existing = UnitDefinitionIds.Find(&UnitDefinition))
	{
		OutUnitDefinitionId = *Existing;
		if (const uint16* ExistingLayout = LayoutIds.Find(&UnitDefinition))
		{
			OutLayoutId = *ExistingLayout;
			return true;
		}
		return false;
	}
	FMassCompiledAttributeLayout UnitLayout;
	if (!RegisterLayout(UnitDefinition, OutLayoutId, UnitLayout))
	{
		return false;
	}
	if (UnitDefinitions.Num() >= Massbility::InvalidId)
	{
		UE_LOG(LogMassbility, Error, TEXT("Mass unit definition registry exhausted."));
		return false;
	}
	TStaticArray<float, Massbility::MaxExtendedAttributes> InitialValues = {};
	for (uint8 Index = 0; Index < UnitLayout.AttributeCount; ++Index)
	{
		InitialValues[Index] = UnitLayout.Attributes[Index].DefaultValue;
	}
	TSet<FGameplayTag> SeenOverrides;
	for (const FMassAttributeInitialValue& Override : UnitDefinition.GetInitialAttributeValues())
	{
		if (!Override.AttributeTag.IsValid() || !FMath::IsFinite(Override.Value) || SeenOverrides.Contains(Override.AttributeTag))
		{
			UE_LOG(LogMassbility, Error, TEXT("Invalid or duplicate initial attribute in '%s'."), *UnitDefinition.GetPathName());
			return false;
		}
		SeenOverrides.Add(Override.AttributeTag);
		FMassAttributeHandle Handle;
		if (!ResolveAttributeHandle(OutLayoutId, Override.AttributeTag, Handle)
			|| Handle.Storage != EMassAttributeStorage::Extended)
		{
			UE_LOG(LogMassbility, Error, TEXT("Initial attribute '%s' is not extended in '%s'."),
				*Override.AttributeTag.ToString(), *UnitDefinition.GetPathName());
			return false;
		}
		const FMassCompiledAttribute& Attribute = UnitLayout.Attributes[Handle.Index];
		InitialValues[Handle.Index] = FMath::Clamp(Override.Value, Attribute.MinValue, Attribute.MaxValue);
	}
	OutUnitDefinitionId = static_cast<uint16>(UnitDefinitions.Num());
	UnitDefinitions.Add(const_cast<UMassUnitDefinition*>(&UnitDefinition));
	InitialValuesByUnit.Add(InitialValues);
	UnitDefinitionIds.Add(&UnitDefinition, OutUnitDefinitionId);
	return true;
}

// 将状态枚举转换为运行时状态 Tag Mask。
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

// 延迟创建并复用普通单位实体的 Archetype。
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
			FMassUnitHitShapeFragment::StaticStruct(),
			FMassForcedMovementFragment::StaticStruct(),
			FTransformFragment::StaticStruct(),
		};
		UnitArchetype = EntityManager.CreateArchetype(UnitFragments);
	}
	return UnitArchetype;
}
