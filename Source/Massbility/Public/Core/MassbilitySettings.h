// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"

#include "MassbilitySettings.generated.h"

class UMassbilityRuntimeTagCatalog;
class UMassbilityStateTagMapping;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Massbility"))
class MASSBILITY_API UMassbilitySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override;

	UPROPERTY(Config, EditAnywhere, Category = "Massbility|Tags")
	TSoftObjectPtr<UMassbilityRuntimeTagCatalog> RuntimeTagCatalog; //把GameplayTag 编译成运行时使用的紧凑 Tag Mask/ID

	UPROPERTY(Config, EditAnywhere, Category = "Massbility|Tags")
	TSoftObjectPtr<UMassbilityStateTagMapping> StateTagMapping; //把项目 GameplayTag 映射到内部状态，例如死亡、不可被选中、能力阻断之类的

	/** Author tags that identify the three direct fields in FMassbilityVitalFragment. */
    //把项目自己的 GameplayTag 映射到三个固定字段
	UPROPERTY(Config, EditAnywhere, Category = "Massbility|Attributes")
	FGameplayTag HealthAttributeTag;

	UPROPERTY(Config, EditAnywhere, Category = "Massbility|Attributes")
	FGameplayTag MaxHealthAttributeTag;

	UPROPERTY(Config, EditAnywhere, Category = "Massbility|Attributes")
	FGameplayTag MoveSpeedAttributeTag;

	UPROPERTY(Config, EditAnywhere, Category = "Massbility|Simulation", meta = (ClampMin = "1", ClampMax = "120"))
	int32 SimulationTicksPerSecond = 20; //每秒推进多少个固定 simulation tick

	UPROPERTY(Config, EditAnywhere, Category = "Massbility|Targeting", meta = (ClampMin = "1", ClampMax = "60"))
	int32 TargetingTicksPerSecond = 10; //目标刷新频率，每秒更新目标选择的次数

	UPROPERTY(Config, EditAnywhere, Category = "Massbility|Targeting", meta = (ClampMin = "50.0", Units = "cm"))
	float SpatialCellSize = 400.0f; //2D Spatial Hash 的网格边长

	/** Hard cap for queued generic outputs. Requests are never silently dropped. */
	UPROPERTY(Config, EditAnywhere, Category = "Massbility|Output", meta = (ClampMin = "1"))
	int32 MaxPendingOutputRequests = 4096;  //Output 队列上限。能力或外部 Mass 系统提交的伤害/效果输出会先进入该队列，再由 Dispatcher 路由到 Mass Combat/Effect

	/** Hard cap for forced movement requests waiting for the next simulation tick. */
	UPROPERTY(Config, EditAnywhere, Category = "Massbility|Movement", meta = (ClampMin = "1"))
	int32 MaxPendingForcedMovementRequests = 2048;  //等待下一个模拟 tick 消费的强制位移请求上限。用于击退、拉拽、位移到目标点等通用位移请求。满了时新请求应被明确拒绝
};
