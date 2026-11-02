// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Effect/MassEffectTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "MassEffectSubsystem.generated.h"

class UMassEntitySubsystem;
class UMassCombatSubsystem;
class UMassEffectDefinition;
class UMassRuntimeSubsystem;
struct FMassEntityManager;
struct FMassExecutionContext;
struct FMassActiveEffectFragment;
struct FMassAttributeFragment;
struct FMassCombatFragment;
struct FMassStateFragment;
struct FMassUnitFragment;
struct FMassVitalFragment;

// 外部提交的Effect请求，转换成实体上的活动效果，并维护效果的注册、叠加、持续时间、周期执行、属性修改和 Tag 状态
UCLASS()
class MASSBILITY_API UMassEffectSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	static UMassEffectSubsystem* Get(const UObject* WorldContextObject);

	/** 编译并注册效果模板，返回可在请求和 Fragment 中保存的紧凑 EffectId。 */
	FMassEffectId RegisterEffect(const UMassEffectDefinition& Definition);
	/** 为指定 Attribute Schema 预先建立效果 Variant，避免首次应用时编译。 */
	bool PrewarmEffectForSchema(const UMassEffectDefinition& Definition, uint16 SchemaId);
	/** 以 Definition 形式提交效果；未注册时会先完成注册。 */
	bool QueueEffect(FMassEntityHandle Target, const UMassEffectDefinition& Definition,
		FMassEntityHandle Source = {}, float MagnitudeScale = 1.0f);
	/** 使用已注册 EffectId 提交效果请求，适合热路径调用。 */
	bool QueueRegisteredEffect(FMassEntityHandle Target, FMassEffectId EffectId,
		FMassEntityHandle Source = {}, float MagnitudeScale = 1.0f);
	/** 将移除操作排入队列，由效果 Processor 在统一时序中执行。 */
	bool QueueRemoveEffect(FMassEntityHandle Target, FMassEffectId EffectId);
	bool QueueRemoveAllEffects(FMassEntityHandle Target);
	/** 查询与 EffectId 对应的、与 Schema 无关的模板数据。 */
	const FMassCompiledEffectTemplate* FindEffectTemplate(FMassEffectId EffectId) const;
	/** 通过作者配置的 EffectTag 反查运行时 EffectId。 */
	bool FindEffectIdByTag(FGameplayTag EffectTag, FMassEffectId& OutEffectId) const;

	int32 GetPendingRequestCount() const { return PendingRequests.Num(); }

private:
	friend class UMassEffectProcessor;
	friend class UMassEffectApplyProcessor;

	/** 编译 Definition 的通用部分；属性 Tag 到 Handle 的解析留给 Variant 编译。 */
	bool CompileTemplate(const UMassEffectDefinition& Definition, FMassCompiledEffectTemplate& OutTemplate);
	/** 获取或创建某个 EffectId 在指定 Schema 下的热路径 Variant。 */
	const FMassCompiledEffect* GetOrCreateVariant(FMassEffectId EffectId, uint16 SchemaId);
	/** 在效果处理阶段批量转移本 Tick 收到的应用请求。 */
	void FlushPendingRequests(FMassEntityManager& EntityManager, FMassExecutionContext& Context, uint32 CurrentTick);
	/** 在效果处理阶段批量转移本 Tick 收到的移除请求。 */
	void FlushRemovalRequests(FMassEntityManager& EntityManager, FMassExecutionContext& Context);
	/** 校验目标并按叠加策略应用一个效果请求。 */
	bool ApplyRequest(FMassEntityManager& EntityManager, FMassExecutionContext& Context,
		const FMassEffectRequest& Request, uint32 CurrentTick);
	/** 按 EffectId 或 All 模式移除目标上的活动效果，并执行反向 Modifier。 */
	bool RemoveMatchingEffects(FMassEntityManager& EntityManager, FMassExecutionContext& Context,
		const FMassEffectRemovalRequest& Request);
	/** 推进单个实体的周期效果、到期效果、层数和派生状态 Tag。 */
	void TickEntity(FMassEntityHandle Entity, FMassExecutionContext& Context, uint32 CurrentTick,
		const FMassUnitFragment& Unit, FMassVitalFragment& Vitals,
		FMassAttributeFragment& Attributes, FMassStateFragment& State, FMassCombatFragment& Combat,
		FMassActiveEffectFragment& ActiveEffects);
	/** 从所有活动 Effect Slot 汇总 GrantedTags，重建 State.EffectTags 和 RuntimeTags。 */
	void RebuildEffectTags(FMassStateFragment& State, const FMassActiveEffectFragment& ActiveEffects) const;
	/** 按 Modifier Timing 应用属性变更；Ongoing 的移除通过负倍率完成。 */
	bool ApplyModifiers(const FMassCompiledEffect& Effect, EMassModifierTiming Timing,
		float Scale, const FMassUnitFragment& Unit, FMassVitalFragment& Vitals,
		FMassAttributeFragment& Attributes) const;

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassRuntimeSubsystem> MassSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UMassCombatSubsystem> CombatSubsystem;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMassEffectDefinition>> EffectDefinitions;

	/** Definition 资产到模板 ID 的注册表。 */
	TMap<const UMassEffectDefinition*, uint16> EffectIds;
	/** EffectTag 到模板 ID 的反向索引。 */
	TMap<FGameplayTag, uint16> EffectTagIds;
	/** 不依赖具体 Attribute Schema 的效果模板。 */
	TArray<FMassCompiledEffectTemplate> CompiledTemplates;
	/** (EffectId, SchemaId) 到 VariantId 的索引。 */
	TMap<uint32, uint16> VariantIds;
	/** 已解析 Attribute Handle 的效果变体，供热路径直接使用。 */
	TArray<FMassCompiledEffect> CompiledVariants;
	/** 等待当前效果 Processor 消费的应用请求。 */
	TArray<FMassEffectRequest> PendingRequests;
	/** 等待当前效果 Processor 消费的移除请求。 */
	TArray<FMassEffectRemovalRequest> PendingRemovalRequests;
};
