// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Core/MassbilityTypes.h"
#include "GameplayTagContainer.h"
#include "Mass/EntityHandle.h"
#include "Output/MassbilityOutputTypes.h"

#include "MassbilityAbilityTypes.generated.h"

USTRUCT(BlueprintType)
struct MASSBILITY_API FMassbilityAbilityId
{
	GENERATED_BODY()

	/** 运行时 Ability 注册表中的紧凑索引；InvalidId 表示无效能力。 */
	uint16 Value = Massbility::InvalidId;
	bool IsValid() const { return Value != Massbility::InvalidId; }
};

UENUM(BlueprintType)
enum class EMassbilityAbilityTargetPolicy : uint8
{
	Self,           // 目标固定为能力拥有者自身。
	CurrentTarget,  // 使用实体当前缓存的目标。
	ExplicitTarget, // 使用请求中提供的 ExplicitTarget。
};

/**
 * 已编译的单个 Ability 数据。
 * 作者资产中的秒数、GameplayTag 和输出定义会在编译阶段转换为 Tick、Tag Mask 和 OutputId。
 */
struct MASSBILITY_API FMassbilityCompiledAbility
{
	/** 能力在全局注册表中的 ID。 */
	FMassbilityAbilityId AbilityId;
	/** 用于调试、查找和外部映射的能力 GameplayTag。 */
	FGameplayTag AbilityTag;
	/** 冷却持续的模拟 Tick 数。 */
	uint32 CooldownTicks = 0;
	/** 激活者必须具备的运行时 Tag。 */
	FMassbilityTagMask RequiredOwnerTags;
	/** 激活者不能具备的运行时 Tag。 */
	FMassbilityTagMask BlockedOwnerTags;
	/** 目标必须具备的运行时 Tag。 */
	FMassbilityTagMask RequiredTargetTags;
	/** 目标不能具备的运行时 Tag。 */
	FMassbilityTagMask BlockedTargetTags;
	/** 最大施法距离的平方，避免运行时进行平方根计算。 */
	float RangeSquared = 0.0f;
	/** OutputIds 中实际有效的输出数量。 */
	uint8 OutputCount = 0;
	/** 决定激活时目标从哪里取得。 */
	EMassbilityAbilityTargetPolicy TargetPolicy = EMassbilityAbilityTargetPolicy::CurrentTarget;
	/** 为 true 时使用二维平面距离判断范围。 */
	bool bUse2DDistance = true;
	/** 能力激活后提交给统一 Output Dispatcher 的输出 ID。 */
	TStaticArray<FMassbilityOutputId, Massbility::MaxAbilityOutputs> OutputIds;
};

/**
 * 已编译的单位 Ability Loadout。
 * 实体通过 LoadoutId 和固定容量 AbilityIds 使用能力，不在热路径遍历资产数组。
 * 
 * //TODO: 这个范围要去除吗？
 */
struct MASSBILITY_API FMassbilityCompiledAbilityLoadout
{
	/** Loadout 注册表中的 ID。 */
	uint16 LoadoutId = Massbility::InvalidId;
	/** AbilityIds 中实际有效的能力数量。 */
	uint8 AbilityCount = 0;
	/** Loadout 内所有能力的最大目标范围，用于快速预筛选。 */
	float MaxTargetRange = 0.0f;
	/** 单位可使用的能力 ID 列表。 */
	TStaticArray<FMassbilityAbilityId, Massbility::MaxAbilitiesPerUnit> AbilityIds;
};

struct MASSBILITY_API FMassbilityAbilityRequest
{
	/** 发起能力激活的 Mass Entity。 */
	FMassEntityHandle Source;
	/** TargetPolicy 为 ExplicitTarget 时使用的目标。 */
	FMassEntityHandle ExplicitTarget;
	/** Source Loadout 中的能力槽位索引。 */
	uint8 AbilitySlot = 0;
	/** 对该次能力输出统一施加的倍率。 */
	float MagnitudeScale = 1.0f;
};

UENUM()
enum class EMassbilityAbilityFailure : uint8
{
	None,             // 激活成功或没有失败原因。
	InvalidSource,    // Source Handle 无效或实体不存在。
	InvalidAbility,  // 能力槽位、AbilityId 或编译数据无效。
	BlockedState,    // 拥有者当前状态禁止能力激活。
	Cooldown,        // 能力仍处于冷却中。
	OwnerTags,       // 拥有者不满足 Required/Blocked Owner Tags。
	InvalidTarget,   // 目标 Handle 无效或目标不可用。
	Team,            // 目标与拥有者的队伍关系不允许命中。
	TargetTags,      // 目标不满足 Required/Blocked Target Tags。
	Range,           // 目标超出编译后的最大范围。
	ExternalRoute,   // External Proxy 路由不存在或不可用。
	OutputRejected,  // 输出进入 Dispatcher 前或进入 Dispatcher 时被拒绝。
	Count UMETA(Hidden),
};

struct MASSBILITY_API FMassbilityAbilityStats
{
	/** 尝试激活能力的总次数。 */
	uint64 Attempts = 0;
	/** 成功激活并提交输出的次数。 */
	uint64 Succeeded = 0;
	/** 按失败枚举索引记录的失败次数。 */
	TStaticArray<uint64, static_cast<int32>(EMassbilityAbilityFailure::Count)> Failures = {};
};
