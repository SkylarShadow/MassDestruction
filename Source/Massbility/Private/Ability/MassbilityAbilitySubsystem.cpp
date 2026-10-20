// Copyright MassDestruction. All Rights Reserved.

#include "Ability/MassbilityAbilitySubsystem.h"

#include "Ability/MassbilityAbilityDefinition.h"
#include "Core/MassbilitySubsystem.h"
#include "Effect/MassbilityEffectDefinition.h"
#include "Effect/MassbilityEffectSubsystem.h"
#include "Engine/Engine.h"
#include "Entity/MassbilityFragments.h"
#include "Entity/MassbilityUnitDefinition.h"
#include "External/MassbilityExternalTargetFragments.h"
#include "External/MassbilityExternalTargetSubsystem.h"
#include "Mass/EntityFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Massbility.h"
#include "Output/MassbilityOutputSubsystem.h"

void UMassbilityAbilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	MassbilitySubsystem = Collection.InitializeDependency<UMassbilitySubsystem>();
	EffectSubsystem = Collection.InitializeDependency<UMassbilityEffectSubsystem>();
	ExternalTargetSubsystem = Collection.InitializeDependency<UMassbilityExternalTargetSubsystem>();
	OutputSubsystem = Collection.InitializeDependency<UMassbilityOutputSubsystem>();
	PendingRequests.Reserve(256);
}

void UMassbilityAbilitySubsystem::Deinitialize()
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
	MassbilitySubsystem = nullptr;
	EntitySubsystem = nullptr;
	Super::Deinitialize();
}

bool UMassbilityAbilitySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

UMassbilityAbilitySubsystem* UMassbilityAbilitySubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassbilityAbilitySubsystem>();
	}
	return nullptr;
}

FMassbilityAbilityId UMassbilityAbilitySubsystem::RegisterAbility(const UMassbilityAbilityDefinition& Definition)
{
	// Ability 只编译一次；后续通过 Definition 指针复用已有 ID。
	if (const uint16* Existing = AbilityIds.Find(&Definition))
	{
		return {*Existing};
	}
	if (CompiledAbilities.Num() >= Massbility::InvalidId)
	{
		UE_LOG(LogMassbility, Error, TEXT("Massbility ability registry exhausted."));
		return {};
	}

	FMassbilityCompiledAbility Compiled;
	if (!CompileAbility(Definition, Compiled))
	{
		return {};
	}
	if (AbilityTagIds.Contains(Compiled.AbilityTag))
	{
		UE_LOG(LogMassbility, Error, TEXT("Ability tag '%s' is already registered by another definition."), *Compiled.AbilityTag.ToString());
		return {};
	}
	Compiled.AbilityId.Value = static_cast<uint16>(CompiledAbilities.Num());
	AbilityIds.Add(&Definition, Compiled.AbilityId.Value);
	AbilityTagIds.Add(Compiled.AbilityTag, Compiled.AbilityId.Value);
	AbilityDefinitions.Add(const_cast<UMassbilityAbilityDefinition*>(&Definition));
	CompiledAbilities.Add(Compiled);
	return Compiled.AbilityId;
}

bool UMassbilityAbilitySubsystem::RegisterLoadout(const UMassbilityUnitDefinition& UnitDefinition, uint16& OutLoadoutId)
{
	if (const uint16* Existing = LoadoutIds.Find(&UnitDefinition))
	{
		OutLoadoutId = *Existing;
		return true;
	}
	// Loadout 是 Unit Definition 的能力列表快照，实体运行时只保存 LoadoutId。
	const TArray<TObjectPtr<UMassbilityAbilityDefinition>>& Definitions = UnitDefinition.GetAbilities();
	if (Definitions.Num() > Massbility::MaxAbilitiesPerUnit || CompiledLoadouts.Num() >= Massbility::InvalidId)
	{
		UE_LOG(LogMassbility, Error, TEXT("Unit '%s' has too many abilities or the loadout registry is exhausted."), *UnitDefinition.GetPathName());
		return false;
	}

	FMassbilityCompiledAbilityLoadout Loadout;
	Loadout.LoadoutId = static_cast<uint16>(CompiledLoadouts.Num());
	for (UMassbilityAbilityDefinition* Definition : Definitions)
	{
		if (!Definition)
		{
			UE_LOG(LogMassbility, Error, TEXT("Unit '%s' contains a null ability."), *UnitDefinition.GetPathName());
			return false;
		}
		const FMassbilityAbilityId AbilityId = RegisterAbility(*Definition);
		const FMassbilityCompiledAbility* Ability = FindAbility(AbilityId);
		if (!Ability)
		{
			return false;
		}
		Loadout.AbilityIds[Loadout.AbilityCount++] = AbilityId;
		// 只有 CurrentTarget 能力需要参与自动目标范围的最大值预筛选。
		if (Ability->TargetPolicy == EMassbilityAbilityTargetPolicy::CurrentTarget)
		{
			Loadout.MaxTargetRange = FMath::Max(Loadout.MaxTargetRange, FMath::Sqrt(Ability->RangeSquared));
		}
	}
	OutLoadoutId = Loadout.LoadoutId;
	CompiledLoadouts.Add(Loadout);
	LoadoutIds.Add(&UnitDefinition, OutLoadoutId);
	return true;
}

