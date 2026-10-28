// Copyright Hello Project. All Rights Reserved.

#include "Animation/AnimNotifyState_ComboBackswing.h"
#include "AbilitySystemComponent.h"
#include "Character/HelloCharacterBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameplayTags/HelloGameplayTags.h"

UAnimNotifyState_ComboBackswing::UAnimNotifyState_ComboBackswing()
{
	// 默认 = 攻击后摇事件：已摆在 5 段攻击动画上的通知资产不存该属性，加载时吃到此默认值，零改动
	BackswingEventTag = HelloGameplayTags::Event_Combo_BackswingStart;
}

void UAnimNotifyState_ComboBackswing::NotifyBegin(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	// 与 AnimNotify_EnableWeaponTrace 同款取角色方式
	AHelloCharacterBase* Character = Cast<AHelloCharacterBase>(MeshComp->GetOwner());
	if (!Character)
	{
		return;
	}

	UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	if (!ASC)
	{
		return;
	}

	// 与 PlayerController 发 Event.Combo.AttackPressed 同款链路；Tag 由动画资产上的通知实例决定
	FGameplayEventData Payload;
	Payload.EventTag = BackswingEventTag;
	Payload.Instigator = Character;
	ASC->HandleGameplayEvent(BackswingEventTag, &Payload);

	// 验证日志：确认"通知触发 → 找到角色 ASC → 事件已发出"链路（含实际发出的 Tag 名）
	UE_LOG(LogTemp, Warning,
		TEXT("[ComboBackswing] %s 的动画 [%s] 进入后摇区间（后摇时长=%.2f 秒）→ %s 已发出"),
		*Character->GetName(), *Animation->GetName(), TotalDuration, *BackswingEventTag.ToString());
}
