// Copyright MassDestruction. All Rights Reserved.

#include "Effect/MassEffectSubsystem.h"

#include "Attributes/MassAttributeAccess.h"
#include "Combat/MassCombatFragments.h"
#include "Combat/MassCombatSubsystem.h"
#include "Core/MassRuntimeSubsystem.h"
#include "Effect/MassEffectDefinition.h"
#include "Entity/MassFragments.h"
#include "Engine/Engine.h"
#include "MassCommandBuffer.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "Mass.h"

namespace
{
	uint32 MakeVariantKey(const uint16 EffectId, const uint16 SchemaId)
	{
		return (static_cast<uint32>(EffectId) << 16) | SchemaId;
	}

}

void UMassEffectSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	MassSubsystem = Collection.InitializeDependency<UMassRuntimeSubsystem>();
	CombatSubsystem = Collection.InitializeDependency<UMassCombatSubsystem>();
	PendingRequests.Reserve(256);
	PendingRemovalRequests.Reserve(64);
}

void UMassEffectSubsystem::Deinitialize()
{
	PendingRequests.Reset();
	PendingRemovalRequests.Reset();
	CompiledVariants.Reset();
	VariantIds.Reset();
	CompiledTemplates.Reset();
	EffectTagIds.Reset();
	EffectIds.Reset();
	EffectDefinitions.Reset();
	MassSubsystem = nullptr;
	CombatSubsystem = nullptr;
	EntitySubsystem = nullptr;
	Super::Deinitialize();
}

bool UMassEffectSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

UMassEffectSubsystem* UMassEffectSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassEffectSubsystem>();
	}
	return nullptr;
}

FMassEffectId UMassEffectSubsystem::RegisterEffect(const UMassEffectDefinition& Definition)
{
	if (const uint16* Existing = EffectIds.Find(&Definition))
	{
		return {*Existing};
	}
	if (CompiledTemplates.Num() >= Massbility::InvalidId)
	{
		UE_LOG(LogMassbility, Error, TEXT("Mass effect registry exhausted."));
		return {};
	}

	FMassCompiledEffectTemplate Compiled;
	if (!CompileTemplate(Definition, Compiled))
	{
		return {};
	}
	if (EffectTagIds.Contains(Compiled.EffectTag))
	{
		UE_LOG(LogMassbility, Error, TEXT("Effect tag '%s' is already registered by another definition."), *Compiled.EffectTag.ToString());
		return {};
	}
	Compiled.EffectId.Value = static_cast<uint16>(CompiledTemplates.Num());
	EffectIds.Add(&Definition, Compiled.EffectId.Value);
	EffectTagIds.Add(Compiled.EffectTag, Compiled.EffectId.Value);
	EffectDefinitions.Add(const_cast<UMassEffectDefinition*>(&Definition));
	CompiledTemplates.Add(MoveTemp(Compiled));
	return {static_cast<uint16>(CompiledTemplates.Num() - 1)};
}

bool UMassEffectSubsystem::PrewarmEffectForSchema(const UMassEffectDefinition& Definition, const uint16 SchemaId)
{
	const FMassEffectId EffectId = RegisterEffect(Definition);
	return EffectId.IsValid() && GetOrCreateVariant(EffectId, SchemaId) != nullptr;
}

bool UMassEffectSubsystem::QueueEffect(const FMassEntityHandle Target, const UMassEffectDefinition& Definition,
	const FMassEntityHandle Source, const float MagnitudeScale)
{
	const FMassEffectId EffectId = RegisterEffect(Definition);
	return QueueRegisteredEffect(Target, EffectId, Source, MagnitudeScale);
}

bool UMassEffectSubsystem::QueueRegisteredEffect(const FMassEntityHandle Target, const FMassEffectId EffectId,
	const FMassEntityHandle Source, const float MagnitudeScale)
{
	if (!Target.IsSet() || !EffectId.IsValid() || !CompiledTemplates.IsValidIndex(EffectId.Value) || !FMath::IsFinite(MagnitudeScale))
	{
		return false;
	}
	PendingRequests.Add({Target, Source, EffectId, MagnitudeScale});
	return true;
}

bool UMassEffectSubsystem::QueueRemoveEffect(const FMassEntityHandle Target, const FMassEffectId EffectId)
{
	if (!Target.IsSet() || !EffectId.IsValid())
	{
		return false;
	}
	PendingRemovalRequests.Add({Target, EffectId, EMassEffectRemovalMode::ByEffectId});
	return true;
}

