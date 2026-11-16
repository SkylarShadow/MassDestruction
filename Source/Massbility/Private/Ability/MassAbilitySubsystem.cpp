// Copyright MassDestruction. All Rights Reserved.

#include "Ability/MassAbilitySubsystem.h"

#include "Ability/MassAbilityDefinition.h"
#include "Core/MassRuntimeSubsystem.h"
#include "Effect/MassEffectDefinition.h"
#include "Effect/MassEffectSubsystem.h"
#include "Engine/Engine.h"
#include "Entity/MassFragments.h"
#include "Entity/MassUnitDefinition.h"
#include "External/MassExternalTargetFragments.h"
#include "External/MassExternalTargetSubsystem.h"
#include "Mass/EntityFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Mass.h"
#include "Output/MassOutputSubsystem.h"

void UMassAbilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	MassSubsystem = Collection.InitializeDependency<UMassRuntimeSubsystem>(); //模拟 Tick、运行时 Tag Catalog
	EffectSubsystem = Collection.InitializeDependency<UMassEffectSubsystem>();  //把 Effect DataAsset 编译/注册为 EffectId
	ExternalTargetSubsystem = Collection.InitializeDependency<UMassExternalTargetSubsystem>(); //验证外部 Actor/GAS 代理目标
	OutputSubsystem = Collection.InitializeDependency<UMassOutputSubsystem>();//能力最终输出的统一投递端
	PendingRequests.Reserve(256);
}

void UMassAbilitySubsystem::Deinitialize()
{
	PendingRequests.Reset();
	CompiledLoadouts.Reset();
	LoadoutIds.Reset();
	CompiledAbilities.Reset();
	AbilityTagIds.Reset();
	AbilityIds.Reset();
	AbilityDefinitions.Reset();
	Stats = {};
	OutputSubsystem = nullptr;
	ExternalTargetSubsystem = nullptr;
	EffectSubsystem = nullptr;
	MassSubsystem = nullptr;
	EntitySubsystem = nullptr;
	Super::Deinitialize();
}

bool UMassAbilitySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

UMassAbilitySubsystem* UMassAbilitySubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassAbilitySubsystem>();
	}
	return nullptr;
}

