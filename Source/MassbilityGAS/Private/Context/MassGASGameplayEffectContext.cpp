// Copyright MassDestruction. All Rights Reserved.

#include "Context/MassGASGameplayEffectContext.h"

UScriptStruct* FMassGASGameplayEffectContext::GetScriptStruct() const
{
	return StaticStruct();
}

FMassGASGameplayEffectContext* FMassGASGameplayEffectContext::Duplicate() const
{
	FMassGASGameplayEffectContext* NewContext = new FMassGASGameplayEffectContext();
	*NewContext = *this;
	if (const FHitResult* SourceHitResult = GetHitResult())
	{
		NewContext->AddHitResult(*SourceHitResult, true);
	}
	return NewContext;
}

bool FMassGASGameplayEffectContext::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
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
