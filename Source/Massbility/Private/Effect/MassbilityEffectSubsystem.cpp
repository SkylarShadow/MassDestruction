// Copyright MassDestruction. All Rights Reserved.

#include "Effect/MassbilityEffectSubsystem.h"

#include "Attributes/MassbilityAttributeAccess.h"
#include "Combat/MassbilityCombatFragments.h"
#include "Combat/MassbilityCombatSubsystem.h"
#include "Core/MassbilitySubsystem.h"
#include "Effect/MassbilityEffectDefinition.h"
#include "Entity/MassbilityFragments.h"
#include "Engine/Engine.h"
#include "MassCommandBuffer.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "Massbility.h"

namespace
{
	uint32 MakeVariantKey(const uint16 EffectId, const uint16 SchemaId)
	{
		return (static_cast<uint32>(EffectId) << 16) | SchemaId;
	}

}

void UMassbilityEffectSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	MassbilitySubsystem = Collection.InitializeDependency<UMassbilitySubsystem>();
	CombatSubsystem = Collection.InitializeDependency<UMassbilityCombatSubsystem>();
	PendingRequests.Reserve(256);
	PendingRemovalRequests.Reserve(64);
}

void UMassbilityEffectSubsystem::Deinitialize()
{
	PendingRequests.Reset();
	PendingRemovalRequests.Reset();
	CompiledVariants.Reset();
	VariantIds.Reset();
	CompiledTemplates.Reset();
	EffectTagIds.Reset();
	EffectIds.Reset();
	EffectDefinitions.Reset();
	MassbilitySubsystem = nullptr;
	CombatSubsystem = nullptr;
	EntitySubsystem = nullptr;
	Super::Deinitialize();
}

bool UMassbilityEffectSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

UMassbilityEffectSubsystem* UMassbilityEffectSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassbilityEffectSubsystem>();
	}
	return nullptr;
}

FMassbilityEffectId UMassbilityEffectSubsystem::RegisterEffect(const UMassbilityEffectDefinition& Definition)
{
	if (const uint16* Existing = EffectIds.Find(&Definition))
	{
		return {*Existing};
	}
	if (CompiledTemplates.Num() >= Massbility::InvalidId)
	{
		UE_LOG(LogMassbility, Error, TEXT("Massbility effect registry exhausted."));
		return {};
	}

	FMassbilityCompiledEffectTemplate Compiled;
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
	EffectDefinitions.Add(const_cast<UMassbilityEffectDefinition*>(&Definition));
	CompiledTemplates.Add(MoveTemp(Compiled));
	return {static_cast<uint16>(CompiledTemplates.Num() - 1)};
}

bool UMassbilityEffectSubsystem::PrewarmEffectForSchema(const UMassbilityEffectDefinition& Definition, const uint16 SchemaId)
{
	const FMassbilityEffectId EffectId = RegisterEffect(Definition);
	return EffectId.IsValid() && GetOrCreateVariant(EffectId, SchemaId) != nullptr;
}

bool UMassbilityEffectSubsystem::QueueEffect(const FMassEntityHandle Target, const UMassbilityEffectDefinition& Definition,
	const FMassEntityHandle Source, const float MagnitudeScale)
{
	const FMassbilityEffectId EffectId = RegisterEffect(Definition);
	return QueueRegisteredEffect(Target, EffectId, Source, MagnitudeScale);
}

bool UMassbilityEffectSubsystem::QueueRegisteredEffect(const FMassEntityHandle Target, const FMassbilityEffectId EffectId,
	const FMassEntityHandle Source, const float MagnitudeScale)
{
	if (!Target.IsSet() || !EffectId.IsValid() || !CompiledTemplates.IsValidIndex(EffectId.Value) || !FMath::IsFinite(MagnitudeScale))
	{
		return false;
	}
	PendingRequests.Add({Target, Source, EffectId, MagnitudeScale});
	return true;
}

bool UMassbilityEffectSubsystem::QueueRemoveEffect(const FMassEntityHandle Target, const FMassbilityEffectId EffectId)
{
	if (!Target.IsSet() || !EffectId.IsValid())
	{
		return false;
	}
	PendingRemovalRequests.Add({Target, EffectId, EMassbilityEffectRemovalMode::ByEffectId});
	return true;
}

