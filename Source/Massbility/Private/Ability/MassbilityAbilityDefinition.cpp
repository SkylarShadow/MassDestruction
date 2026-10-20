// Copyright MassDestruction. All Rights Reserved.

#include "Ability/MassbilityAbilityDefinition.h"

bool UMassbilityAbilityDefinition::Validate(FString& OutError) const
{
	OutError.Reset();
	if (!AbilityTag.IsValid())
	{
		OutError = TEXT("AbilityTag is invalid.");
		return false;
	}
	if (!FMath::IsFinite(CooldownSeconds) || CooldownSeconds < 0.0f || !FMath::IsFinite(Range) || Range < 0.0f)
	{
		OutError = TEXT("Cooldown and range must be finite non-negative values.");
		return false;
	}
	if (Outputs.IsEmpty() || Outputs.Num() > Massbility::MaxAbilityOutputs)
	{
		OutError = FString::Printf(TEXT("Ability needs 1..%d outputs."), Massbility::MaxAbilityOutputs);
		return false;
	}
	for (const FMassbilityAbilityOutputDefinition& Output : Outputs)
	{
		if (!FMath::IsFinite(Output.MagnitudeScale) || Output.MagnitudeScale <= 0.0f)
		{
			OutError = TEXT("Output MagnitudeScale must be finite and positive.");
			return false;
		}
		if (Output.Type == EMassbilityAbilityOutputType::Effect && !Output.Effect)
		{
			OutError = TEXT("Effect output requires an Effect Definition.");
			return false;
		}
		if (Output.Type == EMassbilityAbilityOutputType::Damage && (!FMath::IsFinite(Output.Damage) || Output.Damage <= 0.0f))
		{
			OutError = TEXT("Damage output requires a finite positive Damage value.");
			return false;
		}
		if (Output.Type == EMassbilityAbilityOutputType::Damage && !Output.SemanticTag.IsValid())
		{
			OutError = TEXT("Damage output requires a valid SemanticTag for cross-runtime routing.");
			return false;
		}
	}
	return true;
}
