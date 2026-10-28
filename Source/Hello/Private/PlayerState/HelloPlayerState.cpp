// Fill out your copyright notice in the Description page of Project Settings.

#include "PlayerState/HelloPlayerState.h"
#include "AbilitySystem/ASC/HelloASC.h"
#include "AbilitySystemComponent.h"

//初始化ASC
AHelloPlayerState::AHelloPlayerState()
{
	SetNetUpdateFrequency(100.f);

	// 创建小队级 ASC
	AbilitySystemComponent = CreateDefaultSubobject<UHelloASC>("HelloAbilitySystemComponent");
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

UAbilitySystemComponent* AHelloPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AHelloPlayerState::BeginPlay()
{
	Super::BeginPlay();

	// 服务端授予并激活小队级 GA（如换人能力）
	if (HasAuthority())
	{
		InitialStartUpAbilities();
	}
}

// 初始化全队共享的技能
void AHelloPlayerState::InitialStartUpAbilities()
{
	if (!AbilitySystemComponent) return;

	for (const TSubclassOf<UGameplayAbility>& AbilityClass : StartUpAbilities)
	{
		if (!AbilityClass) continue;

		// 避免重复授予
		if (AbilitySystemComponent->FindAbilitySpecFromClass(AbilityClass))
			continue;

		//技能等级在之后可能会考虑转变为可以设置，不是硬编码
		FGameplayAbilitySpec Spec(AbilityClass, 1, INDEX_NONE, this);
		AbilitySystemComponent->GiveAbility(Spec);
	}

	// 换人能力激活
	// 授予完成后，如果有换人 GA 则激活
	// 注意：GA 应该具有 Ability.Switch 标签，我们通过 Tag 激活
	if (!bSwitchAbilityActive)
	{
		FGameplayTagContainer SwitchTagContainer;
		SwitchTagContainer.AddTag(FGameplayTag::RequestGameplayTag("Ability.Switch"));
		bool bActivated = AbilitySystemComponent->TryActivateAbilitiesByTag(SwitchTagContainer);
		if (bActivated)
		{
			bSwitchAbilityActive = true;
		}
	}
}
