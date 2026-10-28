// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "AnimNotifyState_ComboBackswing.generated.h"

class USkeletalMeshComponent;
class UAnimSequenceBase;

/**
 * 动画区间通知：标记攻击/闪避动画的"后摇区间"（两用类）。
 *
 * NotifyBegin 时向角色 ASC 发送 BackswingEventTag 指定的后摇开始事件，
 * 攻击动画默认 Event.Combo.BackswingStart（GA_NormalAttack 消费：剪断推进/移动释放/闪避记忆）；
 * 闪避动画在编辑器通知详情里改为 Event.Dodge.BackswingStart（GA_Dodge 消费：移动释放）。
 *
 * 摆放建议（见 Docs/Combo_Backswing_Cancel.md 第 4.3 节；GA 侧有防御性收武器兜底）：
 * - 建议位于 DisableWeaponTrace 之后（剪断会放弃之后的剩余伤害窗口）；
 * - 区间起点 = 该段最早允许取消的时刻，区间拉到动画结尾；
 * - 漏摆的动画自动退化为现状行为（整段播完才推进、闪避记 N），不崩溃。
 */
UCLASS(Blueprintable, meta = (DisplayName = "Combo Backswing"))
class HELLO_API UAnimNotifyState_ComboBackswing : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UAnimNotifyState_ComboBackswing();

	/**
	 * 本通知发出的后摇事件 Tag。
	 * 攻击动画保持默认（Event.Combo.BackswingStart，已摆的 5 段资产零改动）；
	 * 闪避动画在编辑器里改选 Event.Dodge.BackswingStart。
	 */
	UPROPERTY(EditAnywhere, Category = "Backswing")
	FGameplayTag BackswingEventTag;

	/**
	 * 后摇区间进入时刻：向角色 ASC 发送 BackswingEventTag。
	 * 与 AnimNotify_EnableWeaponTrace 同款取角色方式、与 PlayerController 发
	 * Event.Combo.AttackPressed 同款事件链路（ASC->HandleGameplayEvent）。
	 */
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override { return TEXT("ComboBackswing"); }
};
