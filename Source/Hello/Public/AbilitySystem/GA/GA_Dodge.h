// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GA/HelloGA.h"
#include "GA_Dodge.generated.h"

class UAnimSequence;
class UGameplayEffect;
class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_WaitGameplayEvent;
class AHelloCharacterBase;
struct FGameplayEventData;

/**
 * 闪避能力（高优先级，可打断普攻；无敌帧；完美闪避减速攻击者）
 *
 * - 激活瞬间取消普攻（CancelAbilitiesWithTag = Ability.Combo.NormalAttack）
 * - GA 存续期间自动持有 Status.Dodge.Active（ActivationOwnedTags），
 *   GA_NormalAttack 的 ActivationBlockedTags 据此阻止闪避中起手普攻
 * - 无敌帧：应用 InvincibleGE（运行时用 SetDuration 覆盖时长），Tag = Status.Dodge.Invincible
 * - 完美闪避：无敌帧内被命中时攻击者侧发 Event.Dodge.PerfectHit，本 GA 监听后对攻击者 SlowTime
 * - 独立冷却：走引擎原生机制——冷却 GE 配在 GA 蓝图 Class Defaults 的 Cooldowns 分类，
 *   CommitAbility 自动检测（冷却中激活失败）+ 自动应用；冷却时长直接配在 GE 资产上
 */
UCLASS()
class HELLO_API UGA_Dodge : public UHelloGA
{
	GENERATED_BODY()

public:
	UGA_Dodge();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	// ---- 闪避本体 ----

	/** 无敌帧时长（秒）——应用 InvincibleGE 的持续时间 */
	UPROPERTY(EditDefaultsOnly, Category = "Dodge", meta = (ClampMin = 0.0f))
	float InvincibleDuration = 0.3f;

	/** 无敌 GE（Duration GE，GrantedTags 需含 Status.Dodge.Invincible） */
	UPROPERTY(EditDefaultsOnly, Category = "Dodge|Effects")
	TSubclassOf<UGameplayEffect> InvincibleGE;

	// ---- 完美闪避（敌人武器命中无敌帧中的角色时触发）----

	/** 减速倍率（0.3 = 攻击者以 30% 速度运行，动画/移动整体变慢；1.0 = 不减速） */
	UPROPERTY(EditDefaultsOnly, Category = "Dodge|Perfect", meta = (ClampMin = 0.01f, ClampMax = 1.0f))
	float PerfectSlowScale = 0.3f;

	/** 减速持续时长（秒），到期恢复 1.0 */
	UPROPERTY(EditDefaultsOnly, Category = "Dodge|Perfect", meta = (ClampMin = 0.0f))
	float PerfectSlowDuration = 1.5f;

private:
	/** 闪避动画任务 */
	UPROPERTY()
	UAbilityTask_PlayMontageAndWait* DodgeMontageTask = nullptr;

	/** 完美闪避事件监听（Event.Dodge.PerfectHit → 减速攻击者） */
	UPROPERTY()
	UAbilityTask_WaitGameplayEvent* PerfectHitTask = nullptr;

	/** 当前是否处于闪避动画的后摇区间（Event.Dodge.BackswingStart 驱动；移动释放判定用） */
	bool bInDodgeBackswing = false;

	/** 释放/收尾期间抑制 Montage 回调（EndTask→StopMontage 可能同步触发回调，防重入） */
	bool bSuppressMontageCallbacks = false;

	/** 闪避后摇事件监听（Event.Dodge.BackswingStart，闪避动画上的 Combo Backswing 通知发出） */
	UPROPERTY()
	UAbilityTask_WaitGameplayEvent* WaitBackswingTask = nullptr;

	/** 移动事件监听（Event.Movement.Started，PlayerController::Move 每帧发、标志位幂等消化） */
	UPROPERTY()
	UAbilityTask_WaitGameplayEvent* WaitMovementTask = nullptr;

	/** 读取输入方向与战斗状态，旋转角色并播放对应闪避动画 */
	void PlayDodgeMontageByDirection();

	/** 根据移动输入与摄像机 Yaw 选择闪避动画并直接旋转角色（返回值：动画，出参是否后闪） */
	UAnimSequence* SelectDodgeAnimationAndRotation(AHelloCharacterBase* BaseChar, const FVector2D& Input, float CameraYaw, bool& bOutBackward) const;

	/** 闪避动画自然播完（含 BlendOut）→ 正常结束能力 */
	UFUNCTION()
	void OnDodgeMontageFinished();

	/** 闪避动画被中断（受击/更高优先级能力等）→ 以取消状态结束能力 */
	UFUNCTION()
	void OnDodgeMontageInterrupted();

	/** 收到闪避后摇开始事件（Event.Dodge.BackswingStart）→ 置位后摇标志（此后推杆可释放） */
	UFUNCTION()
	void OnDodgeBackswingStarted(FGameplayEventData EventData);

	/** 后摇区间内推杆 → 提前结束闪避动画、交还移动控制（可立即移动/起手普攻；无敌 GE 时间制不受影响） */
	UFUNCTION()
	void OnDodgeMovementStarted(FGameplayEventData EventData);

	/** 收到完美闪避事件 → 对攻击者（EventData.Instigator）施加时间减速 */
	UFUNCTION()
	void OnPerfectDodge(FGameplayEventData EventData);
};