bool UMassbilityEffectSubsystem::QueueRemoveAllEffects(const FMassEntityHandle Target)
{
	if (!Target.IsSet())
	{
		return false;
	}
	PendingRemovalRequests.Add({Target, {}, EMassbilityEffectRemovalMode::All});
	return true;
}

const FMassbilityCompiledEffectTemplate* UMassbilityEffectSubsystem::FindEffectTemplate(
	const FMassbilityEffectId EffectId) const
{
	return EffectId.IsValid() && CompiledTemplates.IsValidIndex(EffectId.Value)
		? &CompiledTemplates[EffectId.Value] : nullptr;
}

bool UMassbilityEffectSubsystem::FindEffectIdByTag(const FGameplayTag EffectTag,
	FMassbilityEffectId& OutEffectId) const
{
	OutEffectId = {};
	if (const uint16* Existing = EffectTagIds.Find(EffectTag))
	{
		OutEffectId.Value = *Existing;
		return true;
	}
	return false;
}

bool UMassbilityEffectSubsystem::CompileTemplate(const UMassbilityEffectDefinition& Definition, FMassbilityCompiledEffectTemplate& OutTemplate)
{
	FString Error;
	if (!MassbilitySubsystem || !Definition.Validate(Error))
	{
		UE_LOG(LogMassbility, Error, TEXT("Invalid effect definition '%s': %s"), *Definition.GetPathName(), *Error);
		return false;
	}
	OutTemplate = {};
	OutTemplate.EffectTag = Definition.GetEffectTag();
	OutTemplate.DurationPolicy = Definition.GetDurationPolicy();
	OutTemplate.DurationTicks = OutTemplate.DurationPolicy == EMassbilityEffectDurationPolicy::Duration
		? MassbilitySubsystem->GetSimulationClock().SecondsToTicksCeil(Definition.GetDurationSeconds()) : 0;
	OutTemplate.PeriodTicks = MassbilitySubsystem->GetSimulationClock().SecondsToTicksCeil(Definition.GetPeriodSeconds());
	OutTemplate.StackPolicy = Definition.GetStackPolicy();
	OutTemplate.StackLimit = FMath::Max<uint8>(1, Definition.GetStackLimit());
	OutTemplate.Modifiers = Definition.GetModifiers();
	if (!MassbilitySubsystem->GetRuntimeTagCatalog().BuildMask(Definition.GetGrantedTags(), OutTemplate.GrantedTags, Error))
	{
		UE_LOG(LogMassbility, Error, TEXT("Could not compile granted tags for effect '%s': %s"), *Definition.GetPathName(), *Error);
		return false;
	}
	return true;
}

const FMassbilityCompiledEffect* UMassbilityEffectSubsystem::GetOrCreateVariant(const FMassbilityEffectId EffectId, const uint16 SchemaId)
{
	if (!MassbilitySubsystem || !CompiledTemplates.IsValidIndex(EffectId.Value) || !MassbilitySubsystem->FindCompiledSchema(SchemaId))
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

	const FMassbilityCompiledEffectTemplate& Template = CompiledTemplates[EffectId.Value];
	FMassbilityCompiledEffect Variant;
	Variant.EffectId = EffectId;
	Variant.VariantId = static_cast<uint16>(CompiledVariants.Num());
	Variant.SchemaId = SchemaId;
	Variant.DurationPolicy = Template.DurationPolicy;
	Variant.DurationTicks = Template.DurationTicks;
	Variant.PeriodTicks = Template.PeriodTicks;
	Variant.StackLimit = Template.StackLimit;
	Variant.StackPolicy = Template.StackPolicy;
	Variant.GrantedTags = Template.GrantedTags;

	for (const FMassbilityEffectModifierDefinition& SourceModifier : Template.Modifiers)
	{
		FMassbilityAttributeHandle Handle;
		if (!MassbilitySubsystem->ResolveAttributeHandle(SchemaId, SourceModifier.AttributeTag, Handle))
		{
			UE_LOG(LogMassbility, Error, TEXT("Effect '%s' references attribute '%s' missing from schema %u."),
				*Template.EffectTag.ToString(), *SourceModifier.AttributeTag.ToString(), SchemaId);
			return nullptr;
		}
		if (SourceModifier.Timing == EMassbilityModifierTiming::Ongoing
			&& Handle.Storage == EMassbilityAttributeStorage::Vital
			&& Handle.Index != static_cast<uint8>(EMassbilityVitalAttribute::MoveSpeed))
		{
			UE_LOG(LogMassbility, Error, TEXT("Effect '%s' uses Ongoing on Health or MaxHealth, which is not reversible in Phase 2."), *Template.EffectTag.ToString());
			return nullptr;
		}
		FMassbilityCompiledEffectModifier& Modifier = Variant.Modifiers[Variant.ModifierCount++];
		Modifier.Attribute = Handle;
		Modifier.Timing = SourceModifier.Timing;
		Modifier.Operation = SourceModifier.Operation;
		Modifier.Magnitude = SourceModifier.Magnitude;
	}

	VariantIds.Add(Key, Variant.VariantId);
	CompiledVariants.Add(Variant);
	return &CompiledVariants.Last();
}

