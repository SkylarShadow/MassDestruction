// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMassbility, Log, All);

/**
 * Runtime foundation for Mass-native abilities and effects.
 *
 * Massbility uses Mass fragments and processors for large crowds. It does not
 * create a Gameplay Ability System component for every Mass entity.
 */
class MASSBILITY_API FMassbilityModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