bool UMassbilityAbilitySubsystem::QueueAbility(const FMassEntityHandle Source, const uint8 AbilitySlot,
	const FMassEntityHandle ExplicitTarget, const float MagnitudeScale)
{
	if (!Source.IsSet() || !FMath::IsFinite(MagnitudeScale) || MagnitudeScale <= 0.0f)
	{
		return false;
	}
	// 这里只做轻量参数检查；实体、槽位、状态、目标和队伍检查延迟到 FlushRequests。
	PendingRequests.Add({Source, ExplicitTarget, AbilitySlot, MagnitudeScale});
	return true;
}

const FMassbilityCompiledAbility* UMassbilityAbilitySubsystem::FindAbility(const FMassbilityAbilityId AbilityId) const
{
	return AbilityId.IsValid() && CompiledAbilities.IsValidIndex(AbilityId.Value) ? &CompiledAbilities[AbilityId.Value] : nullptr;
}

const FMassbilityCompiledAbilityLoadout* UMassbilityAbilitySubsystem::FindLoadout(const uint16 LoadoutId) const
{
	return CompiledLoadouts.IsValidIndex(LoadoutId) ? &CompiledLoadouts[LoadoutId] : nullptr;
}

bool UMassbilityAbilitySubsystem::CompileAbility(const UMassbilityAbilityDefinition& Definition, FMassbilityCompiledAbility& OutAbility)
{
	FString Error;
	if (!MassbilitySubsystem || !EffectSubsystem || !OutputSubsystem || !Definition.Validate(Error))
	{
		UE_LOG(LogMassbility, Error, TEXT("Invalid ability definition '%s': %s"), *Definition.GetPathName(), *Error);
		return false;
	}
	OutAbility = {};
	OutAbility.AbilityTag = Definition.GetAbilityTag();
	// 作者数据在此转换为固定 Tick 和紧凑 Tag Mask，热路径不读取 Data Asset。
	OutAbility.CooldownTicks = MassbilitySubsystem->GetSimulationClock().SecondsToTicksCeil(Definition.GetCooldownSeconds());
	OutAbility.TargetPolicy = Definition.GetTargetPolicy();
	OutAbility.RangeSquared = FMath::Square(Definition.GetRange());
	OutAbility.bUse2DDistance = Definition.Uses2DDistance();
	const FMassbilityCompiledRuntimeTagCatalog& Catalog = MassbilitySubsystem->GetRuntimeTagCatalog();
	if (!Catalog.BuildMask(Definition.GetRequiredOwnerTags(), OutAbility.RequiredOwnerTags, Error)
		|| !Catalog.BuildMask(Definition.GetBlockedOwnerTags(), OutAbility.BlockedOwnerTags, Error)
		|| !Catalog.BuildMask(Definition.GetRequiredTargetTags(), OutAbility.RequiredTargetTags, Error)
		|| !Catalog.BuildMask(Definition.GetBlockedTargetTags(), OutAbility.BlockedTargetTags, Error))
	{
		UE_LOG(LogMassbility, Error, TEXT("Could not compile tags for ability '%s': %s"), *Definition.GetPathName(), *Error);
		return false;
	}
	// 每个能力输出都注册为独立 OutputId，实际由统一 Output Dispatcher 路由。
	for (const FMassbilityAbilityOutputDefinition& SourceOutput : Definition.GetOutputs())
	{
		FMassbilityOutputRegistration Output;
		Output.Type = SourceOutput.Type;
		Output.Damage = SourceOutput.Damage;
		Output.MagnitudeScale = SourceOutput.MagnitudeScale;
		Output.SemanticTag = SourceOutput.SemanticTag;
		Output.RequiredTargetTags = OutAbility.RequiredTargetTags;
		Output.BlockedTargetTags = OutAbility.BlockedTargetTags;
		Output.RelationPolicy = OutAbility.TargetPolicy == EMassbilityAbilityTargetPolicy::Self
			? EMassbilityOutputRelationPolicy::Self
			: (OutAbility.TargetPolicy == EMassbilityAbilityTargetPolicy::ExplicitTarget
				? EMassbilityOutputRelationPolicy::SelfOrHostile : EMassbilityOutputRelationPolicy::Hostile);
		Output.bRequireTargetable = OutAbility.TargetPolicy != EMassbilityAbilityTargetPolicy::Self;
		if (SourceOutput.Type == EMassbilityAbilityOutputType::Effect)
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
		const FMassbilityOutputId OutputId = OutputSubsystem->RegisterOutput(Output);
		if (!OutputId.IsValid())
		{
			return false;
		}
		OutAbility.OutputIds[OutAbility.OutputCount++] = OutputId;
	}
	return true;
}

