// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_SwitchCharacter.generated.h"

/**
 * 换人特效 GA — 挂载在 PlayerState ASC 上，收到事件后播放出入场特效
 */
UCLASS()
class HELLO_API UGA_SwitchCharacter : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_SwitchCharacter();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	UPROPERTY(EditDefaultsOnly, Category = "Switch|Effects")
	FGameplayTag OutCueTag;

	UPROPERTY(EditDefaultsOnly, Category = "Switch|Effects")
	FGameplayTag InCueTag;

	//换人时的无敌效果
	UPROPERTY(EditDefaultsOnly, Category = "Switch|Effects")
	TSubclassOf<UGameplayEffect> InvincibilityGE;

	UPROPERTY(EditDefaultsOnly, Category = "Switch")
	float SwitchDelay = 0.15f;

protected:
	void ListenForSwitchEvent();

	UFUNCTION()
	void OnSwitchRequestReceived(FGameplayEventData EventData);

	UFUNCTION()
	void DoSwapEffects();

	UFUNCTION()
	void CleanupSwitch();

private:
	FGameplayTag SwitchingTag;

	/** 从 EventData.OptionalObject 中提取的目标角色 */
	TWeakObjectPtr<ACharacter> TargetChar;
};
