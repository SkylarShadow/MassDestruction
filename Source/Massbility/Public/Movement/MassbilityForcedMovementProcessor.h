// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"

#include "MassbilityForcedMovementProcessor.generated.h"

UCLASS()
class MASSBILITY_API UMassbilityForcedMovementProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassbilityForcedMovementProcessor();
	virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery MovementQuery;
	uint32 LastProcessedTick = MAX_uint32;
};

