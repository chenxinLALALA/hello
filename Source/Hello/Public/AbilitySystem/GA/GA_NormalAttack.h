// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/GA/HelloGA.h"
#include "GA_NormalAttack.generated.h"

class UComboConfigDataAsset;
class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_WaitGameplayEvent;
struct FComboSegmentData;

/**
 * 通用 5 段平A连段 GA。
 * 从角色的 ComboConfigDataAsset 读取各段动画/伤害/窗口时长，单 GA 管理整条连段。
 */
UCLASS()
class HELLO_API UGA_NormalAttack : public UHelloGA
{
	GENERATED_BODY()

public:
	UGA_NormalAttack();

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

private:
	/** 当前连到第几段（1~5） */
	int32 GetCurrentComboIndex() const;

	/** 播放当前段动画 */
	void PlayCurrentSegment();

	/** 开输入窗口 GE（第 5 段同时开宽容期 GE）——动画自然播完与后摇移动释放两条路径共用 */
	void OpenWindowAndGrace();

	/** 推进到下一段（段数 +1，播下一段动画） */
	void StartNextSegment();

	/** 结束连段（清 Tag、清缓冲、EndAbility） */
	void EndCombo();

	/** 从角色的 ComboConfig 获取段数据 */
	const FComboSegmentData* GetSegmentData(int32 InSegmentIndex) const;

	/** 监听 Event.Combo.AttackPressed 回调 */
	UFUNCTION()
	void OnAttackPressed(FGameplayEventData EventData);

	/** 后摇开始回调（Event.Combo.BackswingStart）：置后摇标志；有预输入 → 剪断推进（第 5 段循环回第 1 段） */
	UFUNCTION()
	void OnBackswingStarted(FGameplayEventData EventData);

	/** 移动回调（Event.Movement.Started，每帧发、幂等消化）：后摇区间内 → 释放动画、窗口提前开（边跑边接段） */
	UFUNCTION()
	void OnMovementStarted(FGameplayEventData EventData);

	/** Montage 播完回调 */
	UFUNCTION()
	void OnMontageCompleted();

	/** Montage 中断回调 */
	UFUNCTION()
	void OnMontageCancelled();

	/** 挂连段记忆 GE（运行时授予被打断段的 Count.N）。仅在 EndAbility 内、段数 Loose Tag 清理【之后】调用——挂 GE 前后顺序错会丢 Tag */
	void SaveComboMemory(int32 InterruptedIndex);

	/** 连段状态 GE（Tag = Status.Combo.Attacking，无限时长，由 GA 显式移除） */
	UPROPERTY(EditDefaultsOnly, Category = "Combo|Effects")
	TSubclassOf<UGameplayEffect> ComboAttackingGE;
	// 窗口期/重启期 GE 已随 Plan B 退役（SetDuration 运行时覆盖被资产 Def 顶替，见 Docs/Combo_Backswing_Cancel.md §11）：
	// 现为 Loose Tag + 世界定时器自管，时长唯一真值 = ComboConfig 的 WindowDuration/GraceDuration

	/** 连段记忆 GE（空壳 Duration GE，被打断时运行时授予 Status.Combo.Count.N） */
	UPROPERTY(EditDefaultsOnly, Category = "Combo|ComboMemory")
	TSubclassOf<UGameplayEffect> ComboMemoryGE;

	/** 闪避打断后可接回连段的窗口时长（秒），过期 Tag 自动回收、连段回第 1 段 */
	UPROPERTY(EditDefaultsOnly, Category = "Combo|ComboMemory", meta = (ClampMin = 0.0f))
	float ComboMemoryWindow = 2.0f;

	/** 记忆 GE 的活动句柄（接回时移除 GE 及其授予的段数 Tag） */
	FActiveGameplayEffectHandle ComboMemoryEffectHandle;
	// 窗口/宽容期 GE 句柄已随 Plan B 退役（改为 Loose Tag + 定时器，无需句柄）

	/** 当前 Montage 任务 */
	UPROPERTY()
	UAbilityTask_PlayMontageAndWait* CurrentMontageTask = nullptr;

	/** 等待按键事件任务 */
	UPROPERTY()
	UAbilityTask_WaitGameplayEvent* WaitInputTask = nullptr;

	/** 预输入标志：动画中按过攻击键，等动画播完自动连 */
	bool bHasPendingInput = false;

	/** 推进段数已安排（防止重复调度） */
	bool bAdvancePending = false;

	/** 本次结束是否已挂连段记忆（取消链与 Montage 中断回调可能双路径到达，防重复挂 GE） */
	bool bComboMemorySaved = false;

	/** 当前段是否处于后摇区间（Event.Combo.BackswingStart 驱动；PlayCurrentSegment/OnMontageCompleted/EndAbility 三处重置） */
	bool bInBackswing = false;

	/** 剪断/换段期间抑制 Montage 回调（EndTask→StopMontage 可能同步触发 OnBlendOut/OnInterrupted，防重入） */
	bool bSuppressMontageCallbacks = false;

	/** 等待后摇开始事件任务（WaitGameplayEvent 单 Tag，与 WaitInputTask 各一个任务实例） */
	UPROPERTY()
	UAbilityTask_WaitGameplayEvent* WaitBackswingTask = nullptr;

	/** 本段后摇已因移动释放（防重复释放；PlayCurrentSegment/EndAbility 重置）。释放≠推进：段数不动、GA 不结束 */
	bool bBackswingReleased = false;

	/** 等待移动开始事件任务（Event.Movement.Started，PlayerController::Move 每帧发） */
	UPROPERTY()
	UAbilityTask_WaitGameplayEvent* WaitMovementTask = nullptr;

	/** 段间混合时长（动态 Montage 的 BlendIn/Out，秒；0=硬切）。剪断推进与自然换段共用 */
	UPROPERTY(EditDefaultsOnly, Category = "Combo", meta = (ClampMin = 0.0f))
	float SegmentBlendTime = 0.2f;

	/** 安排推进段数（延迟到下一帧，避免在任务回调内重入） */
	void ScheduleAdvance();

	/** 实际执行推进段数（延迟回调入口） */
	void AdvanceComboSegment();

	/** 动画最长允许播多久,兜底配置 */
	UPROPERTY(EditDefaultsOnly, Category = "Combo", meta = (ClampMin = 0.0f))
	float MaxComboDuration = 3.0f;

	/** 最大连击时间计时器 */
	FTimerHandle MaxComboTimerHandle;

	/** 段结束计时器（窗口+重启期总时长后到点 → OnComboExpired → 连段结束；开窗时挂、新段开始/连段结束时清） */
	FTimerHandle ComboExpiryTimerHandle;

	/** 窗口期计时器（WindowDuration 后到点 → OnWindowExpired：摘窗口 Tag、挂重启 Tag；Plan B 时长真值来源） */
	FTimerHandle WindowTimerHandle;

	/** 当前段动画开始时间（墙钟 FPlatformTime::Seconds）——测试动画实际时长/减速效果用 */
	double SegmentStartTime = 0.0;

	/** 最大连击时间超时回调 */
	void OnMaxComboTimeout();

	/** 段结束回调（窗口+重启期都过完没人接段 → 连段正式结束，不存记忆） */
	void OnComboExpired();

	/** 窗口期到期回调（摘窗口 Loose Tag → 挂重启期 Loose Tag + 检查点日志；原 OnRestartPeriodStarted 升级） */
	void OnWindowExpired();
};