// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Attributes/MassAttributeSchema.h"
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
	bool DestroyUnit(FMassEntityHandle Entity);

	/** 将作者配置的 GameplayTag 解析为固定 Vital 或 Extended 存储中的属性句柄。 */
	bool ResolveAttributeHandle(const UMassAttributeSchema& Schema, FGameplayTag AttributeTag, FMassAttributeHandle& OutHandle) const;
	bool ResolveAttributeHandle(uint16 SchemaId, FGameplayTag AttributeTag, FMassAttributeHandle& OutHandle) const;
	bool GetAttribute(FMassEntityHandle Entity, FMassAttributeHandle Handle, float& OutValue) const;
	bool SetAttribute(FMassEntityHandle Entity, FMassAttributeHandle Handle, float Value);
	bool AddAttribute(FMassEntityHandle Entity, FMassAttributeHandle Handle, float Delta);
	/** 仅操作 Extended Attribute，防止 Vital 句柄被误用。 */
	bool GetExtendedAttribute(FMassEntityHandle Entity, FMassAttributeHandle Handle, float& OutValue) const;
	bool SetExtendedAttribute(FMassEntityHandle Entity, FMassAttributeHandle Handle, float Value);
	bool AddExtendedAttribute(FMassEntityHandle Entity, FMassAttributeHandle Handle, float Delta);
	/** 设置状态位，并立即重建 Runtime Tag 缓存。 */
	bool SetStateFlags(FMassEntityHandle Entity, EMassStateFlags NewFlags);
	bool HasRuntimeTags(FMassEntityHandle Entity, const FMassTagMask& Required, const FMassTagMask& Blocked = {}) const;
	bool RebuildRuntimeTags(FMassEntityHandle Entity);
	const FMassCompiledAttributeSchema* FindCompiledSchema(uint16 SchemaId) const;
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
	/** 注册并缓存 Attribute Schema，返回紧凑 SchemaId。 */
	bool RegisterSchema(const UMassAttributeSchema& Schema, uint16& OutSchemaId);
	/** 注册 Unit Definition 及其 Schema；实体热路径只保存 ID。 */
	bool RegisterUnitDefinition(const UMassUnitDefinition& UnitDefinition, uint16& OutUnitDefinitionId, uint16& OutSchemaId);
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
	/** Schema 资产到紧凑运行时 ID 的注册表。 */
	TMap<const UMassAttributeSchema*, uint16> SchemaIds;
	TArray<FMassCompiledAttributeSchema> CompiledSchemas;
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
