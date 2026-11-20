// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Attributes/MassAttributeSet.h"
#include "Core/MassTypes.h"
#include "Core/MassSimulationClock.h"
#include "Mass/EntityHandle.h"
#include "MassArchetypeTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tags/MassRuntimeTagCatalog.h"
#include "Tags/MassStateTagMapping.h"

#include "MassRuntimeSubsystem.generated.h"

class UMassEntitySubsystem;
class UMassEffectSubsystem;
class UMassDeathProcessor;
class UMassRuntimeTagCatalog;
class UMassStateTagMapping;
class UMassUnitDefinition;
struct FMassEntityFactory;
struct FMassAttributeAccess;

UCLASS()
class MASSBILITY_API UMassRuntimeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassRuntimeSubsystem* Get(const UObject* WorldContextObject);

	FMassEntityHandle CreateUnit(const UMassUnitDefinition& UnitDefinition);
	FMassEntityHandle CreateUnit(const UMassUnitDefinition& UnitDefinition, const FTransform& Transform);
	/** TeamIdOverride 为 INDEX_NONE 时使用 UnitDefinition 的默认阵营。 */
	FMassEntityHandle CreateUnit(const UMassUnitDefinition& UnitDefinition, const FTransform& Transform, int16 TeamIdOverride);
	/** Spawn the same unit shape, vitals and tags without compiling or granting its ability loadout. */
	FMassEntityHandle CreateUnitWithoutAbilities(const UMassUnitDefinition& UnitDefinition, const FTransform& Transform);
	bool DestroyUnit(FMassEntityHandle Entity);

	/** 将作者配置的 GameplayTag 解析为固定 Vital 或 Extended 存储中的属性句柄。 */
	bool ResolveAttributeHandle(const UMassUnitDefinition& UnitDefinition, FGameplayTag AttributeTag, FMassAttributeHandle& OutHandle) const;
	bool ResolveAttributeHandle(uint16 LayoutId, FGameplayTag AttributeTag, FMassAttributeHandle& OutHandle) const;
	bool GetAttribute(FMassEntityHandle Entity, FMassAttributeHandle Handle, float& OutValue) const;
	bool SetAttribute(FMassEntityHandle Entity, FMassAttributeHandle Handle, float Value);
	bool AddAttribute(FMassEntityHandle Entity, FMassAttributeHandle Handle, float Delta);
	/** 设置状态位，并立即重建 Runtime Tag 缓存。 */
	bool SetStateFlags(FMassEntityHandle Entity, EMassStateFlags NewFlags);
	bool HasRuntimeTags(FMassEntityHandle Entity, const FMassTagMask& Required, const FMassTagMask& Blocked = {}) const;
	bool RebuildRuntimeTags(FMassEntityHandle Entity);
	const FMassCompiledAttributeLayout* FindCompiledLayout(uint16 LayoutId) const;
	const FMassCompiledRuntimeTagCatalog& GetRuntimeTagCatalog() const { return RuntimeTagCatalog; }
	const FMassSimulationClock& GetSimulationClock() const { return SimulationClock; }
	uint32 GetTargetRefreshTicks() const { return TargetRefreshTicks; }
	float GetSpatialCellSize() const { return SpatialCellSize; }

private:
	friend struct FMassEntityFactory;
	friend struct FMassAttributeAccess;
	friend class UMassEffectSubsystem;
	friend class UMassDeathProcessor;

	/** 读取 DeveloperSettings，并将资产数据编译为运行时可直接查询的结构。 */
	bool CompileSettings();
	/** 编译单位属性配置并复用相同的布局。 */
	bool RegisterLayout(const UMassUnitDefinition& UnitDefinition, uint16& OutLayoutId, FMassCompiledAttributeLayout& OutUnitLayout);
	/** 注册单位定义及其初始属性。 */
	bool RegisterUnitDefinition(const UMassUnitDefinition& UnitDefinition, uint16& OutUnitDefinitionId, uint16& OutLayoutId);
	/** 将组合 StateFlags 转换为对应 Runtime Tag Mask 的并集。 */
	bool BuildStateMask(EMassStateFlags Flags, FMassTagMask& OutMask) const;
	/** 延迟创建所有普通单位共享的 Fragment 布局。 */
	FMassArchetypeHandle GetOrCreateUnitArchetype();

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassRuntimeTagCatalog> RuntimeTagCatalogAsset;

	UPROPERTY(Transient)
	TObjectPtr<UMassStateTagMapping> StateTagMappingAsset;

	/** 资产编译后的紧凑 Tag 表，热路径只引用其中的 bit。 */
	FMassCompiledRuntimeTagCatalog RuntimeTagCatalog;
	/** 单位定义到共享属性布局的映射。 */
	TMap<const UMassUnitDefinition*, uint16> LayoutIds;
	TArray<FMassCompiledAttributeLayout> CompiledLayouts;
	TArray<TStaticArray<float, Massbility::MaxExtendedAttributes>> InitialValuesByUnit;
	TMap<const UMassUnitDefinition*, uint16> UnitDefinitionIds;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMassUnitDefinition>> UnitDefinitions;
	/** 单个 StateFlag 对应的 Tag Mask；组合状态在 BuildStateMask 中合并。 */
	TMap<uint32, FMassTagMask> StateTagMasks;
	TStaticArray<FGameplayTag, 3> VitalAttributeTags;
	FMassSimulationClock SimulationClock;
	uint32 TargetRefreshTicks = 2;
	float SpatialCellSize = 400.0f;
	FMassArchetypeHandle UnitArchetype;
};
