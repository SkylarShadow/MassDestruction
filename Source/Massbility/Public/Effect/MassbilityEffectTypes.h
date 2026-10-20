// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Core/MassbilityTypes.h"
#include "GameplayTagContainer.h"
#include "Mass/EntityHandle.h"

#include "MassbilityEffectTypes.generated.h"

USTRUCT(BlueprintType)
struct MASSBILITY_API FMassbilityEffectId
{
	GENERATED_BODY()

	/** 运行时效果模板的紧凑索引；InvalidId 表示尚未注册或解析失败。 */
	uint16 Value = Massbility::InvalidId;

	bool IsValid() const { return Value != Massbility::InvalidId; }
	friend bool operator==(const FMassbilityEffectId& A, const FMassbilityEffectId& B) { return A.Value == B.Value; }
};

UENUM(BlueprintType)
enum class EMassbilityEffectStackPolicy : uint8
{
	Reject,           // 已存在相同效果时拒绝新的应用请求。
	RefreshDuration,  // 不增加层数，只刷新已有效果的持续时间。
	AddStack,         // 增加层数，并按定义的层数上限裁剪。
};

UENUM(BlueprintType)
enum class EMassbilityEffectDurationPolicy : uint8
{
	Instant,  // 只执行一次，不进入 Active Effect Slot。
	Duration, // 进入 Slot，并在 ExpireTick 到达时移除。
	Infinite, // 进入 Slot，但没有自然到期时间。
};

UENUM(BlueprintType)
enum class EMassbilityModifierTiming : uint8
{
	Instant,  // 应用效果时执行一次。
	Ongoing,  // 加入时应用，到期或移除时执行反向操作。
	Periodic, // 按 PeriodTicks 周期执行。
};

UENUM(BlueprintType)
enum class EMassbilityModifierOp : uint8
{
	Add, // 将 Magnitude 加到目标属性上；当前版本只有加法操作。
};

USTRUCT(BlueprintType)
struct MASSBILITY_API FMassbilityEffectModifierDefinition
{
	GENERATED_BODY()

	/** 该 Modifier 影响的作者属性 Tag，编译时会解析成 AttributeHandle。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect")
	FGameplayTag AttributeTag;

	/** Modifier 的执行时机。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect")
	EMassbilityModifierTiming Timing = EMassbilityModifierTiming::Instant;

	/** Modifier 的运算类型。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect")
	EMassbilityModifierOp Operation = EMassbilityModifierOp::Add;

	/** 该 Modifier 的基础数值；最终值会乘以请求中的 MagnitudeScale。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect")
	float Magnitude = 0.0f;
};

struct MASSBILITY_API FMassbilityCompiledEffectModifier
{
	/** 已从 GameplayTag 解析出的属性句柄，热路径不再查询作者 Tag。 */
	FMassbilityAttributeHandle Attribute;
	EMassbilityModifierTiming Timing = EMassbilityModifierTiming::Instant;
	EMassbilityModifierOp Operation = EMassbilityModifierOp::Add;
	/** 编译后的基础数值，具体应用由 Timing 和 Operation 决定。 */
	float Magnitude = 0.0f;
};

/**
 * 与具体 Attribute Schema 无关的效果模板数据。
 * 该层描述持续时间、周期、层数和授予的 Tags，之后可为不同 Schema 编译出 Variant。
 */
struct MASSBILITY_API FMassbilityCompiledEffectTemplate
{
	/** 效果在全局效果注册表中的 ID。 */
	FMassbilityEffectId EffectId;
	/** 用于作者配置、调试和外部映射的效果 GameplayTag。 */
	FGameplayTag EffectTag;
	/** 决定该效果是即时、有限持续还是无限持续。 */
	EMassbilityEffectDurationPolicy DurationPolicy = EMassbilityEffectDurationPolicy::Instant;
	uint32 DurationTicks = 0;
	uint32 PeriodTicks = 0;
	/** 允许的最大层数；Instant 效果通常不会使用该字段。 */
	uint8 StackLimit = 1;
	/** 相同效果再次应用时的层数和持续时间处理方式。 */
	EMassbilityEffectStackPolicy StackPolicy = EMassbilityEffectStackPolicy::RefreshDuration;
	/** 应用期间附加到目标 RuntimeTags 的标签。 */
	FMassbilityTagMask GrantedTags;
	/** 尚未绑定具体 Schema 的 Modifier 定义。 */
	TArray<FMassbilityEffectModifierDefinition> Modifiers;
};

