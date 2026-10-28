#include "AbilitySystem/GA/GA_SwitchCharacter.h"
#include "AbilitySystemGlobals.h"
#include "Hello/HelloCharacter.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"

// 初始化函数
UGA_SwitchCharacter::UGA_SwitchCharacter()
{
	SwitchingTag = FGameplayTag::RequestGameplayTag("State.Switching");
	AbilityTags.AddTag(FGameplayTag::RequestGameplayTag("Ability.Switch"));
}

// 激活函数
void UGA_SwitchCharacter::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ListenForSwitchEvent();
}

bool UGA_SwitchCharacter::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

// 激活监听
void UGA_SwitchCharacter::ListenForSwitchEvent()
{
	UAbilityTask_WaitGameplayEvent* WaitTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		FGameplayTag::RequestGameplayTag("Event.Switch.Request"),
		nullptr,
		true,
		false
	);
	WaitTask->EventReceived.AddDynamic(this, &UGA_SwitchCharacter::OnSwitchRequestReceived);
	WaitTask->ReadyForActivation();
}

// 换人事件接收函数
void UGA_SwitchCharacter::OnSwitchRequestReceived(FGameplayEventData EventData)
{
	// 从 EventData 中直接获取目标角色
	AHelloCharacter* RawTarget = Cast<AHelloCharacter>(const_cast<UObject*>(EventData.OptionalObject.Get()));
	TargetChar = TWeakObjectPtr<AHelloCharacter>(RawTarget);
	if (!TargetChar.IsValid())
	{
		ListenForSwitchEvent();
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();

	// 出场特效 (还未实现)
	if (ASC && OutCueTag.IsValid())
	{
		ASC->ExecuteGameplayCue(OutCueTag);
	}

	// 延迟后放入场特效
	UAbilityTask_WaitDelay* DelayTask = UAbilityTask_WaitDelay::WaitDelay(this, SwitchDelay);
	DelayTask->OnFinish.AddDynamic(this, &UGA_SwitchCharacter::DoSwapEffects);
	DelayTask->ReadyForActivation();
}

// 播放登场特效
void UGA_SwitchCharacter::DoSwapEffects()
{
	if (!TargetChar.IsValid()) return;

	// 入场特效在目标角色的 ASC 上触发
	if (UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetChar.Get()))
	{
		if (InCueTag.IsValid())
		{
			TargetASC->ExecuteGameplayCue(InCueTag);
		}

		if (InvincibilityGE)
		{
			FGameplayEffectContextHandle Context = TargetASC->MakeEffectContext();
			TargetASC->ApplyGameplayEffectToSelf(InvincibilityGE->GetDefaultObject<UGameplayEffect>(), 1.0f, Context);
		}

		if (SwitchingTag.IsValid())
		{
			TargetASC->RemoveLooseGameplayTag(SwitchingTag);
		}
	}

	CleanupSwitch();
}

void UGA_SwitchCharacter::CleanupSwitch()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(SwitchingTag);
	}

	TargetChar = nullptr;

	ListenForSwitchEvent();
}
