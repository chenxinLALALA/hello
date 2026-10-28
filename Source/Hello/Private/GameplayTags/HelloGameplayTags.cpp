// Copyright Hello Project. All Rights Reserved.

#include "Hello/Public/GameplayTags/HelloGameplayTags.h"

namespace HelloGameplayTags
{
	// ---- 能力标签 ----
	UE_DEFINE_GAMEPLAY_TAG(Ability_Combo_NormalAttack, "Ability.Combo.NormalAttack");

	// ---- 连段状态标签 ----
	UE_DEFINE_GAMEPLAY_TAG(Status_Combo_Attacking, "Status.Combo.Attacking");
	UE_DEFINE_GAMEPLAY_TAG(Status_Combo_Window, "Status.Combo.Window");
	UE_DEFINE_GAMEPLAY_TAG(Status_Combo_ResetPending, "Status.Combo.ResetPending");

	// ---- 段数标签 ----
	UE_DEFINE_GAMEPLAY_TAG(Status_Combo_Count_1, "Status.Combo.Count.1");
	UE_DEFINE_GAMEPLAY_TAG(Status_Combo_Count_2, "Status.Combo.Count.2");
	UE_DEFINE_GAMEPLAY_TAG(Status_Combo_Count_3, "Status.Combo.Count.3");
	UE_DEFINE_GAMEPLAY_TAG(Status_Combo_Count_4, "Status.Combo.Count.4");
	UE_DEFINE_GAMEPLAY_TAG(Status_Combo_Count_5, "Status.Combo.Count.5");

	// ---- 输入缓冲标签 ----
	UE_DEFINE_GAMEPLAY_TAG(Input_Buffer_Combo, "Input.Buffer.Combo");

	// ---- 游戏事件标签 ----
	UE_DEFINE_GAMEPLAY_TAG(Event_Combo_AttackPressed, "Event.Combo.AttackPressed");// 最终连段动画结束后不会立刻结束连段
	UE_DEFINE_GAMEPLAY_TAG(Event_Combo_ChainCombo, "Event.Combo.ChainCombo");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combo_ResetCombo, "Event.Combo.ResetCombo");
	UE_DEFINE_GAMEPLAY_TAG(Event_Combo_BackswingStart, "Event.Combo.BackswingStart");// 攻击动画后摇区间开始（AnimNotifyState_ComboBackswing → GA_NormalAttack）
	UE_DEFINE_GAMEPLAY_TAG(Event_Dodge_BackswingStart, "Event.Dodge.BackswingStart");// 闪避动画后摇区间开始（同款通知类、闪避动画上改选此 Tag → GA_Dodge）

	// ---- 移动事件标签 ----
	UE_DEFINE_GAMEPLAY_TAG(Event_Movement_Started, "Event.Movement.Started");// 玩家推杆移动中（PlayerController::Move 每帧发；GA 收到后幂等处理）

	// ---- 数据标签 ----
	UE_DEFINE_GAMEPLAY_TAG(Data_Damage, "Data.Damage");

	// ---- 闪避标签 ----
	UE_DEFINE_GAMEPLAY_TAG(Ability_Dodge_Dodge, "Ability.Dodge.Dodge");
	UE_DEFINE_GAMEPLAY_TAG(Status_Dodge_Active, "Status.Dodge.Active");
	UE_DEFINE_GAMEPLAY_TAG(Status_Dodge_Invincible, "Status.Dodge.Invincible");
	UE_DEFINE_GAMEPLAY_TAG(Event_Dodge_PerfectHit, "Event.Dodge.PerfectHit");
	UE_DEFINE_GAMEPLAY_TAG(Cooldown_Dodge, "Cooldown.Dodge");

	// ---- 相机标签 ----
	UE_DEFINE_GAMEPLAY_TAG(Camera_Mode_ThirdPerson, "Camera.Mode.ThirdPerson");

	FGameplayTag GetComboCountTag(int32 InComboIndex)
	{
		switch (InComboIndex)
		{
		case 1: return Status_Combo_Count_1.GetTag();
		case 2: return Status_Combo_Count_2.GetTag();
		case 3: return Status_Combo_Count_3.GetTag();
		case 4: return Status_Combo_Count_4.GetTag();
		case 5: return Status_Combo_Count_5.GetTag();
		default: return FGameplayTag();
		}
	}

	int32 GetComboIndexFromTag(const FGameplayTag& InTag)
	{
		if (InTag == Status_Combo_Count_1.GetTag()) return 1;
		if (InTag == Status_Combo_Count_2.GetTag()) return 2;
		if (InTag == Status_Combo_Count_3.GetTag()) return 3;
		if (InTag == Status_Combo_Count_4.GetTag()) return 4;
		if (InTag == Status_Combo_Count_5.GetTag()) return 5;
		return 0;
	}
}

FGameplayTag UHelloGameplayTags::GetComboCountTag(int32 InComboIndex)
{
	return HelloGameplayTags::GetComboCountTag(InComboIndex);
}

int32 UHelloGameplayTags::GetComboIndexFromTag(const FGameplayTag& InTag)
{
	return HelloGameplayTags::GetComboIndexFromTag(InTag);
}