/**
 * 针对单个 Attribute Schema 的热路径效果数据。
 * 所有属性 Tag 已在编译阶段转换为句柄，运行时只使用固定容量字段和 ID。
 */
struct MASSBILITY_API FMassbilityCompiledEffect
{
	FMassbilityEffectId EffectId;
	/** 同一效果模板在特定 Schema 下的编译变体 ID。 */
	uint16 VariantId = Massbility::InvalidId;
	/** 该 Variant 所使用的 Attribute Schema ID。 */
	uint16 SchemaId = Massbility::InvalidId;
	/** 决定该效果是即时、有限持续还是无限持续。 */
	EMassbilityEffectDurationPolicy DurationPolicy = EMassbilityEffectDurationPolicy::Instant;
	/** 持续时间和周期均使用固定 Simulation Tick，而不是浮点秒数。 */
	uint32 DurationTicks = 0;
	uint32 PeriodTicks = 0;
	uint8 StackLimit = 1;
	/** Modifiers 中实际有效的元素数量。 */
	uint8 ModifierCount = 0;
	/** 相同效果再次应用时的层数和持续时间处理方式。 */
	EMassbilityEffectStackPolicy StackPolicy = EMassbilityEffectStackPolicy::RefreshDuration;
	/** 应用期间附加到目标 RuntimeTags 的编译 Tag Mask。 */
	FMassbilityTagMask GrantedTags;
	TStaticArray<FMassbilityCompiledEffectModifier, Massbility::MaxEffectModifiers> Modifiers;
};

struct MASSBILITY_API FMassbilityEffectRequest
{
	/** 请求应用效果的目标和来源；Source 用于归属、上下文和后续事件。 */
	FMassEntityHandle Target;
	FMassEntityHandle Source;
	FMassbilityEffectId EffectId;
	/** 对效果内所有 Modifier 数值统一施加的倍率。 */
	float MagnitudeScale = 1.0f;
};

UENUM()
enum class EMassbilityEffectRemovalMode : uint8
{
	ByEffectId, // 只移除指定 EffectId 的活动效果。
	All,        // 移除目标身上的全部活动效果。
};

struct MASSBILITY_API FMassbilityEffectRemovalRequest
{
	/** 要清理效果的目标。 */
	FMassEntityHandle Target;
	/** Mode 为 ByEffectId 时使用的效果 ID。 */
	FMassbilityEffectId EffectId;
	EMassbilityEffectRemovalMode Mode = EMassbilityEffectRemovalMode::ByEffectId;
};

USTRUCT()
struct MASSBILITY_API FMassbilityActiveEffectSlot
{
	GENERATED_BODY()

	/** 最初施加该效果的来源实体。 */
	FMassEntityHandle Source;
	/** 当前 Slot 保存的效果实例 ID。 */
	FMassbilityEffectId EffectId;
	/** 该实例对应的 Schema 编译变体。 */
	uint16 VariantId = Massbility::InvalidId;
	/** 当前层数，0 表示 Slot 未激活。 */
	uint8 StackCount = 0;
	/** 有限持续效果的绝对到期 Tick；Infinite 效果不依赖该字段。 */
	uint32 ExpireTick = 0;
	/** 下一次周期 Modifier 执行的绝对 Tick。 */
	uint32 NextPeriodicTick = 0;
	/** Sum of per-stack magnitude scales; keeps heterogeneous stacks exactly reversible. */
	float StackMagnitudeSum = 1.0f;

	bool IsActive() const { return EffectId.IsValid() && VariantId != Massbility::InvalidId && StackCount > 0; }
	void Reset() { *this = {}; }
};
