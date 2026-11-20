// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Core/MassTypes.h"
#include "GameplayTagContainer.h"
#include "Mass/EntityHandle.h"

#include "MassEffectTypes.generated.h"

USTRUCT(BlueprintType)
struct MASSBILITY_API FMassEffectId
{
	GENERATED_BODY()

	/** 运行时效果模板的紧凑索引；InvalidId 表示尚未注册或解析失败。 */
	uint16 Value = Massbility::InvalidId;

	bool IsValid() const { return Value != Massbility::InvalidId; }
	friend bool operator==(const FMassEffectId& A, const FMassEffectId& B) { return A.Value == B.Value; }
};

UENUM(BlueprintType)
enum class EMassEffectStackPolicy : uint8
{
	Reject,           // 已存在相同效果时拒绝新的应用请求。
	RefreshDuration,  // 不增加层数，只刷新已有效果的持续时间。
	AddStack,         // 增加层数，并按定义的层数上限裁剪。
};

UENUM(BlueprintType)
enum class EMassEffectDurationPolicy : uint8
{
	Instant,  // 只执行一次，不进入 Active Effect Slot。
	Duration, // 进入 Slot，并在 ExpireTick 到达时移除。
	Infinite, // 进入 Slot，但没有自然到期时间。
};

UENUM(BlueprintType)
enum class EMassModifierTiming : uint8
{
	Instant,  // 应用效果时执行一次。
	Ongoing,  // 加入时应用，到期或移除时执行反向操作。
	Periodic, // 按 PeriodTicks 周期执行。
};

UENUM(BlueprintType)
enum class EMassModifierOp : uint8
{
	Add, // 将 Magnitude 加到目标属性上；当前版本只有加法操作。
};

/** Modifier 数值来自定义常量，或由本次 FMassEffectSpec 提供。 */
UENUM(BlueprintType)
enum class EMassEffectMagnitudeSource : uint8
{
	Constant,
	SetByCaller,
};

USTRUCT(BlueprintType)
struct MASSBILITY_API FMassEffectModifierDefinition
{
	GENERATED_BODY()

	/** 该 Modifier 影响的作者属性 Tag，编译时会解析成 AttributeHandle。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect")
	FGameplayTag AttributeTag;

	/** Modifier 的执行时机。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect")
	EMassModifierTiming Timing = EMassModifierTiming::Instant;

	/** Modifier 的运算类型。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect")
	EMassModifierOp Operation = EMassModifierOp::Add;

	/** 该 Modifier 的基础数值；SetByCaller 缺省时也使用该值。最终值会乘以 Spec 的 MagnitudeScale。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect")
	float Magnitude = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect")
	EMassEffectMagnitudeSource MagnitudeSource = EMassEffectMagnitudeSource::Constant;

	/** MagnitudeSource 为 SetByCaller 时，读取本次 Effect Spec 中同名参数。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Massbility|Effect", meta = (EditCondition = "MagnitudeSource == EMassEffectMagnitudeSource::SetByCaller", EditConditionHides))
	FGameplayTag SetByCallerTag;
};

struct MASSBILITY_API FMassCompiledEffectModifier
{
	/** 已从 GameplayTag 解析出的属性句柄，热路径不再查询作者 Tag。 */
	FMassAttributeHandle Attribute;
	EMassModifierTiming Timing = EMassModifierTiming::Instant;
	EMassModifierOp Operation = EMassModifierOp::Add;
	/** 编译后的基础数值，具体应用由 Timing 和 Operation 决定。 */
	float Magnitude = 0.0f;
	EMassEffectMagnitudeSource MagnitudeSource = EMassEffectMagnitudeSource::Constant;
	/** Effect 模板内的紧凑 SetByCaller 槽位；Constant 时为 MAX_uint8。 */
	uint8 SetByCallerIndex = MAX_uint8;
};

/**
 * 与具体 Attribute Schema 无关的效果模板数据。
 * 该层描述持续时间、周期、层数和授予的 Tags，之后可为不同 Schema 编译出 Variant。
 */
struct MASSBILITY_API FMassCompiledEffectTemplate
{
	/** 效果在全局效果注册表中的 ID。 */
	FMassEffectId EffectId;
	/** 用于作者配置、调试和外部映射的效果 GameplayTag。 */
	FGameplayTag EffectTag;
	/** 决定该效果是即时、有限持续还是无限持续。 */
	EMassEffectDurationPolicy DurationPolicy = EMassEffectDurationPolicy::Instant;
	uint32 DurationTicks = 0;
	uint32 PeriodTicks = 0;
	/** 允许的最大层数；Instant 效果通常不会使用该字段。 */
	uint8 StackLimit = 1;
	/** 相同效果再次应用时的层数和持续时间处理方式。 */
	EMassEffectStackPolicy StackPolicy = EMassEffectStackPolicy::RefreshDuration;
	/** 应用期间附加到目标 RuntimeTags 的标签。 */
	FMassTagMask GrantedTags;
	/** 该 Active Effect 阻止的 AbilityTag；由实体上的 Effect 状态聚合。 */
	FMassTagMask BlockedAbilityTags;
	/** 作者 SetByCaller Tag 编译后的固定槽位。 */
	uint8 SetByCallerTagCount = 0;
	TStaticArray<FGameplayTag, Massbility::MaxEffectSetByCallerValues> SetByCallerTags;
	/** 尚未绑定具体 Schema 的 Modifier 定义。 */
	TArray<FMassEffectModifierDefinition> Modifiers;
};

/**
 * 针对单个 Attribute Schema 的热路径效果数据。
 * 所有属性 Tag 已在编译阶段转换为句柄，运行时只使用固定容量字段和 ID。
 */
