// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "MassProcessor.h"

#include "MassbilityGASBridgeProcessor.generated.h"

UCLASS()
class MASSBILITYGAS_API UMassbilityGASProxySyncProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassbilityGASProxySyncProcessor();
	virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

UCLASS()
class MASSBILITYGAS_API UMassbilityGASBridgeProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassbilityGASBridgeProcessor();
	virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};

