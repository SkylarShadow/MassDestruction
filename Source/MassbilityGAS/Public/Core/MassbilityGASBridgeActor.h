// Copyright MassDestruction. All Rights Reserved.

#pragma once

#include "AbilitySystemInterface.h"
#include "GameFramework/Actor.h"

#include "MassbilityGASBridgeActor.generated.h"

class UAbilitySystemComponent;

/** 每个权威世界一个临时来源 ASC；不会为每个 Mass 实体创建 ASC。 */
UCLASS(Transient, NotBlueprintable, NotPlaceable)
class MASSBILITYGAS_API AMassbilityGASBridgeActor : public AActor, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AMassbilityGASBridgeActor();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystemComponent; }

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Massbility|GAS")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;
};