//把一个 UMassAbilityDefinition 编译并登记为轻量运行时能力。
FMassAbilityId UMassAbilitySubsystem::RegisterAbility(const UMassAbilityDefinition& Definition)
{
	// 将资产编译成紧凑运行时数据，并用 Definition 指针避免重复注册。
	// Ability 只编译一次；后续通过 Definition 指针复用已有 ID。
	if (const uint16* Existing = AbilityIds.Find(&Definition)) //如果Definition已注册,复用原来的 AbilityId
	{
		return {*Existing}; 
	}
	if (CompiledAbilities.Num() >= Massbility::InvalidId) //iD表是否耗尽
	{
		UE_LOG(LogMassbility, Error, TEXT("Mass ability registry exhausted."));
		return {};
	}

	FMassCompiledAbility Compiled;
	if (!CompileAbility(Definition, Compiled)) //把 DataAsset 转为紧凑结构
	{
		return {};
	}
	if (AbilityTagIds.Contains(Compiled.AbilityTag))    //AbilityTag全局唯一；两个不同能力不能有同一个主 Tag
	{
		UE_LOG(LogMassbility, Error, TEXT("Ability tag '%s' is already registered by another definition."), *Compiled.AbilityTag.ToString());
		return {};
	}
	Compiled.AbilityId.Value = static_cast<uint16>(CompiledAbilities.Num());
    // 写入下面四张表
	AbilityIds.Add(&Definition, Compiled.AbilityId.Value);
	AbilityTagIds.Add(Compiled.AbilityTag, Compiled.AbilityId.Value);
	AbilityDefinitions.Add(const_cast<UMassAbilityDefinition*>(&Definition));
	CompiledAbilities.Add(Compiled);
	return Compiled.AbilityId;
}
//把UnitDefinition 中的能力数组编译成“能力栏”
bool UMassAbilitySubsystem::RegisterLoadout(const UMassUnitDefinition& UnitDefinition, uint16& OutLoadoutId)
{
	if (const uint16* Existing = LoadoutIds.Find(&UnitDefinition))//同registerAbility 处理，注册了直接复用
	{
		OutLoadoutId = *Existing;
		return true;
	}
	// Loadout 是 Unit Definition 的能力列表快照，实体运行时只保存 LoadoutId。
	const TArray<TObjectPtr<UMassAbilityDefinition>>& Definitions = UnitDefinition.GetAbilities();
	if (Definitions.Num() > Massbility::MaxAbilitiesPerUnit || CompiledLoadouts.Num() >= Massbility::InvalidId)//校验数量 同上
	{
		UE_LOG(LogMassbility, Error, TEXT("Unit '%s' has too many abilities or the loadout registry is exhausted."), *UnitDefinition.GetPathName());
		return false;
	}

	FMassCompiledAbilityLoadout Loadout;
	Loadout.LoadoutId = static_cast<uint16>(CompiledLoadouts.Num());
	for (UMassAbilityDefinition* Definition : Definitions)  //遍历AbilityDefinition
	{
		if (!Definition)
		{
			UE_LOG(LogMassbility, Error, TEXT("Unit '%s' contains a null ability."), *UnitDefinition.GetPathName());
			return false;
		}
		const FMassAbilityId AbilityId = RegisterAbility(*Definition);// 注册或复用能力
		const FMassCompiledAbility* Ability = FindAbility(AbilityId);
		if (!Ability)
		{
			return false;
		}
		Loadout.AbilityIds[Loadout.AbilityCount++] = AbilityId;
		// 对所有 CurrentTarget 能力计算最大射程，存入 MaxTargetRange，用于记录单位最远能打到多远，可避免目标查询找得过近(Self 和特定目标不参与这个统计)
		if (Ability->TargetPolicy == EMassAbilityTargetPolicy::CurrentTarget)
		{
			Loadout.MaxTargetRange = FMath::Max(Loadout.MaxTargetRange, FMath::Sqrt(Ability->RangeSquared));
		}
	}
	OutLoadoutId = Loadout.LoadoutId;
	CompiledLoadouts.Add(Loadout);
	LoadoutIds.Add(&UnitDefinition, OutLoadoutId);
	return true;
}

bool UMassAbilitySubsystem::QueueAbility(const FMassEntityHandle Source, const uint8 AbilitySlot,
	const FMassEntityHandle ExplicitTarget, const float MagnitudeScale)
{
    // 只做轻量参数检查
    // Source Handle 已设置
    // MagnitudeScale 为有限正数 TODO: 是否允许为负数？
	if (!Source.IsSet() || !FMath::IsFinite(MagnitudeScale) || MagnitudeScale <= 0.0f)
	{
		return false;
	}
	// ；实体、槽位、状态、目标和队伍检查延迟到 FlushRequests。
	PendingRequests.Add({Source, ExplicitTarget, AbilitySlot, MagnitudeScale});
	return true;
}

//按 ID从 CompiledAbilities 中取能力
const FMassCompiledAbility* UMassAbilitySubsystem::FindAbility(const FMassAbilityId AbilityId) const
{
	return AbilityId.IsValid() && CompiledAbilities.IsValidIndex(AbilityId.Value) ? &CompiledAbilities[AbilityId.Value] : nullptr;
}

//安全地按 ID 取单位能力栏
const FMassCompiledAbilityLoadout* UMassAbilitySubsystem::FindLoadout(const uint16 LoadoutId) const
{
	return CompiledLoadouts.IsValidIndex(LoadoutId) ? &CompiledLoadouts[LoadoutId] : nullptr;
}

