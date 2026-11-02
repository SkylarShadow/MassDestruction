// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Ability/MassAbilityTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "MassAbilitySubsystem.generated.h"

class UMassEntitySubsystem;
class UMassAbilityDefinition;
class UMassEffectSubsystem;
class UMassExternalTargetSubsystem;
class UMassOutputSubsystem;
class UMassRuntimeSubsystem;
class UMassUnitDefinition;
struct FMassEntityManager;
struct FMassExecutionContext;

UCLASS()
class MASSBILITY_API UMassAbilitySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassAbilitySubsystem* Get(const UObject* WorldContextObject);

	/** 编译并注册单个 Ability，返回供 Loadout 和实体运行时使用的紧凑 ID。 */
	FMassAbilityId RegisterAbility(const UMassAbilityDefinition& Definition);
	/** 编译 Unit Definition 的能力列表，并建立可复用的 Loadout。 */
	bool RegisterLoadout(const UMassUnitDefinition& UnitDefinition, uint16& OutLoadoutId);
	/** 将一次能力激活请求排队，实际校验和输出在 Processor 阶段执行。 */
	bool QueueAbility(FMassEntityHandle Source, uint8 AbilitySlot,
		FMassEntityHandle ExplicitTarget = {}, float MagnitudeScale = 1.0f);

	const FMassCompiledAbility* FindAbility(FMassAbilityId AbilityId) const;
	const FMassCompiledAbilityLoadout* FindLoadout(uint16 LoadoutId) const;
	const FMassAbilityStats& GetStats() const { return Stats; }

private:
	friend class UMassAbilityProcessor;

	/** 把作者数据转换为 Tick、Tag Mask 和 OutputId 组成的热路径结构。 */
	bool CompileAbility(const UMassAbilityDefinition& Definition, FMassCompiledAbility& OutAbility);
	/** 按提交顺序消费能力请求，并记录每次激活结果。 */
	void FlushRequests(FMassEntityManager& EntityManager, uint32 CurrentTick);
	/** 执行一次完整的能力激活校验和输出提交。 */
	EMassAbilityFailure TryActivate(FMassEntityManager& EntityManager, FMassEntityHandle Source,
		uint8 AbilitySlot, FMassEntityHandle ExplicitTarget, float MagnitudeScale, uint32 CurrentTick);
	/** 更新尝试、成功和分类失败统计。 */
	void RecordResult(EMassAbilityFailure Failure);

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassRuntimeSubsystem> MassSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassEffectSubsystem> EffectSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassExternalTargetSubsystem> ExternalTargetSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassOutputSubsystem> OutputSubsystem;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMassAbilityDefinition>> AbilityDefinitions;

	/** Ability Definition 资产到紧凑 ID 的注册表。 */
	TMap<const UMassAbilityDefinition*, uint16> AbilityIds;
	/** AbilityTag 到 AbilityId 的反向索引。 */
	TMap<FGameplayTag, uint16> AbilityTagIds;
	/** 已编译的能力数据。 */
	TArray<FMassCompiledAbility> CompiledAbilities;
	/** Unit Definition 到 LoadoutId 的注册表。 */
	TMap<const UMassUnitDefinition*, uint16> LoadoutIds;
	/** 已编译的单位能力栏。 */
	TArray<FMassCompiledAbilityLoadout> CompiledLoadouts;
	/** 等待 Ability Processor 消费的激活请求。 */
	TArray<FMassAbilityRequest> PendingRequests;
	/** 能力激活运行统计。 */
	FMassAbilityStats Stats;
};