void UMassbilityAbilitySubsystem::FlushRequests(FMassEntityManager& EntityManager, const uint32 CurrentTick)
{
	// 按入队顺序执行，确保同一 Tick 内请求结果可预测。
	for (const FMassbilityAbilityRequest& Request : PendingRequests)
	{
		RecordResult(TryActivate(EntityManager, Request.Source, Request.AbilitySlot,
			Request.ExplicitTarget, Request.MagnitudeScale, CurrentTick));
	}
	PendingRequests.Reset();
}

EMassbilityAbilityFailure UMassbilityAbilitySubsystem::TryActivate(FMassEntityManager& EntityManager,
	const FMassEntityHandle Source, const uint8 AbilitySlot, const FMassEntityHandle ExplicitTarget,
	const float MagnitudeScale, const uint32 CurrentTick)
{
	// 以下检查顺序也是能力失败原因的优先级顺序：来源、能力、状态、冷却、Tag、目标、范围、输出。
	if (!EntityManager.IsEntityValid(Source) || !FMath::IsFinite(MagnitudeScale) || MagnitudeScale <= 0.0f)
	{
		return EMassbilityAbilityFailure::InvalidSource;
	}
	FMassbilityAbilityFragment* AbilityState = EntityManager.GetFragmentDataPtr<FMassbilityAbilityFragment>(Source);
	const FMassbilityCompiledAbilityLoadout* Loadout = AbilityState ? FindLoadout(AbilityState->AbilityLoadoutId) : nullptr;
	if (!Loadout || AbilitySlot >= Loadout->AbilityCount)
	{
		return EMassbilityAbilityFailure::InvalidAbility;
	}
	const FMassbilityCompiledAbility* Ability = FindAbility(Loadout->AbilityIds[AbilitySlot]);
	const FMassbilityStateFragment* SourceState = EntityManager.GetFragmentDataPtr<FMassbilityStateFragment>(Source);
	const FMassbilityUnitFragment* SourceUnit = EntityManager.GetFragmentDataPtr<FMassbilityUnitFragment>(Source);
	const FTransformFragment* SourceTransform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Source);
	if (!Ability || !SourceState || !SourceUnit || !SourceTransform)
	{
		return EMassbilityAbilityFailure::InvalidAbility;
	}
	if (EnumHasAnyFlags(SourceState->StateFlags,
		EMassbilityStateFlags::AbilityBlocked | EMassbilityStateFlags::Dying | EMassbilityStateFlags::Dead))
	{
		return EMassbilityAbilityFailure::BlockedState;
	}
	if (!FMassbilitySimulationClock::HasReached(CurrentTick, AbilityState->NextReadyTicks[AbilitySlot]))
	{
		return EMassbilityAbilityFailure::Cooldown;
	}
	if (!SourceState->RuntimeTags.ContainsAll(Ability->RequiredOwnerTags)
		|| SourceState->RuntimeTags.ContainsAny(Ability->BlockedOwnerTags))
	{
		return EMassbilityAbilityFailure::OwnerTags;
	}

	// 根据编译后的目标策略确定目标；CurrentTarget 读取 Target Fragment 的缓存结果。
	FMassEntityHandle Target;
	if (Ability->TargetPolicy == EMassbilityAbilityTargetPolicy::Self)
	{
		Target = Source;
	}
	else if (Ability->TargetPolicy == EMassbilityAbilityTargetPolicy::ExplicitTarget)
	{
		Target = ExplicitTarget;
	}
	else if (const FMassbilityTargetFragment* TargetState = EntityManager.GetFragmentDataPtr<FMassbilityTargetFragment>(Source))
	{
		Target = TargetState->Target;
	}
	if (!EntityManager.IsEntityValid(Target))
	{
		return EMassbilityAbilityFailure::InvalidTarget;
	}

	const FMassbilityStateFragment* TargetState = EntityManager.GetFragmentDataPtr<FMassbilityStateFragment>(Target);
	const FMassbilityUnitFragment* TargetUnit = EntityManager.GetFragmentDataPtr<FMassbilityUnitFragment>(Target);
	const FTransformFragment* TargetTransform = EntityManager.GetFragmentDataPtr<FTransformFragment>(Target);
	if (!TargetState || !TargetUnit || !TargetTransform
		|| (Target != Source && !EnumHasAnyFlags(TargetState->StateFlags, EMassbilityStateFlags::Targetable))
		|| EnumHasAnyFlags(TargetState->StateFlags, EMassbilityStateFlags::Dying | EMassbilityStateFlags::Dead))
	{
		return EMassbilityAbilityFailure::InvalidTarget;
	}
	if (Target != Source && SourceUnit->TeamId == TargetUnit->TeamId)
	{
		return EMassbilityAbilityFailure::Team;
	}
	if (!TargetState->RuntimeTags.ContainsAll(Ability->RequiredTargetTags)
		|| TargetState->RuntimeTags.ContainsAny(Ability->BlockedTargetTags))
	{
		return EMassbilityAbilityFailure::TargetTags;
	}
	const FVector SourceLocation = SourceTransform->GetTransform().GetLocation();
	const FVector TargetLocation = TargetTransform->GetTransform().GetLocation();
	// 使用平方距离避免平方根；二维距离适合当前 Mass 空间索引语义。
	const float DistanceSquared = Ability->bUse2DDistance
		? FVector::DistSquared2D(SourceLocation, TargetLocation)
		: FVector::DistSquared(SourceLocation, TargetLocation);
	if (DistanceSquared > Ability->RangeSquared)
	{
		return EMassbilityAbilityFailure::Range;
	}

	const FMassbilityExternalTargetFragment* ExternalTarget =
		EntityManager.GetFragmentDataPtr<FMassbilityExternalTargetFragment>(Target);
	// External Proxy 需要额外验证外部句柄和每个 Output 的 GAS 路由能力。
	if (ExternalTarget)
	{
		if (!ExternalTargetSubsystem || !ExternalTargetSubsystem->IsExternalTargetValid(ExternalTarget->Handle))
		{
			return EMassbilityAbilityFailure::InvalidTarget;
		}
		for (uint8 Index = 0; Index < Ability->OutputCount; ++Index)
		{
			if (!OutputSubsystem || !OutputSubsystem->CanRouteExternalOutput(Ability->OutputIds[Index]))
			{
				return EMassbilityAbilityFailure::ExternalRoute;
			}
		}
	}

	if (!OutputSubsystem)
	{
		return EMassbilityAbilityFailure::OutputRejected;
	}
	// 先在固定容量数组中组装全部输出，再一次性提交，避免只提交部分输出。
	TStaticArray<FMassbilityOutputRequest, Massbility::MaxAbilityOutputs> OutputRequests;
	const FVector Direction = (TargetLocation - SourceLocation).GetSafeNormal();
	for (uint8 Index = 0; Index < Ability->OutputCount; ++Index)
	{
		FMassbilityOutputRequest& Request = OutputRequests[Index];
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
		return EMassbilityAbilityFailure::OutputRejected;
	}
	// 只有所有输出成功进入队列后才写入冷却，避免失败请求消耗冷却时间。
	AbilityState->NextReadyTicks[AbilitySlot] = CurrentTick + Ability->CooldownTicks;
	return EMassbilityAbilityFailure::None;
}

void UMassbilityAbilitySubsystem::RecordResult(const EMassbilityAbilityFailure Failure)
{
	// 统计按失败枚举直接索引，便于性能统计和调试面板展示。
	++Stats.Attempts;
	if (Failure == EMassbilityAbilityFailure::None)
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