// 把秒数、GameplayTag 和输出定义转换为 Tick、TagMask 与 OutputId。
bool UMassAbilitySubsystem::CompileAbility(const UMassAbilityDefinition& Definition, FMassCompiledAbility& OutAbility)
{
	FString Error;
	if (!MassSubsystem || !EffectSubsystem || !OutputSubsystem || !Definition.Validate(Error))
	{
		UE_LOG(LogMassbility, Error, TEXT("Invalid ability definition '%s': %s"), *Definition.GetPathName(), *Error);
		return false;
	}
	OutAbility = {};
	OutAbility.AbilityTag = Definition.GetAbilityTag();
	FGameplayTagContainer EffectiveAbilityTags = Definition.GetAbilityTags();
	EffectiveAbilityTags.AddTag(Definition.GetAbilityTag());
    
	OutAbility.CooldownTicks = MassSubsystem->GetSimulationClock().SecondsToTicksCeil(Definition.GetCooldownSeconds());//Tick转换
	OutAbility.TargetPolicy = Definition.GetTargetPolicy();
	OutAbility.RangeSquared = FMath::Square(Definition.GetRange());
	OutAbility.bUse2DDistance = Definition.Uses2DDistance();
    
	const FMassCompiledRuntimeTagCatalog& Catalog = MassSubsystem->GetRuntimeTagCatalog();
	if (!Catalog.BuildMask(EffectiveAbilityTags, OutAbility.AbilityTags, Error)
		|| !Catalog.BuildMask(Definition.GetRequiredOwnerTags(), OutAbility.RequiredOwnerTags, Error)
		|| !Catalog.BuildMask(Definition.GetBlockedOwnerTags(), OutAbility.BlockedOwnerTags, Error)
		|| !Catalog.BuildMask(Definition.GetRequiredTargetTags(), OutAbility.RequiredTargetTags, Error)
		|| !Catalog.BuildMask(Definition.GetBlockedTargetTags(), OutAbility.BlockedTargetTags, Error))
	{
		UE_LOG(LogMassbility, Error, TEXT("Could not compile tags for ability '%s': %s"), *Definition.GetPathName(), *Error);
		return false;
	}
	// 每个能力输出都注册为独立 OutputId，实际由统一 Output Dispatcher 路由。
	for (const FMassAbilityOutputDefinition& SourceOutput : Definition.GetOutputs())
	{
		FMassOutputRegistration Output;
		Output.Type = SourceOutput.Type;
		Output.Damage = SourceOutput.Damage;
		Output.MagnitudeScale = SourceOutput.MagnitudeScale;
		Output.SetByCallerCount = static_cast<uint8>(SourceOutput.SetByCallerValues.Num());
		for (uint8 ValueIndex = 0; ValueIndex < Output.SetByCallerCount; ++ValueIndex)
		{
			Output.SetByCallerValues[ValueIndex] = SourceOutput.SetByCallerValues[ValueIndex];
		}
		Output.SemanticTag = SourceOutput.SemanticTag;
		Output.RequiredTargetTags = OutAbility.RequiredTargetTags;
		Output.BlockedTargetTags = OutAbility.BlockedTargetTags;
		Output.RelationPolicy = OutAbility.TargetPolicy == EMassAbilityTargetPolicy::Self
			? EMassOutputRelationPolicy::Self
			: (OutAbility.TargetPolicy == EMassAbilityTargetPolicy::ExplicitTarget
				? EMassOutputRelationPolicy::SelfOrHostile : EMassOutputRelationPolicy::Hostile);
		Output.bRequireTargetable = OutAbility.TargetPolicy != EMassAbilityTargetPolicy::Self;
		if (SourceOutput.Type == EMassAbilityOutputType::Effect)
		{
			Output.EffectId = EffectSubsystem->RegisterEffect(*SourceOutput.Effect);
			if (!Output.EffectId.IsValid())
			{
				return false;
			}
			if (!Output.SemanticTag.IsValid())
			{
				Output.SemanticTag = SourceOutput.Effect->GetEffectTag();
			}
		}
		const FMassOutputId OutputId = OutputSubsystem->RegisterOutput(Output);
		if (!OutputId.IsValid())
		{
			return false;
		}
		OutAbility.OutputIds[OutAbility.OutputCount++] = OutputId;
	}
	return true;
}

