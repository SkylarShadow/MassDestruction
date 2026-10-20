// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "MassProcessor.h"

#include "MassbilityOutputProcessor.generated.h"

UCLASS()
class MASSBILITY_API UMassbilityOutputProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassbilityOutputProcessor();
	virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

