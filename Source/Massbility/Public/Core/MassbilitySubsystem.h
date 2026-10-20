// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Attributes/MassbilityAttributeSchema.h"
#include "Core/MassbilityTypes.h"
#include "Core/MassbilitySimulationClock.h"
#include "Mass/EntityHandle.h"
#include "MassArchetypeTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tags/MassbilityRuntimeTagCatalog.h"
#include "Tags/MassbilityStateTagMapping.h"

#include "MassbilitySubsystem.generated.h"

class UMassEntitySubsystem;
class UMassbilityEffectSubsystem;
class UMassbilityDeathProcessor;
class UMassbilityRuntimeTagCatalog;
class UMassbilityStateTagMapping;
class UMassbilityUnitDefinition;
struct FMassbilityEntityFactory;
struct FMassbilityAttributeAccess;

UCLASS()
class MASSBILITY_API UMassbilitySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassbilitySubsystem* Get(const UObject* WorldContextObject);

	FMassEntityHandle CreateUnit(const UMassbilityUnitDefinition& UnitDefinition);
	FMassEntityHandle CreateUnit(const UMassbilityUnitDefinition& UnitDefinition, const FTransform& Transform);
	bool DestroyUnit(FMassEntityHandle Entity);

	/** 将作者配置的 GameplayTag 解析为固定 Vital 或 Extended 存储中的属性句柄。 */
	bool ResolveAttributeHandle(const UMassbilityAttributeSchema& Schema, FGameplayTag AttributeTag, FMassbilityAttributeHandle& OutHandle) const;
	bool ResolveAttributeHandle(uint16 SchemaId, FGameplayTag AttributeTag, FMassbilityAttributeHandle& OutHandle) const;
	bool GetAttribute(FMassEntityHandle Entity, FMassbilityAttributeHandle Handle, float& OutValue) const;
	bool SetAttribute(FMassEntityHandle Entity, FMassbilityAttributeHandle Handle, float Value);
	bool AddAttribute(FMassEntityHandle Entity, FMassbilityAttributeHandle Handle, float Delta);
	/** 仅操作 Extended Attribute，防止 Vital 句柄被误用。 */
	bool GetExtendedAttribute(FMassEntityHandle Entity, FMassbilityAttributeHandle Handle, float& OutValue) const;
	bool SetExtendedAttribute(FMassEntityHandle Entity, FMassbilityAttributeHandle Handle, float Value);
	bool AddExtendedAttribute(FMassEntityHandle Entity, FMassbilityAttributeHandle Handle, float Delta);
	/** 设置状态位，并立即重建 Runtime Tag 缓存。 */
	bool SetStateFlags(FMassEntityHandle Entity, EMassbilityStateFlags NewFlags);
	bool HasRuntimeTags(FMassEntityHandle Entity, const FMassbilityTagMask& Required, const FMassbilityTagMask& Blocked = {}) const;
	bool RebuildRuntimeTags(FMassEntityHandle Entity);
	const FMassbilityCompiledAttributeSchema* FindCompiledSchema(uint16 SchemaId) const;
	const FMassbilityCompiledRuntimeTagCatalog& GetRuntimeTagCatalog() const { return RuntimeTagCatalog; }
	const FMassbilitySimulationClock& GetSimulationClock() const { return SimulationClock; }
	uint32 GetTargetRefreshTicks() const { return TargetRefreshTicks; }
	float GetSpatialCellSize() const { return SpatialCellSize; }

private:
	friend struct FMassbilityEntityFactory;
	friend struct FMassbilityAttributeAccess;
	friend class UMassbilityEffectSubsystem;
	friend class UMassbilityDeathProcessor;

	/** 读取 DeveloperSettings，并将资产数据编译为运行时可直接查询的结构。 */
	bool CompileSettings();
	/** 注册并缓存 Attribute Schema，返回紧凑 SchemaId。 */
	bool RegisterSchema(const UMassbilityAttributeSchema& Schema, uint16& OutSchemaId);
	/** 注册 Unit Definition 及其 Schema；实体热路径只保存 ID。 */
	bool RegisterUnitDefinition(const UMassbilityUnitDefinition& UnitDefinition, uint16& OutUnitDefinitionId, uint16& OutSchemaId);
	/** 将组合 StateFlags 转换为对应 Runtime Tag Mask 的并集。 */
	bool BuildStateMask(EMassbilityStateFlags Flags, FMassbilityTagMask& OutMask) const;
	/** 延迟创建所有普通单位共享的 Fragment 布局。 */
	FMassArchetypeHandle GetOrCreateUnitArchetype();

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilityRuntimeTagCatalog> RuntimeTagCatalogAsset;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilityStateTagMapping> StateTagMappingAsset;

	/** 资产编译后的紧凑 Tag 表，热路径只引用其中的 bit。 */
	FMassbilityCompiledRuntimeTagCatalog RuntimeTagCatalog;
	/** Schema 资产到紧凑运行时 ID 的注册表。 */
	TMap<const UMassbilityAttributeSchema*, uint16> SchemaIds;
	TArray<FMassbilityCompiledAttributeSchema> CompiledSchemas;
	TMap<const UMassbilityUnitDefinition*, uint16> UnitDefinitionIds;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMassbilityUnitDefinition>> UnitDefinitions;
	/** 单个 StateFlag 对应的 Tag Mask；组合状态在 BuildStateMask 中合并。 */
	TMap<uint32, FMassbilityTagMask> StateTagMasks;
	TStaticArray<FGameplayTag, 3> VitalAttributeTags;
	FMassbilitySimulationClock SimulationClock;
	uint32 TargetRefreshTicks = 2;
	float SpatialCellSize = 400.0f;
	FMassArchetypeHandle UnitArchetype;
};
