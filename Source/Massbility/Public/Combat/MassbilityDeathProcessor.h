// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"

#include "MassbilityDeathProcessor.generated.h"

UCLASS()
class MASSBILITY_API UMassbilityDeathProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassbilityDeathProcessor();
	virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery WriteDependencyQuery;
	FMassEntityQuery DyingQuery;
};

