// Copyright MassDestruction. All Rights Reserved.

#include "Context/MassbilityGameplayEffectContext.h"

UScriptStruct* FMassbilityGameplayEffectContext::GetScriptStruct() const
{
	return StaticStruct();
}

FMassbilityGameplayEffectContext* FMassbilityGameplayEffectContext::Duplicate() const
{
	FMassbilityGameplayEffectContext* NewContext = new FMassbilityGameplayEffectContext();
	*NewContext = *this;
	if (const FHitResult* SourceHitResult = GetHitResult())
	{
		NewContext->AddHitResult(*SourceHitResult, true);
	}
	return NewContext;
}

bool FMassbilityGameplayEffectContext::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bool bBaseSuccess = false;
	const bool bBaseResult = FGameplayEffectContext::NetSerialize(Ar, Map, bBaseSuccess);
	bool bTagSuccess = true;
	SemanticTag.NetSerialize(Ar, Map, bTagSuccess);
	Ar << RawMagnitude;
	Ar << Origin;
	Ar << Direction;
	Ar << SimulationTick;
	// SourceMassEntity 仅在权威端使用，永远不参与序列化。
	if (Ar.IsLoading())
	{
		SourceMassEntity = {};
	}
	bOutSuccess = bBaseSuccess && bTagSuccess && !Ar.IsError();
	return bBaseResult && bOutSuccess;
}
