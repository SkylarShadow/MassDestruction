// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"

#include "MassTargetingProcessor.generated.h"

UCLASS()
class MASSBILITY_API UMassSpatialBuildProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassSpatialBuildProcessor();
	virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	// 命中快照每个模拟 Tick 重建；目标选择仍由 TargetFragment 错峰控制。
	FMassEntityQuery SourceQuery;
};

UCLASS()
class MASSBILITY_API UMassTargetingProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassTargetingProcessor();
	virtual bool ShouldAllowQueryBasedPruning(bool bRuntimeMode = true) const override;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	// 只写 Target Fragment；能力、战斗和 GAS 处理器消费这里保存的目标快照。
	FMassEntityQuery TargetingQuery;
};