bool UMassEffectSubsystem::QueueRemoveAllEffects(const FMassEntityHandle Target)
{
	if (!Target.IsSet())
	{
		return false;
	}
	PendingRemovalRequests.Add({Target, {}, EMassEffectRemovalMode::All});
	return true;
}

const FMassCompiledEffectTemplate* UMassEffectSubsystem::FindEffectTemplate(
	const FMassEffectId EffectId) const
{
	return EffectId.IsValid() && CompiledTemplates.IsValidIndex(EffectId.Value)
		? &CompiledTemplates[EffectId.Value] : nullptr;
}

bool UMassEffectSubsystem::FindEffectIdByTag(const FGameplayTag EffectTag,
	FMassEffectId& OutEffectId) const
{
	OutEffectId = {};
	if (const uint16* Existing = EffectTagIds.Find(EffectTag))
	{
		OutEffectId.Value = *Existing;
		return true;
	}
	return false;
}

bool UMassEffectSubsystem::CompileTemplate(const UMassEffectDefinition& Definition, FMassCompiledEffectTemplate& OutTemplate)
{
	FString Error;
	if (!MassSubsystem || !Definition.Validate(Error))
	{
		UE_LOG(LogMassbility, Error, TEXT("Invalid effect definition '%s': %s"), *Definition.GetPathName(), *Error);
		return false;
	}
	OutTemplate = {};
	OutTemplate.EffectTag = Definition.GetEffectTag();
	OutTemplate.DurationPolicy = Definition.GetDurationPolicy();
	OutTemplate.DurationTicks = OutTemplate.DurationPolicy == EMassEffectDurationPolicy::Duration
		? MassSubsystem->GetSimulationClock().SecondsToTicksCeil(Definition.GetDurationSeconds()) : 0;
	OutTemplate.PeriodTicks = MassSubsystem->GetSimulationClock().SecondsToTicksCeil(Definition.GetPeriodSeconds());
	OutTemplate.StackPolicy = Definition.GetStackPolicy();
	OutTemplate.StackLimit = FMath::Max<uint8>(1, Definition.GetStackLimit());
	OutTemplate.Modifiers = Definition.GetModifiers();
	if (!MassSubsystem->GetRuntimeTagCatalog().BuildMask(Definition.GetGrantedTags(), OutTemplate.GrantedTags, Error))
	{
		UE_LOG(LogMassbility, Error, TEXT("Could not compile granted tags for effect '%s': %s"), *Definition.GetPathName(), *Error);
		return false;
	}
	return true;
}

const FMassCompiledEffect* UMassEffectSubsystem::GetOrCreateVariant(const FMassEffectId EffectId, const uint16 SchemaId)
{
	if (!MassSubsystem || !CompiledTemplates.IsValidIndex(EffectId.Value) || !MassSubsystem->FindCompiledSchema(SchemaId))
	{
		return nullptr;
	}
	const uint32 Key = MakeVariantKey(EffectId.Value, SchemaId);
	if (const uint16* Existing = VariantIds.Find(Key))
	{
		return CompiledVariants.IsValidIndex(*Existing) ? &CompiledVariants[*Existing] : nullptr;
	}
	if (CompiledVariants.Num() >= Massbility::InvalidId)
	{
		return nullptr;
	}

	const FMassCompiledEffectTemplate& Template = CompiledTemplates[EffectId.Value];
	FMassCompiledEffect Variant;
	Variant.EffectId = EffectId;
	Variant.VariantId = static_cast<uint16>(CompiledVariants.Num());
	Variant.SchemaId = SchemaId;
	Variant.DurationPolicy = Template.DurationPolicy;
	Variant.DurationTicks = Template.DurationTicks;
	Variant.PeriodTicks = Template.PeriodTicks;
	Variant.StackLimit = Template.StackLimit;
	Variant.StackPolicy = Template.StackPolicy;
	Variant.GrantedTags = Template.GrantedTags;

	for (const FMassEffectModifierDefinition& SourceModifier : Template.Modifiers)
	{
		FMassAttributeHandle Handle;
		if (!MassSubsystem->ResolveAttributeHandle(SchemaId, SourceModifier.AttributeTag, Handle))
		{
			UE_LOG(LogMassbility, Error, TEXT("Effect '%s' references attribute '%s' missing from schema %u."),
				*Template.EffectTag.ToString(), *SourceModifier.AttributeTag.ToString(), SchemaId);
			return nullptr;
		}
		if (SourceModifier.Timing == EMassModifierTiming::Ongoing
			&& Handle.Storage == EMassAttributeStorage::Vital
			&& Handle.Index != static_cast<uint8>(EMassVitalAttribute::MoveSpeed))
		{
			UE_LOG(LogMassbility, Error, TEXT("Effect '%s' uses Ongoing on Health or MaxHealth, which is not reversible in Phase 2."), *Template.EffectTag.ToString());
			return nullptr;
		}
		FMassCompiledEffectModifier& Modifier = Variant.Modifiers[Variant.ModifierCount++];
		Modifier.Attribute = Handle;
		Modifier.Timing = SourceModifier.Timing;
		Modifier.Operation = SourceModifier.Operation;
		Modifier.Magnitude = SourceModifier.Magnitude;
	}

	VariantIds.Add(Key, Variant.VariantId);
	CompiledVariants.Add(Variant);
	return &CompiledVariants.Last();
}

