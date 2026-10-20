// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;

/** Immutable-per-world quantized clock shared by effects, abilities and targeting. */
struct MASSBILITY_API FMassbilitySimulationClock
{//按世界时间计算的离散模拟时钟，供效果、能力和目标系统使用同一套 Tick
	void Initialize(const UWorld* InWorld, int32 InTicksPerSecond);
	void Reset();
	uint32 GetCurrentTick() const;
	uint32 SecondsToTicksCeil(float Seconds) const;
	uint32 GetTicksPerSecond() const { return TicksPerSecond; }
	bool IsInitialized() const { return World != nullptr; }

	static bool HasReached(uint32 CurrentTick, uint32 TargetTick)
	{
		return static_cast<int32>(CurrentTick - TargetTick) >= 0;
	}

private:
	const UWorld* World = nullptr;
	uint32 TicksPerSecond = 20;
};

