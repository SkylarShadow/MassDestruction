// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Ability/MassbilityAbilityTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "MassbilityAbilitySubsystem.generated.h"

class UMassEntitySubsystem;
class UMassbilityAbilityDefinition;
class UMassbilityEffectSubsystem;
class UMassbilityExternalTargetSubsystem;
class UMassbilityOutputSubsystem;
class UMassbilitySubsystem;
class UMassbilityUnitDefinition;
struct FMassEntityManager;
struct FMassExecutionContext;

UCLASS()
class MASSBILITY_API UMassbilityAbilitySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassbilityAbilitySubsystem* Get(const UObject* WorldContextObject);

	/** 编译并注册单个 Ability，返回供 Loadout 和实体运行时使用的紧凑 ID。 */
	FMassbilityAbilityId RegisterAbility(const UMassbilityAbilityDefinition& Definition);
	/** 编译 Unit Definition 的能力列表，并建立可复用的 Loadout。 */
	bool RegisterLoadout(const UMassbilityUnitDefinition& UnitDefinition, uint16& OutLoadoutId);
	/** 将一次能力激活请求排队，实际校验和输出在 Processor 阶段执行。 */
	bool QueueAbility(FMassEntityHandle Source, uint8 AbilitySlot,
		FMassEntityHandle ExplicitTarget = {}, float MagnitudeScale = 1.0f);

	const FMassbilityCompiledAbility* FindAbility(FMassbilityAbilityId AbilityId) const;
	const FMassbilityCompiledAbilityLoadout* FindLoadout(uint16 LoadoutId) const;
	const FMassbilityAbilityStats& GetStats() const { return Stats; }

private:
	friend class UMassbilityAbilityProcessor;

	/** 把作者数据转换为 Tick、Tag Mask 和 OutputId 组成的热路径结构。 */
	bool CompileAbility(const UMassbilityAbilityDefinition& Definition, FMassbilityCompiledAbility& OutAbility);
	/** 按提交顺序消费能力请求，并记录每次激活结果。 */
	void FlushRequests(FMassEntityManager& EntityManager, uint32 CurrentTick);
	/** 执行一次完整的能力激活校验和输出提交。 */
	EMassbilityAbilityFailure TryActivate(FMassEntityManager& EntityManager, FMassEntityHandle Source,
		uint8 AbilitySlot, FMassEntityHandle ExplicitTarget, float MagnitudeScale, uint32 CurrentTick);
	/** 更新尝试、成功和分类失败统计。 */
	void RecordResult(EMassbilityAbilityFailure Failure);

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilitySubsystem> MassbilitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilityEffectSubsystem> EffectSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilityExternalTargetSubsystem> ExternalTargetSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassbilityOutputSubsystem> OutputSubsystem;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMassbilityAbilityDefinition>> AbilityDefinitions;

	/** Ability Definition 资产到紧凑 ID 的注册表。 */
	TMap<const UMassbilityAbilityDefinition*, uint16> AbilityIds;
	/** AbilityTag 到 AbilityId 的反向索引。 */
	TMap<FGameplayTag, uint16> AbilityTagIds;
	/** 已编译的能力数据。 */
	TArray<FMassbilityCompiledAbility> CompiledAbilities;
	/** Unit Definition 到 LoadoutId 的注册表。 */
	TMap<const UMassbilityUnitDefinition*, uint16> LoadoutIds;
	/** 已编译的单位能力栏。 */
	TArray<FMassbilityCompiledAbilityLoadout> CompiledLoadouts;
	/** 等待 Ability Processor 消费的激活请求。 */
	TArray<FMassbilityAbilityRequest> PendingRequests;
	/** 能力激活运行统计。 */
	FMassbilityAbilityStats Stats;
};
