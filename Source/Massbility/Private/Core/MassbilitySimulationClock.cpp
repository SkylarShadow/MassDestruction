// Copyright MassDestruction. All Rights Reserved.

#include "Core/MassbilitySimulationClock.h"

#include "Engine/World.h"

void FMassbilitySimulationClock::Initialize(const UWorld* InWorld, const int32 InTicksPerSecond)
{
	World = InWorld;
	TicksPerSecond = static_cast<uint32>(FMath::Clamp(InTicksPerSecond, 1, 120));
}

void FMassbilitySimulationClock::Reset()
{
	World = nullptr;
	TicksPerSecond = 20;
}

uint32 FMassbilitySimulationClock::GetCurrentTick() const
{
	return World
		? static_cast<uint32>(FMath::FloorToDouble(World->GetTimeSeconds() * static_cast<double>(TicksPerSecond)))
		: 0;
}

uint32 FMassbilitySimulationClock::SecondsToTicksCeil(const float Seconds) const
{
	return Seconds > 0.0f
		? FMath::Max(1u, static_cast<uint32>(FMath::CeilToInt(Seconds * static_cast<float>(TicksPerSecond))))
		: 0u;
}

