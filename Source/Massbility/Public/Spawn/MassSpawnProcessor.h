// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "MassProcessor.h"

#include "MassSpawnProcessor.generated.h"

/** 在 Simulation Tick 末尾消费通用 Spawn 队列；新实体从下一 Tick 起参与空间和能力处理。 */
UCLASS()
class MASSBILITY_API UMassSpawnProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassSpawnProcessor();
	virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	uint32 LastProcessedTick = MAX_uint32;
};
