// Copyright MassDestruction. All Rights Reserved.

#include "Targeting/MassbilityGameplayAbilityTargetData.h"

#include "AbilitySystemComponent.h"
#include "Core/MassbilityGASSubsystem.h"
#include "Engine/World.h"
#include "GameplayEffect.h"

UScriptStruct* FGameplayAbilityTargetData_MassEntity::GetScriptStruct() const
{
	return StaticStruct();
}

FTransform FGameplayAbilityTargetData_MassEntity::GetOrigin() const
{
	return bHasHitResult ? FTransform(HitResult.TraceStart) : FTransform::Identity;
}

FVector FGameplayAbilityTargetData_MassEntity::GetEndPoint() const
{
	return bHasHitResult ? HitResult.ImpactPoint : FVector::ZeroVector;
}

void FGameplayAbilityTargetData_MassEntity::AddTargetDataToContext(
	FGameplayEffectContextHandle& Context, const bool bIncludeActorArray) const
{
	if (bHasHitResult)
	{
		Context.AddHitResult(HitResult, true);
	}
}

TArray<FActiveGameplayEffectHandle> FGameplayAbilityTargetData_MassEntity::ApplyGameplayEffectSpec(
	FGameplayEffectSpec& Spec, FPredictionKey PredictionKey)
{
	TArray<FActiveGameplayEffectHandle> Result;
	UAbilitySystemComponent* SourceAbilitySystem = Spec.GetContext().GetInstigatorAbilitySystemComponent();
	UWorld* World = SourceAbilitySystem ? SourceAbilitySystem->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_Client)
	{
		return Result;
	}
	if (UMassbilityGASSubsystem* Bridge = World->GetSubsystem<UMassbilityGASSubsystem>())
	{
		Bridge->ApplyGameplayEffectSpecToMassTarget(Spec, TargetEntity, bHasHitResult ? &HitResult : nullptr);
	}
	// Mass 效果没有 FActiveGameplayEffectHandle；这里返回空数组是有意设计。
	return Result;
}

bool FGameplayAbilityTargetData_MassEntity::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	if (Ar.IsLoading())
	{
		TargetEntity = {};
		HitResult = {};
		bHasHitResult = false;
	}
	bOutSuccess = false;
	return false;
}

FGameplayAbilityTargetDataHandle MassbilityGAS::MakeMassEntityTargetData(
	const FMassEntityHandle TargetEntity, const FHitResult* OptionalHitResult)
{
	FGameplayAbilityTargetData_MassEntity* Data = new FGameplayAbilityTargetData_MassEntity();
	Data->TargetEntity = TargetEntity;
	if (OptionalHitResult)
	{
		Data->HitResult = *OptionalHitResult;
		Data->bHasHitResult = true;
	}
	return FGameplayAbilityTargetDataHandle(Data);
}