void UMassAbilitySubsystem::FlushRequests(FMassEntityManager& EntityManager, const uint32 CurrentTick)
{
	// 按入队顺序执行，保证同一 Tick 的手动请求结果可预测。
	for (const FMassAbilityRequest& Request : PendingRequests)
	{
		RecordResult(TryActivate(EntityManager, Request.Source, Request.AbilitySlot,
			Request.ExplicitTarget, Request.MagnitudeScale, CurrentTick));
	}
	PendingRequests.Reset();
}

EMassAbilityFailure UMassAbilitySubsystem::TryActivate(FMassEntityManager& EntityManager,
	const FMassEntityHandle Source, const uint8 AbilitySlot, const FMassEntityHandle ExplicitTarget,
	const float MagnitudeScale, const uint32 CurrentTick)
{
	// 失败检查按固定优先级执行；只有所有检查和输出入队成功才进入冷却。
	// 以下检查顺序也是能力失败原因的优先级顺序：来源、能力、状态、冷却、Tag、目标、范围、输出。
	if (!EntityManager.IsEntityValid(Source) || !FMath::IsFinite(MagnitudeScale) || MagnitudeScale <= 0.0f)
	{
		return EMassAbilityFailure::InvalidSource;
	}
	FMassAbilityFragment* AbilityState = EntityManager.GetFragmentDataPtr<FMassAbilityFragment>(Source);
	const FMassCompiledAbilityLoadout* Loadout = AbilityState ? FindLoadout(AbilityState->AbilityLoadoutId) : nullptr;
	if (!Loadout || AbilitySlot >= Loadout->AbilityCount)
	{
		return EMassAbilityFailure::InvalidAbility;
	}
	const FMassCompiledAbility* Ability = FindAbility(Loadout->AbilityIds[AbilitySlot]);
	const FMassStateFragment* SourceState = EntityManager.GetFragmentDataPtr<FMassStateFragment>(Source);
	const FMassUnitFragment* SourceUnit = EntityManager.GetFragmentDataPtr<FMassUnitFragment>(Source);
	const FTransformFragment* SourceTransform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Source);
	if (!Ability || !SourceState || !SourceUnit || !SourceTransform)
	{
		return EMassAbilityFailure::InvalidAbility;
	}
	if (EnumHasAnyFlags(SourceState->StateFlags,
		EMassStateFlags::AbilityBlocked | EMassStateFlags::Dying | EMassStateFlags::Dead))
	{
		return EMassAbilityFailure::BlockedState;
	}
	if (SourceState->EffectBlockedAbilityTags.ContainsAny(Ability->AbilityTags))
	{
		return EMassAbilityFailure::BlockedAbilityTags;
	}
	if (!FMassSimulationClock::HasReached(CurrentTick, AbilityState->NextReadyTicks[AbilitySlot]))
	{
		return EMassAbilityFailure::Cooldown;
	}
	if (!SourceState->RuntimeTags.ContainsAll(Ability->RequiredOwnerTags)
		|| SourceState->RuntimeTags.ContainsAny(Ability->BlockedOwnerTags))
	{
		return EMassAbilityFailure::OwnerTags;
	}

	// 根据编译后的目标策略确定目标；CurrentTarget 读取 Target Fragment 的缓存结果。
	FMassEntityHandle Target;
	if (Ability->TargetPolicy == EMassAbilityTargetPolicy::Self)
	{
		Target = Source;
	}
	else if (Ability->TargetPolicy == EMassAbilityTargetPolicy::ExplicitTarget)
	{
		Target = ExplicitTarget;
	}
	else if (const FMassTargetFragment* TargetState = EntityManager.GetFragmentDataPtr<FMassTargetFragment>(Source))
	{
		Target = TargetState->Target;
	}
	if (!EntityManager.IsEntityValid(Target))
	{
		return EMassAbilityFailure::InvalidTarget;
	}

	const FMassStateFragment* TargetState = EntityManager.GetFragmentDataPtr<FMassStateFragment>(Target);
	const FMassUnitFragment* TargetUnit = EntityManager.GetFragmentDataPtr<FMassUnitFragment>(Target);
	const FTransformFragment* TargetTransform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Target);
	if (!TargetState || !TargetUnit || !TargetTransform
		|| (Target != Source && !EnumHasAnyFlags(TargetState->StateFlags, EMassStateFlags::Targetable))
		|| EnumHasAnyFlags(TargetState->StateFlags, EMassStateFlags::Dying | EMassStateFlags::Dead))
	{
		return EMassAbilityFailure::InvalidTarget;
	}
	if (Target != Source && SourceUnit->TeamId == TargetUnit->TeamId)
	{
		return EMassAbilityFailure::Team;
	}
	if (!TargetState->RuntimeTags.ContainsAll(Ability->RequiredTargetTags)
		|| TargetState->RuntimeTags.ContainsAny(Ability->BlockedTargetTags))
	{
		return EMassAbilityFailure::TargetTags;
	}
	const FVector SourceLocation = SourceTransform->GetTransform().GetLocation();
	const FVector TargetLocation = TargetTransform->GetTransform().GetLocation();
	// 使用平方距离避免平方根；二维距离适合当前 Mass 空间索引语义。
	const float DistanceSquared = Ability->bUse2DDistance
		? FVector::DistSquared2D(SourceLocation, TargetLocation)
		: FVector::DistSquared(SourceLocation, TargetLocation);
	if (DistanceSquared > Ability->RangeSquared)
	{
		return EMassAbilityFailure::Range;
	}

	const FMassExternalTargetFragment* ExternalTarget =
		EntityManager.GetFragmentDataPtr<FMassExternalTargetFragment>(Target);
	// External Proxy 需要额外验证外部句柄和每个 Output 的 GAS 路由能力。
	if (ExternalTarget)
	{
		if (!ExternalTargetSubsystem || !ExternalTargetSubsystem->IsExternalTargetValid(ExternalTarget->Handle))
		{
			return EMassAbilityFailure::InvalidTarget;
		}
		for (uint8 Index = 0; Index < Ability->OutputCount; ++Index)
		{
			if (!OutputSubsystem || !OutputSubsystem->CanRouteExternalOutput(Ability->OutputIds[Index]))
			{
				return EMassAbilityFailure::ExternalRoute;
			}
		}
	}

	if (!OutputSubsystem)
	{
		return EMassAbilityFailure::OutputRejected;
	}
	// 先在固定容量数组中组装全部输出，再一次性提交，避免只提交部分输出
	TStaticArray<FMassOutputRequest, Massbility::MaxAbilityOutputs> OutputRequests;
	const FVector Direction = (TargetLocation - SourceLocation).GetSafeNormal();
	for (uint8 Index = 0; Index < Ability->OutputCount; ++Index)
	{
		FMassOutputRequest& Request = OutputRequests[Index];
		Request.Source = Source;
		Request.Target = Target;
		Request.OutputId = Ability->OutputIds[Index];
		Request.MagnitudeScale = MagnitudeScale;
		Request.Origin = SourceLocation;
		Request.Direction = Direction;
		Request.SimulationTick = CurrentTick;
	}
	if (!OutputSubsystem->QueueOutputs(MakeArrayView(OutputRequests.GetData(), Ability->OutputCount)))
	{
		return EMassAbilityFailure::OutputRejected;
	}
	// 提交失败不能消耗能力冷却。
	// 只有所有输出成功进入队列后才写入冷却，避免失败请求消耗冷却时间
	AbilityState->NextReadyTicks[AbilitySlot] = CurrentTick + Ability->CooldownTicks;
	return EMassAbilityFailure::None;
}

void UMassAbilitySubsystem::RecordResult(const EMassAbilityFailure Failure)
{
	// 统计按失败枚举直接索引，便于性能统计和调试面板展示。
	++Stats.Attempts;
	if (Failure == EMassAbilityFailure::None)
	{
		++Stats.Succeeded;
		return;
	}
	const uint8 Index = static_cast<uint8>(Failure);
	if (Index < Stats.Failures.Num())
	{
		++Stats.Failures[Index];
	}
}
