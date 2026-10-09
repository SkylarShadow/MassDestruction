// Copyright MassDestruction. All Rights Reserved.

#include "Attributes/MassbilityAttributeSchema.h"

bool UMassbilityAttributeSchema::Compile(FMassbilityCompiledAttributeSchema& OutCompiledSchema, FString& OutError) const
{
	OutCompiledSchema = {};
	OutError.Reset();

	TSet<FGameplayTag> SeenTags;
	for (const UMassbilityAttributeSet* AttributeSet : AttributeSets)
	{
		if (!AttributeSet)
		{
			OutError = FString::Printf(TEXT("Schema '%s' contains a null attribute set."), *GetPathName());
			return false;
		}

		for (const FMassbilityAttributeDefinition& Definition : AttributeSet->GetAttributes())
		{
			if (!Definition.AttributeTag.IsValid())
			{
				OutError = FString::Printf(TEXT("Attribute set '%s' contains an invalid attribute tag."), *AttributeSet->GetPathName());
				return false;
			}
			if (Definition.MinValue > Definition.MaxValue)
			{
				OutError = FString::Printf(TEXT("Attribute '%s' in set '%s' has MinValue greater than MaxValue."), *Definition.AttributeTag.ToString(), *AttributeSet->GetPathName());
				return false;
			}
			if (SeenTags.Contains(Definition.AttributeTag))
			{
				OutError = FString::Printf(TEXT("Attribute '%s' is declared more than once in composed schema '%s'."), *Definition.AttributeTag.ToString(), *GetPathName());
				return false;
			}
			if (OutCompiledSchema.AttributeCount >= Massbility::MaxExtendedAttributes)
			{
				OutError = FString::Printf(TEXT("Schema '%s' exceeds the maximum of %d extended attributes."), *GetPathName(), Massbility::MaxExtendedAttributes);
				return false;
			}

			FMassbilityCompiledAttribute& Compiled = OutCompiledSchema.Attributes[OutCompiledSchema.AttributeCount++];
			Compiled.AttributeTag = Definition.AttributeTag;
			Compiled.MinValue = Definition.MinValue;
			Compiled.MaxValue = Definition.MaxValue;
			Compiled.DefaultValue = FMath::Clamp(Definition.DefaultValue, Definition.MinValue, Definition.MaxValue);
			SeenTags.Add(Definition.AttributeTag);
		}
	}

	return true;
}