void UMassbilityEffectSubsystem::FlushPendingRequests(FMassEntityManager& EntityManager, FMassExecutionContext& Context, const uint32 CurrentTick)
{
	for (const FMassbilityEffectRequest& Request : PendingRequests)
	{
		ApplyRequest(EntityManager, Context, Request, CurrentTick);
	}
	PendingRequests.Reset();
}

void UMassbilityEffectSubsystem::FlushRemovalRequests(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	for (const FMassbilityEffectRemovalRequest& Request : PendingRemovalRequests)
	{
		RemoveMatchingEffects(EntityManager, Context, Request);
	}
	PendingRemovalRequests.Reset();
}

bool UMassbilityEffectSubsystem::ApplyRequest(FMassEntityManager& EntityManager, FMassExecutionContext& Context,
	const FMassbilityEffectRequest& Request, const uint32 CurrentTick)
{
	if (!EntityManager.IsEntityValid(Request.Target))
	{
		return false;
	}
	FMassbilityUnitFragment* Unit = EntityManager.GetFragmentDataPtr<FMassbilityUnitFragment>(Request.Target);
	FMassbilityVitalFragment* Vitals = EntityManager.GetFragmentDataPtr<FMassbilityVitalFragment>(Request.Target);
	FMassbilityAttributeFragment* Attributes = EntityManager.GetFragmentDataPtr<FMassbilityAttributeFragment>(Request.Target);
	FMassbilityStateFragment* State = EntityManager.GetFragmentDataPtr<FMassbilityStateFragment>(Request.Target);
	FMassbilityCombatFragment* Combat = EntityManager.GetFragmentDataPtr<FMassbilityCombatFragment>(Request.Target);
	FMassbilityActiveEffectFragment* ActiveEffects = EntityManager.GetFragmentDataPtr<FMassbilityActiveEffectFragment>(Request.Target);
	if (!Unit || !Vitals || !Attributes || !State || !Combat || !ActiveEffects
		|| EnumHasAnyFlags(State->StateFlags, EMassbilityStateFlags::Dying | EMassbilityStateFlags::Dead))
	{
		return false;
	}
	const FMassbilityCompiledEffect* Effect = GetOrCreateVariant(Request.EffectId, Unit->AttributeSchemaId);
	if (!Effect)
	{
		return false;
	}

	if (Effect->DurationPolicy == EMassbilityEffectDurationPolicy::Instant)
	{
		const float PreviousHealth = Vitals->Health;
		const bool bApplied = ApplyModifiers(*Effect, EMassbilityModifierTiming::Instant, Request.MagnitudeScale, *Unit, *Vitals, *Attributes);
		CombatSubsystem->RecordAppliedDamage(Request.Target, *Combat, Request.Source, Request.EffectId,
			PreviousHealth - Vitals->Health, Vitals->Health <= 0.0f, CurrentTick);
		return bApplied;
	}

	FMassbilityActiveEffectSlot* ExistingSlot = nullptr;
	FMassbilityActiveEffectSlot* EmptySlot = nullptr;
	for (FMassbilityActiveEffectSlot& Slot : ActiveEffects->Slots)
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
		if (Effect->StackPolicy == EMassbilityEffectStackPolicy::Reject)
		{
			return false;
		}
		const float PreviousHealth = Vitals->Health;
		ApplyModifiers(*Effect, EMassbilityModifierTiming::Instant, Request.MagnitudeScale, *Unit, *Vitals, *Attributes);
		CombatSubsystem->RecordAppliedDamage(Request.Target, *Combat, Request.Source, Request.EffectId,
			PreviousHealth - Vitals->Health, Vitals->Health <= 0.0f, CurrentTick);
		if (Effect->StackPolicy == EMassbilityEffectStackPolicy::AddStack && ExistingSlot->StackCount < Effect->StackLimit)
		{
			ApplyModifiers(*Effect, EMassbilityModifierTiming::Ongoing, Request.MagnitudeScale, *Unit, *Vitals, *Attributes);
			++ExistingSlot->StackCount;
			ExistingSlot->StackMagnitudeSum += Request.MagnitudeScale;
		}
		ExistingSlot->ExpireTick = Effect->DurationPolicy == EMassbilityEffectDurationPolicy::Duration
			? CurrentTick + Effect->DurationTicks : 0;
		return true;
	}
	if (!EmptySlot)
	{
		return false;
	}

	const float PreviousHealth = Vitals->Health;
	ApplyModifiers(*Effect, EMassbilityModifierTiming::Instant, Request.MagnitudeScale, *Unit, *Vitals, *Attributes);
	CombatSubsystem->RecordAppliedDamage(Request.Target, *Combat, Request.Source, Request.EffectId,
		PreviousHealth - Vitals->Health, Vitals->Health <= 0.0f, CurrentTick);
	ApplyModifiers(*Effect, EMassbilityModifierTiming::Ongoing, Request.MagnitudeScale, *Unit, *Vitals, *Attributes);
	EmptySlot->Source = Request.Source;
	EmptySlot->EffectId = Request.EffectId;
	EmptySlot->VariantId = Effect->VariantId;
	EmptySlot->StackCount = 1;
	EmptySlot->ExpireTick = Effect->DurationPolicy == EMassbilityEffectDurationPolicy::Duration
		? CurrentTick + Effect->DurationTicks : 0;
	EmptySlot->NextPeriodicTick = Effect->PeriodTicks > 0 ? CurrentTick + Effect->PeriodTicks : 0;
	EmptySlot->StackMagnitudeSum = Request.MagnitudeScale;
	if (ActiveEffects->ActiveEffectCount++ == 0)
	{
		Context.Defer().AddTag<FMassbilityHasActiveEffectsTag>(Request.Target);
	}
	RebuildEffectTags(*State, *ActiveEffects);
	MassbilitySubsystem->RebuildRuntimeTags(Request.Target);
	return true;
}

