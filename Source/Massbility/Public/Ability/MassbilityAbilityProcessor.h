// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"

#include "MassbilityAbilityProcessor.generated.h"

UCLASS()
class MASSBILITY_API UMassbilityAbilityProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassbilityAbilityProcessor();
	virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery AbilityQuery;
	uint32 LastAutomaticTick = MAX_uint32;
};