struct MASSBILITY_API FMassCompiledEffect
{
	FMassEffectId EffectId;
	/** 同一效果模板在特定 Schema 下的编译变体 ID。 */
	uint16 VariantId = Massbility::InvalidId;
	/** 该 Variant 所使用的 Attribute Schema ID。 */
	uint16 LayoutId = Massbility::InvalidId;
	/** 决定该效果是即时、有限持续还是无限持续。 */
	EMassEffectDurationPolicy DurationPolicy = EMassEffectDurationPolicy::Instant;
	/** 持续时间和周期均使用固定 Simulation Tick，而不是浮点秒数。 */
	uint32 DurationTicks = 0;
	uint32 PeriodTicks = 0;
	uint8 StackLimit = 1;
	/** Modifiers 中实际有效的元素数量。 */
	uint8 ModifierCount = 0;
	/** 相同效果再次应用时的层数和持续时间处理方式。 */
	EMassEffectStackPolicy StackPolicy = EMassEffectStackPolicy::RefreshDuration;
	/** 应用期间附加到目标 RuntimeTags 的编译 Tag Mask。 */
	FMassTagMask GrantedTags;
	/** 应用期间阻止目标激活的 AbilityTag。 */
	FMassTagMask BlockedAbilityTags;
	TStaticArray<FMassCompiledEffectModifier, Massbility::MaxEffectModifiers> Modifiers;
};

/** 一项由调用者在本次效果施加时提供的动态参数。 */
USTRUCT(BlueprintType)
struct MASSBILITY_API FMassEffectSetByCallerValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Massbility|Effect")
	FGameplayTag DataTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Massbility|Effect")
	float Value = 0.0f;
};

/**
 * Mass 侧的轻量 GameplayEffectSpec。
 * Definition 决定效果结构；Spec 保存一次施加的来源、强度、可选时间覆盖和固定容量 SetByCaller 参数。
 */
USTRUCT(BlueprintType)
struct MASSBILITY_API FMassEffectSpec
{
	GENERATED_BODY()

	/** 请求应用效果的目标和来源；Source 用于归属、上下文和后续事件。 */
	FMassEntityHandle Target;
	FMassEntityHandle Source;
	FMassEffectId EffectId;
	/** 对所有 Modifier 数值统一施加的倍率。 */
	float MagnitudeScale = 1.0f;
	/** 小于零时使用 Definition 默认时长。 */
	float DurationOverrideSeconds = -1.0f;
	/** 小于零时使用 Definition 默认周期。 */
	float PeriodOverrideSeconds = -1.0f;
	uint8 SetByCallerCount = 0;
	TStaticArray<FMassEffectSetByCallerValue, Massbility::MaxEffectSetByCallerValues> SetByCallerValues;

	bool SetSetByCallerMagnitude(const FGameplayTag DataTag, const float Value)
	{
		if (!DataTag.IsValid() || !FMath::IsFinite(Value))
		{
			return false;
		}
		for (uint8 Index = 0; Index < SetByCallerCount; ++Index)
		{
			if (SetByCallerValues[Index].DataTag == DataTag)
			{
				SetByCallerValues[Index].Value = Value;
				return true;
			}
		}
		if (SetByCallerCount >= Massbility::MaxEffectSetByCallerValues)
		{
			return false;
		}
		FMassEffectSetByCallerValue& NewValue = SetByCallerValues[SetByCallerCount++];
		NewValue.DataTag = DataTag;
		NewValue.Value = Value;
		return true;
	}
};

UENUM()
enum class EMassEffectRemovalMode : uint8
{
	ByEffectId, // 只移除指定 EffectId 的活动效果。
	All,        // 移除目标身上的全部活动效果。
};

struct MASSBILITY_API FMassEffectRemovalRequest
{
	/** 要清理效果的目标。 */
	FMassEntityHandle Target;
	/** Mode 为 ByEffectId 时使用的效果 ID。 */
	FMassEffectId EffectId;
	EMassEffectRemovalMode Mode = EMassEffectRemovalMode::ByEffectId;
};

USTRUCT()
struct MASSBILITY_API FMassActiveEffectSlot
{
	GENERATED_BODY()

	/** 最初施加该效果的来源实体。 */
	FMassEntityHandle Source;
	/** 当前 Slot 保存的效果实例 ID。 */
	FMassEffectId EffectId;
	/** 该实例对应的 Schema 编译变体。 */
	uint16 VariantId = Massbility::InvalidId;
	/** 当前层数，0 表示 Slot 未激活。 */
	uint8 StackCount = 0;
	/** 有限持续效果的绝对到期 Tick；Infinite 效果不依赖该字段。 */
	uint32 ExpireTick = 0;
	/** 下一次周期 Modifier 执行的绝对 Tick。 */
	uint32 NextPeriodicTick = 0;
	/** 此实例实际使用的周期；支持一次施加时覆盖 Definition 周期。 */
	uint32 PeriodTicks = 0;
	/** Sum of per-stack magnitude scales; keeps heterogeneous stacks exactly reversible. */
	float StackMagnitudeSum = 1.0f;
	/** 所有已叠加实例的 SetByCaller 值之和；对应模板中的紧凑参数槽位。 */
	uint8 SetByCallerValueMask = 0;
	TStaticArray<float, Massbility::MaxEffectSetByCallerValues> SetByCallerValueSums = {};

	bool IsActive() const { return EffectId.IsValid() && VariantId != Massbility::InvalidId && StackCount > 0; }
	void Reset() { *this = {}; }
};
