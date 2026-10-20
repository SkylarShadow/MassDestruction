// Copyright MassDestruction. All Rights Reserved.

#include "Effect/MassbilityEffectDefinition.h"

bool UMassbilityEffectDefinition::Validate(FString& OutError) const
{
	OutError.Reset();
	if (!EffectTag.IsValid())
	{
		OutError = TEXT("EffectTag is invalid.");
		return false;
	}
	if (!FMath::IsFinite(DurationSeconds) || DurationSeconds < 0.0f || !FMath::IsFinite(PeriodSeconds) || PeriodSeconds < 0.0f)
	{
		OutError = TEXT("Duration and period must be finite non-negative values.");
		return false;
	}
	if (DurationPolicy == EMassbilityEffectDurationPolicy::Duration && DurationSeconds <= 0.0f)
	{
		OutError = TEXT("Duration policy requires DurationSeconds greater than zero.");
		return false;
	}
	if (DurationPolicy != EMassbilityEffectDurationPolicy::Duration && DurationSeconds > 0.0f)
	{
		OutError = TEXT("DurationSeconds is only valid for the Duration policy.");
		return false;
	}
	if (DurationPolicy == EMassbilityEffectDurationPolicy::Instant && !GrantedTags.IsEmpty())
	{
		OutError = TEXT("Instant effects cannot grant persistent tags.");
		return false;
	}
	if (StackLimit == 0)
	{
		OutError = TEXT("StackLimit must be at least one.");
		return false;
	}
	if (Modifiers.Num() > Massbility::MaxEffectModifiers)
	{
		OutError = FString::Printf(TEXT("Effect contains %d modifiers; the maximum is %d."), Modifiers.Num(), Massbility::MaxEffectModifiers);
		return false;
	}

	for (const FMassbilityEffectModifierDefinition& Modifier : Modifiers)
	{
		if (!Modifier.AttributeTag.IsValid() || !FMath::IsFinite(Modifier.Magnitude))
		{
			OutError = TEXT("Every modifier needs a valid attribute tag and finite magnitude.");
			return false;
		}
		if (DurationPolicy == EMassbilityEffectDurationPolicy::Instant && Modifier.Timing != EMassbilityModifierTiming::Instant)
		{
			OutError = TEXT("An instant effect can contain only Instant modifiers.");
			return false;
		}
		if (Modifier.Timing == EMassbilityModifierTiming::Periodic && PeriodSeconds <= 0.0f)
		{
			OutError = TEXT("A Periodic modifier requires PeriodSeconds greater than zero.");
			return false;
		}
	}
	return true;
}