void UMassEffectSubsystem::FlushPendingRequests(FMassEntityManager& EntityManager, FMassExecutionContext& Context, const uint32 CurrentTick)
{
	for (const FMassEffectRequest& Request : PendingRequests)
	{
		ApplyRequest(EntityManager, Context, Request, CurrentTick);
	}
	PendingRequests.Reset();
}

void UMassEffectSubsystem::FlushRemovalRequests(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	for (const FMassEffectRemovalRequest& Request : PendingRemovalRequests)
	{
		RemoveMatchingEffects(EntityManager, Context, Request);
	}
	PendingRemovalRequests.Reset();
}

bool UMassEffectSubsystem::ApplyRequest(FMassEntityManager& EntityManager, FMassExecutionContext& Context,
	const FMassEffectRequest& Request, const uint32 CurrentTick)
{
	if (!EntityManager.IsEntityValid(Request.Target))
	{
		return false;
	}
	FMassUnitFragment* Unit = EntityManager.GetFragmentDataPtr<FMassUnitFragment>(Request.Target);
	FMassVitalFragment* Vitals = EntityManager.GetFragmentDataPtr<FMassVitalFragment>(Request.Target);
	FMassAttributeFragment* Attributes = EntityManager.GetFragmentDataPtr<FMassAttributeFragment>(Request.Target);
	FMassStateFragment* State = EntityManager.GetFragmentDataPtr<FMassStateFragment>(Request.Target);
	FMassCombatFragment* Combat = EntityManager.GetFragmentDataPtr<FMassCombatFragment>(Request.Target);
	FMassActiveEffectFragment* ActiveEffects = EntityManager.GetFragmentDataPtr<FMassActiveEffectFragment>(Request.Target);
	if (!Unit || !Vitals || !Attributes || !State || !Combat || !ActiveEffects
		|| EnumHasAnyFlags(State->StateFlags, EMassStateFlags::Dying | EMassStateFlags::Dead))
	{
		return false;
	}
	const FMassCompiledEffect* Effect = GetOrCreateVariant(Request.EffectId, Unit->AttributeSchemaId);
	if (!Effect)
	{
		return false;
	}

	if (Effect->DurationPolicy == EMassEffectDurationPolicy::Instant)
	{
		const float PreviousHealth = Vitals->Health;
		const bool bApplied = ApplyModifiers(*Effect, EMassModifierTiming::Instant, Request.MagnitudeScale, *Unit, *Vitals, *Attributes);
		CombatSubsystem->RecordAppliedDamage(Request.Target, *Combat, Request.Source, Request.EffectId,
			PreviousHealth - Vitals->Health, Vitals->Health <= 0.0f, CurrentTick);
		return bApplied;
	}

	FMassActiveEffectSlot* ExistingSlot = nullptr;
	FMassActiveEffectSlot* EmptySlot = nullptr;
	for (FMassActiveEffectSlot& Slot : ActiveEffects->Slots)
	{
		if (Slot.IsActive() && Slot.EffectId == Request.EffectId)
		{
			ExistingSlot = &Slot;
			break;
		}
		if (!Slot.IsActive() && !EmptySlot)
		{
			EmptySlot = &Slot;
		}
	}

	if (ExistingSlot)
	{
		if (Effect->StackPolicy == EMassEffectStackPolicy::Reject)
		{
			return false;
		}
		const float PreviousHealth = Vitals->Health;
		ApplyModifiers(*Effect, EMassModifierTiming::Instant, Request.MagnitudeScale, *Unit, *Vitals, *Attributes);
		CombatSubsystem->RecordAppliedDamage(Request.Target, *Combat, Request.Source, Request.EffectId,
			PreviousHealth - Vitals->Health, Vitals->Health <= 0.0f, CurrentTick);
		if (Effect->StackPolicy == EMassEffectStackPolicy::AddStack && ExistingSlot->StackCount < Effect->StackLimit)
		{
			ApplyModifiers(*Effect, EMassModifierTiming::Ongoing, Request.MagnitudeScale, *Unit, *Vitals, *Attributes);
			++ExistingSlot->StackCount;
			ExistingSlot->StackMagnitudeSum += Request.MagnitudeScale;
		}
		ExistingSlot->ExpireTick = Effect->DurationPolicy == EMassEffectDurationPolicy::Duration
			? CurrentTick + Effect->DurationTicks : 0;
		return true;
	}
	if (!EmptySlot)
	{
		return false;
	}

	const float PreviousHealth = Vitals->Health;
	ApplyModifiers(*Effect, EMassModifierTiming::Instant, Request.MagnitudeScale, *Unit, *Vitals, *Attributes);
	CombatSubsystem->RecordAppliedDamage(Request.Target, *Combat, Request.Source, Request.EffectId,
		PreviousHealth - Vitals->Health, Vitals->Health <= 0.0f, CurrentTick);
	ApplyModifiers(*Effect, EMassModifierTiming::Ongoing, Request.MagnitudeScale, *Unit, *Vitals, *Attributes);
	EmptySlot->Source = Request.Source;
	EmptySlot->EffectId = Request.EffectId;
	EmptySlot->VariantId = Effect->VariantId;
	EmptySlot->StackCount = 1;
	EmptySlot->ExpireTick = Effect->DurationPolicy == EMassEffectDurationPolicy::Duration
		? CurrentTick + Effect->DurationTicks : 0;
	EmptySlot->NextPeriodicTick = Effect->PeriodTicks > 0 ? CurrentTick + Effect->PeriodTicks : 0;
	EmptySlot->StackMagnitudeSum = Request.MagnitudeScale;
	if (ActiveEffects->ActiveEffectCount++ == 0)
	{
		Context.Defer().AddTag<FMassHasActiveEffectsTag>(Request.Target);
	}
	RebuildEffectTags(*State, *ActiveEffects);
	MassSubsystem->RebuildRuntimeTags(Request.Target);
	return true;
}

