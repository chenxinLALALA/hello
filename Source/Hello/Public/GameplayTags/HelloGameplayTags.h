// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HelloGameplayTags.generated.h"

/**
 * Native Gameplay Tags（C++ 原生定义，无需在编辑器手动创建）。
 *
 * 用法：
 *   - 在 C++ 中直接引用：`HelloGameplayTags::Ability_Combo_Attack`
 *     （FNativeGameplayTag 可隐式转换为 FGameplayTag）
 *   - 在蓝图中：调用 UHelloGameplayTags 库函数，或把 tag 作为 FGameplayTag 属性选择
 *
 * 相关说明见 Docs/GAS_Combo_Design.md
 */
namespace HelloGameplayTags
{
	// ---- 能力标签 ----
	extern HELLO_API FNativeGameplayTag Ability_Combo_NormalAttack;		// Ability.Combo.NormalAttack

	// ---- 连段状态标签（挂在 ASC 上）----
	extern HELLO_API FNativeGameplayTag Status_Combo_Attacking;		// Status.Combo.Attacking
	extern HELLO_API FNativeGameplayTag Status_Combo_Window;		// Status.Combo.Window
	extern HELLO_API FNativeGameplayTag Status_Combo_ResetPending;	// Status.Combo.ResetPending

	// ---- 段数标签（当前连到第几段）----
	extern HELLO_API FNativeGameplayTag Status_Combo_Count_1;		// Status.Combo.Count.1
	extern HELLO_API FNativeGameplayTag Status_Combo_Count_2;		// Status.Combo.Count.2
	extern HELLO_API FNativeGameplayTag Status_Combo_Count_3;		// Status.Combo.Count.3
	extern HELLO_API FNativeGameplayTag Status_Combo_Count_4;		// Status.Combo.Count.4
	extern HELLO_API FNativeGameplayTag Status_Combo_Count_5;		// Status.Combo.Count.5

	// ---- 输入缓冲标签（挂在 ASC 上，表示"缓存了一次攻击输入"）----
	extern HELLO_API FNativeGameplayTag Input_Buffer_Combo;			// Input.Buffer.Combo

	// ---- 游戏事件标签（SendGameplayEventToActor 使用）----
	extern HELLO_API FNativeGameplayTag Event_Combo_AttackPressed;	// Event.Combo.AttackPressed
	extern HELLO_API FNativeGameplayTag Event_Combo_ChainCombo;		// Event.Combo.ChainCombo
	extern HELLO_API FNativeGameplayTag Event_Combo_ResetCombo;		// Event.Combo.ResetCombo
	extern HELLO_API FNativeGameplayTag Event_Combo_BackswingStart;	// Event.Combo.BackswingStart（攻击动画后摇区间开始，AnimNotifyState_ComboBackswing 发出）
	extern HELLO_API FNativeGameplayTag Event_Dodge_BackswingStart;	// Event.Dodge.BackswingStart（闪避动画后摇区间开始，同一通知类、动画上改选此 Tag）

	// ---- 移动事件标签 ----
	extern HELLO_API FNativeGameplayTag Event_Movement_Started;	// Event.Movement.Started（玩家推杆移动中，PlayerController::Move 每帧发，GA 幂等消化）

	// ---- 数据标签（SetByCaller 传值用）----
	extern HELLO_API FNativeGameplayTag Data_Damage;					// Data.Damage

	// ---- 闪避标签 ----
	extern HELLO_API FNativeGameplayTag Ability_Dodge_Dodge;			// Ability.Dodge.Dodge（GA 激活入口）
	extern HELLO_API FNativeGameplayTag Status_Dodge_Active;			// Status.Dodge.Active（闪避进行中，GA 自动持有）
	extern HELLO_API FNativeGameplayTag Status_Dodge_Invincible;		// Status.Dodge.Invincible（无敌帧，GE 授予）
	extern HELLO_API FNativeGameplayTag Event_Dodge_PerfectHit;		// Event.Dodge.PerfectHit（完美闪避事件）
	extern HELLO_API FNativeGameplayTag Cooldown_Dodge;				// Cooldown.Dodge（闪避 CD GE 的 GrantedTags，引擎 CommitCheck 自动检测）

	// ---- 相机标签（默认态 Tag 驱动）----
	extern HELLO_API FNativeGameplayTag Camera_Mode_ThirdPerson;	// Camera.Mode.ThirdPerson

	/** 段数索引（1~5）→ 对应的段数 Tag；越界返回空 Tag */
	HELLO_API FGameplayTag GetComboCountTag(int32 InComboIndex);

	/** 段数 Tag → 段数索引（1~5）；不是段数 Tag 时返回 0 */
	HELLO_API int32 GetComboIndexFromTag(const FGameplayTag& InTag);
}

/**
 * 蓝图可调用的连段 Tag 辅助库。
 */
UCLASS()
class HELLO_API UHelloGameplayTags : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 段数索引（1/2/3）→ 段数 Tag（如 Status.Combo.Count.2） */
	UFUNCTION(BlueprintPure, Category = "Hello|GameplayTags")
	static FGameplayTag GetComboCountTag(int32 InComboIndex);

	/** 段数 Tag → 段数索引；不是段数 Tag 返回 0 */
	UFUNCTION(BlueprintPure, Category = "Hello|GameplayTags")
	static int32 GetComboIndexFromTag(const FGameplayTag& InTag);
};