bool UMassbilityEffectSubsystem::RemoveMatchingEffects(FMassEntityManager& EntityManager, FMassExecutionContext& Context,
	const FMassbilityEffectRemovalRequest& Request)
{
	if (!EntityManager.IsEntityValid(Request.Target))
	{
		return false;
	}
	FMassbilityUnitFragment* Unit = EntityManager.GetFragmentDataPtr<FMassbilityUnitFragment>(Request.Target);
	FMassbilityVitalFragment* Vitals = EntityManager.GetFragmentDataPtr<FMassbilityVitalFragment>(Request.Target);
	FMassbilityAttributeFragment* Attributes = EntityManager.GetFragmentDataPtr<FMassbilityAttributeFragment>(Request.Target);
	FMassbilityStateFragment* State = EntityManager.GetFragmentDataPtr<FMassbilityStateFragment>(Request.Target);
	FMassbilityActiveEffectFragment* ActiveEffects = EntityManager.GetFragmentDataPtr<FMassbilityActiveEffectFragment>(Request.Target);
	if (!Unit || !Vitals || !Attributes || !State || !ActiveEffects)
	{
		return false;
	}
	bool bRemoved = false;
	for (FMassbilityActiveEffectSlot& Slot : ActiveEffects->Slots)
	{
		if (!Slot.IsActive() || (Request.Mode == EMassbilityEffectRemovalMode::ByEffectId && !(Slot.EffectId == Request.EffectId)))
		{
			continue;
		}
		if (CompiledVariants.IsValidIndex(Slot.VariantId))
		{
			ApplyModifiers(CompiledVariants[Slot.VariantId], EMassbilityModifierTiming::Ongoing,
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
		MassbilitySubsystem->RebuildRuntimeTags(Request.Target);
		if (ActiveEffects->ActiveEffectCount == 0)
		{
			Context.Defer().RemoveTag<FMassbilityHasActiveEffectsTag>(Request.Target);
		}
	}
	return bRemoved;
}

void UMassbilityEffectSubsystem::TickEntity(const FMassEntityHandle Entity, FMassExecutionContext& Context, const uint32 CurrentTick,
	const FMassbilityUnitFragment& Unit, FMassbilityVitalFragment& Vitals, FMassbilityAttributeFragment& Attributes,
	FMassbilityStateFragment& State, FMassbilityCombatFragment& Combat, FMassbilityActiveEffectFragment& ActiveEffects)
{
	bool bEffectsChanged = false;
	for (FMassbilityActiveEffectSlot& Slot : ActiveEffects.Slots)
	{
		if (!Slot.IsActive() || !CompiledVariants.IsValidIndex(Slot.VariantId))
		{
			continue;
		}
		const FMassbilityCompiledEffect& Effect = CompiledVariants[Slot.VariantId];
		if (Effect.DurationPolicy == EMassbilityEffectDurationPolicy::Duration
			&& FMassbilitySimulationClock::HasReached(CurrentTick, Slot.ExpireTick))
		{
			ApplyModifiers(Effect, EMassbilityModifierTiming::Ongoing,
				-Slot.StackMagnitudeSum, Unit, Vitals, Attributes);
			Slot.Reset();
			if (ActiveEffects.ActiveEffectCount > 0)
			{
				--ActiveEffects.ActiveEffectCount;
			}
			bEffectsChanged = true;
			continue;
		}
		if (Effect.PeriodTicks > 0 && FMassbilitySimulationClock::HasReached(CurrentTick, Slot.NextPeriodicTick))
		{
			const uint32 DueCount = 1 + (CurrentTick - Slot.NextPeriodicTick) / Effect.PeriodTicks;
			const float PreviousHealth = Vitals.Health;
			ApplyModifiers(Effect, EMassbilityModifierTiming::Periodic,
				Slot.StackMagnitudeSum * static_cast<float>(DueCount), Unit, Vitals, Attributes);
			CombatSubsystem->RecordAppliedDamage(Entity, Combat, Slot.Source, Slot.EffectId,
				PreviousHealth - Vitals.Health, Vitals.Health <= 0.0f, CurrentTick);
			Slot.NextPeriodicTick += DueCount * Effect.PeriodTicks;
		}
	}
	if (bEffectsChanged)
	{
		RebuildEffectTags(State, ActiveEffects);
		FMassbilityTagMask StateMask;
		MassbilitySubsystem->BuildStateMask(State.StateFlags, StateMask);
		State.RuntimeTags = State.BaseTags;
		State.RuntimeTags.Append(StateMask);
		State.RuntimeTags.Append(State.EffectTags);
		if (ActiveEffects.ActiveEffectCount == 0)
		{
			Context.Defer().RemoveTag<FMassbilityHasActiveEffectsTag>(Entity);
		}
	}
}

void UMassbilityEffectSubsystem::RebuildEffectTags(FMassbilityStateFragment& State, const FMassbilityActiveEffectFragment& ActiveEffects) const
{
	State.EffectTags.Reset();
	for (const FMassbilityActiveEffectSlot& Slot : ActiveEffects.Slots)
	{
		if (Slot.IsActive() && CompiledVariants.IsValidIndex(Slot.VariantId))
		{
			State.EffectTags.Append(CompiledVariants[Slot.VariantId].GrantedTags);
		}
	}
}

bool UMassbilityEffectSubsystem::ApplyModifiers(const FMassbilityCompiledEffect& Effect, const EMassbilityModifierTiming Timing,
	const float Scale, const FMassbilityUnitFragment& Unit, FMassbilityVitalFragment& Vitals,
	FMassbilityAttributeFragment& Attributes) const
{
	const FMassbilityCompiledAttributeSchema* Schema = MassbilitySubsystem->FindCompiledSchema(Unit.AttributeSchemaId);
	if (!Schema)
	{
		return false;
	}
	bool bApplied = false;
	for (uint8 Index = 0; Index < Effect.ModifierCount; ++Index)
	{
		const FMassbilityCompiledEffectModifier& Modifier = Effect.Modifiers[Index];
		if (Modifier.Timing != Timing || Modifier.Operation != EMassbilityModifierOp::Add)
		{
			continue;
		}
		const float Delta = Modifier.Magnitude * Scale;
		if (Timing == EMassbilityModifierTiming::Ongoing)
		{
			if (Modifier.Attribute.Storage == EMassbilityAttributeStorage::Vital)
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
			bApplied |= FMassbilityAttributeAccess::AddResolved(*Schema, Vitals, Attributes, Modifier.Attribute, Delta);
		}
	}
	return bApplied || Effect.ModifierCount == 0;
}