bool UMassEffectSubsystem::RemoveMatchingEffects(FMassEntityManager& EntityManager, FMassExecutionContext& Context,
	const FMassEffectRemovalRequest& Request)
{
	if (!EntityManager.IsEntityValid(Request.Target))
	{
		return false;
	}
	FMassUnitFragment* Unit = EntityManager.GetFragmentDataPtr<FMassUnitFragment>(Request.Target);
	FMassVitalFragment* Vitals = EntityManager.GetFragmentDataPtr<FMassVitalFragment>(Request.Target);
	FMassAttributeFragment* Attributes = EntityManager.GetFragmentDataPtr<FMassAttributeFragment>(Request.Target);
	FMassStateFragment* State = EntityManager.GetFragmentDataPtr<FMassStateFragment>(Request.Target);
	FMassActiveEffectFragment* ActiveEffects = EntityManager.GetFragmentDataPtr<FMassActiveEffectFragment>(Request.Target);
	if (!Unit || !Vitals || !Attributes || !State || !ActiveEffects)
	{
		return false;
	}
	bool bRemoved = false;
	for (FMassActiveEffectSlot& Slot : ActiveEffects->Slots)
	{
		if (!Slot.IsActive() || (Request.Mode == EMassEffectRemovalMode::ByEffectId && !(Slot.EffectId == Request.EffectId)))
		{
			continue;
		}
		if (CompiledVariants.IsValidIndex(Slot.VariantId))
		{
			ApplyModifiers(CompiledVariants[Slot.VariantId], EMassModifierTiming::Ongoing,
				-Slot.StackMagnitudeSum, *Unit, *Vitals, *Attributes);
		}
		Slot.Reset();
		if (ActiveEffects->ActiveEffectCount > 0)
		{
			--ActiveEffects->ActiveEffectCount;
		}
		bRemoved = true;
	}
	if (bRemoved)
	{
		RebuildEffectTags(*State, *ActiveEffects);
		MassSubsystem->RebuildRuntimeTags(Request.Target);
		if (ActiveEffects->ActiveEffectCount == 0)
		{
			Context.Defer().RemoveTag<FMassHasActiveEffectsTag>(Request.Target);
		}
	}
	return bRemoved;
}

