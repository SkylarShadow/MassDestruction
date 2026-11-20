// Copyright MassDestruction. All Rights Reserved.

#include "Attributes/MassAttributeSet.h"

bool FMassAttributeLayoutCompiler::Compile(const TArray<TObjectPtr<UMassAttributeSet>>& Sets, FMassCompiledAttributeLayout& OutLayout, FString& OutError)
{
	OutLayout = {};
	OutError.Reset();

	TSet<FGameplayTag> SeenTags;
	for (const UMassAttributeSet* AttributeSet : Sets)
	{
		if (!AttributeSet)
		{
			OutError = TEXT("Unit AttributeSets contains a null attribute set.");
			return false;
		}

		for (const FMassAttributeDefinition& Definition : AttributeSet->GetAttributes())
		{
			if (!Definition.AttributeTag.IsValid())
			{
				OutError = FString::Printf(TEXT("Attribute set '%s' contains an invalid attribute tag."), *AttributeSet->GetPathName());
				return false;
			}
			if (!FMath::IsFinite(Definition.MinValue) || !FMath::IsFinite(Definition.MaxValue)
				|| !FMath::IsFinite(Definition.DefaultValue) || Definition.MinValue > Definition.MaxValue)
			{
				OutError = FString::Printf(TEXT("Attribute '%s' in set '%s' has invalid default value or bounds."), *Definition.AttributeTag.ToString(), *AttributeSet->GetPathName());
				return false;
			}
			if (SeenTags.Contains(Definition.AttributeTag))
			{
				OutError = FString::Printf(TEXT("Attribute '%s' is declared more than once in the unit's attribute sets."), *Definition.AttributeTag.ToString());
				return false;
			}
			if (OutLayout.AttributeCount >= Massbility::MaxExtendedAttributes)
			{
				OutError = FString::Printf(TEXT("Unit attribute sets exceed the maximum of %d extended attributes."), Massbility::MaxExtendedAttributes);
				return false;
			}

			FMassCompiledAttribute& Compiled = OutLayout.Attributes[OutLayout.AttributeCount++];
			Compiled.AttributeTag = Definition.AttributeTag;
			Compiled.MinValue = Definition.MinValue;
			Compiled.MaxValue = Definition.MaxValue;
			Compiled.DefaultValue = FMath::Clamp(Definition.DefaultValue, Definition.MinValue, Definition.MaxValue);
			SeenTags.Add(Definition.AttributeTag);
		}
	}

	return true;
}
