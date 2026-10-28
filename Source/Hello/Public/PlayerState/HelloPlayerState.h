// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/PlayerState.h"
#include "HelloPlayerState.generated.h"

class UHelloASC;

UCLASS()
class HELLO_API AHelloPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AHelloPlayerState();

	/* AbilitySystemInterface */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	/** 标记换人 GA 是否已激活，防止重复激活 */
	bool bSwitchAbilityActive = false;

	/** 是否处于战斗状态（锁定目标/进入战斗节奏），影响无方向输入时的闪避方向 */
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	bool bInCombat = false;

protected:
	virtual void BeginPlay() override;

	UPROPERTY()
	TObjectPtr<UHelloASC> AbilitySystemComponent;

	/* 小队级起始能力（例如换人 GA，在蓝图中配置） */
	UPROPERTY(EditDefaultsOnly, Category = "Abilities")
	TArray<TSubclassOf<UGameplayAbility>> StartUpAbilities;

	/** 授予 StartUpAbilities 到 ASC */
	void InitialStartUpAbilities();
};