void UMassEffectSubsystem::TickEntity(const FMassEntityHandle Entity, FMassExecutionContext& Context, const uint32 CurrentTick,
	const FMassUnitFragment& Unit, FMassVitalFragment& Vitals, FMassAttributeFragment& Attributes,
	FMassStateFragment& State, FMassCombatFragment& Combat, FMassActiveEffectFragment& ActiveEffects)
{
	bool bEffectsChanged = false;
	for (FMassActiveEffectSlot& Slot : ActiveEffects.Slots)
	{
		if (!Slot.IsActive() || !CompiledVariants.IsValidIndex(Slot.VariantId))
		{
			continue;
		}
		const FMassCompiledEffect& Effect = CompiledVariants[Slot.VariantId];
		if (Effect.DurationPolicy == EMassEffectDurationPolicy::Duration
			&& FMassSimulationClock::HasReached(CurrentTick, Slot.ExpireTick))
		{
			ApplyModifiers(Effect, EMassModifierTiming::Ongoing,
				-Slot.StackMagnitudeSum, Unit, Vitals, Attributes);
			Slot.Reset();
			if (ActiveEffects.ActiveEffectCount > 0)
			{
				--ActiveEffects.ActiveEffectCount;
			}
			bEffectsChanged = true;
			continue;
		}
		if (Effect.PeriodTicks > 0 && FMassSimulationClock::HasReached(CurrentTick, Slot.NextPeriodicTick))
		{
			const uint32 DueCount = 1 + (CurrentTick - Slot.NextPeriodicTick) / Effect.PeriodTicks;
			const float PreviousHealth = Vitals.Health;
			ApplyModifiers(Effect, EMassModifierTiming::Periodic,
				Slot.StackMagnitudeSum * static_cast<float>(DueCount), Unit, Vitals, Attributes);
			CombatSubsystem->RecordAppliedDamage(Entity, Combat, Slot.Source, Slot.EffectId,
				PreviousHealth - Vitals.Health, Vitals.Health <= 0.0f, CurrentTick);
			Slot.NextPeriodicTick += DueCount * Effect.PeriodTicks;
		}
	}
	if (bEffectsChanged)
	{
		RebuildEffectTags(State, ActiveEffects);
		FMassTagMask StateMask;
		MassSubsystem->BuildStateMask(State.StateFlags, StateMask);
		State.RuntimeTags = State.BaseTags;
		State.RuntimeTags.Append(StateMask);
		State.RuntimeTags.Append(State.EffectTags);
		if (ActiveEffects.ActiveEffectCount == 0)
		{
			Context.Defer().RemoveTag<FMassHasActiveEffectsTag>(Entity);
		}
	}
}

void UMassEffectSubsystem::RebuildEffectTags(FMassStateFragment& State, const FMassActiveEffectFragment& ActiveEffects) const
{
	State.EffectTags.Reset();
	for (const FMassActiveEffectSlot& Slot : ActiveEffects.Slots)
	{
		if (Slot.IsActive() && CompiledVariants.IsValidIndex(Slot.VariantId))
		{
			State.EffectTags.Append(CompiledVariants[Slot.VariantId].GrantedTags);
		}
	}
}

bool UMassEffectSubsystem::ApplyModifiers(const FMassCompiledEffect& Effect, const EMassModifierTiming Timing,
	const float Scale, const FMassUnitFragment& Unit, FMassVitalFragment& Vitals,
	FMassAttributeFragment& Attributes) const
{
	const FMassCompiledAttributeSchema* Schema = MassSubsystem->FindCompiledSchema(Unit.AttributeSchemaId);
	if (!Schema)
	{
		return false;
	}
	bool bApplied = false;
	for (uint8 Index = 0; Index < Effect.ModifierCount; ++Index)
	{
		const FMassCompiledEffectModifier& Modifier = Effect.Modifiers[Index];
		if (Modifier.Timing != Timing || Modifier.Operation != EMassModifierOp::Add)
		{
			continue;
		}
		const float Delta = Modifier.Magnitude * Scale;
		if (Timing == EMassModifierTiming::Ongoing)
		{
			if (Modifier.Attribute.Storage == EMassAttributeStorage::Vital)
			{
				Vitals.MoveSpeed += Delta;
				bApplied = true;
			}
			else if (Modifier.Attribute.Index < Attributes.AttributeCount)
			{
				Attributes.Values[Modifier.Attribute.Index] += Delta;
				bApplied = true;
			}
		}
		else
		{
			bApplied |= FMassAttributeAccess::AddResolved(*Schema, Vitals, Attributes, Modifier.Attribute, Delta);
		}
	}
	return bApplied || Effect.ModifierCount == 0;
}
