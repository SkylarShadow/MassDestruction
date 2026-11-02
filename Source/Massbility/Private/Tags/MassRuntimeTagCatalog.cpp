// Copyright MassDestruction. All Rights Reserved.

#include "Tags/MassRuntimeTagCatalog.h"

#include "GameplayTagsManager.h"

namespace
{
	void AddTagAndParents(const FGameplayTag Tag, TArray<FGameplayTag>& InOutTags)
	{
		if (!Tag.IsValid())
		{
			return;
		}

		FString TagName = Tag.ToString();
		while (!TagName.IsEmpty())
		{
			const FGameplayTag CurrentTag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagName), false);
			if (CurrentTag.IsValid() && !InOutTags.Contains(CurrentTag))
			{
				InOutTags.Add(CurrentTag);
			}

			int32 SeparatorIndex = INDEX_NONE;
			if (!TagName.FindLastChar(TEXT('.'), SeparatorIndex))
			{
				break;
			}
			TagName.LeftInline(SeparatorIndex, EAllowShrinking::No);
		}
	}
}

bool FMassCompiledRuntimeTagCatalog::Compile(const TArray<FGameplayTag>& InRuntimeTags, FString& OutError)
{
	TagsByIndex.Reset();
	TagToIndex.Reset();
	bCompiled = false;
	OutError.Reset();

	for (const FGameplayTag Tag : InRuntimeTags)
	{
		if (!Tag.IsValid())
		{
			OutError = TEXT("Runtime tag catalog contains an invalid gameplay tag.");
			return false;
		}
		AddTagAndParents(Tag, TagsByIndex);
	}

	if (TagsByIndex.Num() > Massbility::MaxRuntimeTags)
	{
		OutError = FString::Printf(TEXT("Runtime tag catalog expands to %d tags, exceeding the maximum of %d."), TagsByIndex.Num(), Massbility::MaxRuntimeTags);
		TagsByIndex.Reset();
		return false;
	}

	for (int32 Index = 0; Index < TagsByIndex.Num(); ++Index)
	{
		TagToIndex.Add(TagsByIndex[Index], static_cast<uint16>(Index));
	}
	bCompiled = true;
	return true;
}

bool FMassCompiledRuntimeTagCatalog::BuildMask(const FGameplayTagContainer& Tags, FMassTagMask& OutMask, FString& OutError) const
{
	TArray<FGameplayTag> TagArray;
	Tags.GetGameplayTagArray(TagArray);
	return BuildMaskInternal(TagArray, OutMask, OutError);
}

bool FMassCompiledRuntimeTagCatalog::BuildMask(const TArray<FGameplayTag>& Tags, FMassTagMask& OutMask, FString& OutError) const
{
	return BuildMaskInternal(Tags, OutMask, OutError);
}

void FMassCompiledRuntimeTagCatalog::BuildMaskFiltered(const FGameplayTagContainer& Tags,
	FMassTagMask& OutMask) const
{
	OutMask.Reset();
	if (!bCompiled)
	{
		return;
	}
	TArray<FGameplayTag> TagArray;
	Tags.GetGameplayTagArray(TagArray);
	for (const FGameplayTag Tag : TagArray)
	{
		if (!Tag.IsValid())
		{
			continue;
		}
		for (int32 Index = 0; Index < TagsByIndex.Num(); ++Index)
		{
			if (Tag.MatchesTag(TagsByIndex[Index]))
			{
				OutMask.Set(static_cast<uint16>(Index));
			}
		}
	}
}

bool FMassCompiledRuntimeTagCatalog::BuildMaskInternal(const TConstArrayView<FGameplayTag> Tags, FMassTagMask& OutMask, FString& OutError) const
{
	OutMask.Reset();
	OutError.Reset();
	if (!bCompiled)
	{
		OutError = TEXT("Runtime tag catalog has not been compiled.");
		return false;
	}

	for (const FGameplayTag Tag : Tags)
	{
		if (!Tag.IsValid())
		{
			OutError = TEXT("Cannot compile an invalid gameplay tag into a Mass runtime mask.");
			return false;
		}
		bool bMatchedCatalogTag = false;
		for (int32 Index = 0; Index < TagsByIndex.Num(); ++Index)
		{
			if (Tag.MatchesTag(TagsByIndex[Index]))
			{
				OutMask.Set(static_cast<uint16>(Index));
				bMatchedCatalogTag = true;
			}
		}
		if (!bMatchedCatalogTag)
		{
			OutError = FString::Printf(TEXT("Gameplay tag '%s' is not included in the Mass runtime tag catalog."), *Tag.ToString());
			return false;
		}
	}
	return true;
}
