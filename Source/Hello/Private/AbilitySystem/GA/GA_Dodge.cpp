// Copyright Hello Project. All Rights Reserved.

#include "AbilitySystem/GA/GA_Dodge.h"
#include "GameplayTags/HelloGameplayTags.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimInstance.h"
#include "Character/HelloCharacterBase.h"
#include "PlayerController/HelloPlayerController.h"
#include "PlayerState/HelloPlayerState.h"

UGA_Dodge::UGA_Dodge()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// 高优先级：闪避激活瞬间取消普攻（普攻走既有 Montage 中断 → EndAbility(bWasCancelled) 清理路径）
	CancelAbilitiesWithTag.AddTag(HelloGameplayTags::Ability_Combo_NormalAttack);

	// GA 存续期间自动持有 Status.Dodge.Active（激活挂上、结束自动移除，零清理代码）
	// 消费方：GA_NormalAttack 的 ActivationBlockedTags（闪避中禁起手普攻）等
	ActivationOwnedTags.AddTag(HelloGameplayTags::Status_Dodge_Active);
}

void UGA_Dodge::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// 提交能力（当前无 Cost/Cool GE 时等价于直接通过；失败则取消结束）
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[GA_Dodge] 闪避激活，无敌帧 %.2f 秒"), InvincibleDuration);

	// 冷却不在 C++ 里手动处理：CommitAbility 内部自动检测 + 应用 GA 类默认配置的冷却 GE
	// （冷却中激活时 CommitAbility 返回 false → 走上面的取消分支；激活成功则引擎自动挂 CD GE）

	// ---- 无敌帧 GE ----
	// GE 资产侧只需是 Duration 类 GE 并授予 Status.Dodge.Invincible，时长在这里运行时覆盖
	if (InvincibleGE)
	{
		FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(InvincibleGE, 1.0f);
		Spec.Data->SetDuration(InvincibleDuration, false);
		ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, Spec);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GA_Dodge] InvincibleGE 未配置，本次闪避没有无敌帧（需在编辑器创建 Duration GE 并授予 Status.Dodge.Invincible 后填入）"));
	}

	// ---- 播放闪避 Montage ----
	// 根据输入方向与战斗状态选择前冲/后闪动画，并旋转角色到目标方向
	PlayDodgeMontageByDirection();

	// 与 GA_NormalAttack 一致：只绑 BlendOut（自然播完）+ Interrupted（被中断），
	// 不绑 OnCompleted/OnCancelled，避免两个委托先后触发导致 EndAbility 重复调用
	DodgeMontageTask->OnBlendOut.AddDynamic(this, &UGA_Dodge::OnDodgeMontageFinished);
	DodgeMontageTask->OnInterrupted.AddDynamic(this, &UGA_Dodge::OnDodgeMontageInterrupted);
	DodgeMontageTask->ReadyForActivation();

	// ---- 监听完美闪避事件 ----
	// 事件来自攻击者：ApplyDamageToActor 检测到目标带无敌 Tag 时发到本 ASC。
	// 事件本身只在无敌帧内产生，无需再做窗口判断；GA 结束时任务随 EndAbility 清理
	PerfectHitTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, HelloGameplayTags::Event_Dodge_PerfectHit);
	PerfectHitTask->EventReceived.AddDynamic(this, &UGA_Dodge::OnPerfectDodge);
	PerfectHitTask->ReadyForActivation();

	// ---- 后摇移动释放监听（阶段 3）----
	// 闪避后摇事件：来自闪避动画上摆的 Combo Backswing 通知（BackswingEventTag 改选 Event.Dodge.BackswingStart）
	WaitBackswingTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, HelloGameplayTags::Event_Dodge_BackswingStart);
	WaitBackswingTask->EventReceived.AddDynamic(this, &UGA_Dodge::OnDodgeBackswingStarted);
	WaitBackswingTask->ReadyForActivation();

	// 移动事件：PlayerController::Move 每帧发，GA 侧标志位幂等消化
	WaitMovementTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, HelloGameplayTags::Event_Movement_Started);
	WaitMovementTask->EventReceived.AddDynamic(this, &UGA_Dodge::OnDodgeMovementStarted);
	WaitMovementTask->ReadyForActivation();

	// 标志复位（InstancedPerActor：上一轮残留防御性清理）
	bInDodgeBackswing = false;
	bSuppressMontageCallbacks = false;
}

void UGA_Dodge::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 清理动画任务（IsActive 守卫，避免对已自然结束的任务重复 EndTask）
	if (DodgeMontageTask)
	{
		if (DodgeMontageTask->IsActive())
		{
			DodgeMontageTask->EndTask();
		}
		DodgeMontageTask = nullptr;
	}

	// 清理事件监听任务
	if (PerfectHitTask)
	{
		if (PerfectHitTask->IsActive())
		{
			PerfectHitTask->EndTask();
		}
		PerfectHitTask = nullptr;
	}

	// 清理后摇/移动监听任务
	if (WaitBackswingTask)
	{
		if (WaitBackswingTask->IsActive())
		{
			WaitBackswingTask->EndTask();
		}
		WaitBackswingTask = nullptr;
	}
	if (WaitMovementTask)
	{
		if (WaitMovementTask->IsActive())
		{
			WaitMovementTask->EndTask();
		}
		WaitMovementTask = nullptr;
	}

	// 重置状态标志（防跨激活残留）
	bInDodgeBackswing = false;
	bSuppressMontageCallbacks = false;

	// 闪避结束后清空 Controller 的 LastMoveInput，防止后摇释放/惯性滑行残留的输入
	// 污染下一次闪避的方向判断（表现为第二次闪避变成前冲）
	if (const AActor* Avatar = GetAvatarActorFromActorInfo())
	{
		if (const AController* Ctrl = Cast<AController>(Avatar->GetOwner()))
		{
			if (const AHelloPlayerController* PC = Cast<AHelloPlayerController>(Ctrl))
			{
				const_cast<AHelloPlayerController*>(PC)->ClearLastMoveInput();
			}
		}
	}

	// Status.Dodge.Active（ActivationOwnedTags）与无敌 GE 到期均由 GAS 自动回收，无需手动清理
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Dodge::PlayDodgeMontageByDirection()
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	AHelloCharacterBase* BaseChar = Cast<AHelloCharacterBase>(Avatar);
	if (!BaseChar)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GA_Dodge] Avatar 不是 AHelloCharacterBase 子类，无法读取角色闪避动画"));
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	// 读取最近一次移动输入（X=左右，Y=前后）与摄像机 Yaw
	FVector2D MoveInput = FVector2D::ZeroVector;
	float CameraYaw = Avatar->GetActorRotation().Yaw; // 兜底：无 Controller 时退回到角色面向
	if (AController* Ctrl = Cast<AController>(Avatar->GetOwner()))
	{
		if (AHelloPlayerController* PC = Cast<AHelloPlayerController>(Ctrl))
		{
			MoveInput = PC->LastMoveInput;
			CameraYaw = PC->GetControlRotation().Yaw;
		}
	}

	bool bBackward = false;
	UAnimSequence* SelectedAnim = SelectDodgeAnimationAndRotation(BaseChar, MoveInput, CameraYaw, bBackward);

	if (!SelectedAnim)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GA_Dodge] 角色 %s 未配置闪避动画"), *BaseChar->GetName());
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[GA_Dodge] [%s] 闪避方向：%s | MoveInput=%s | CameraYaw=%.1f | 旋转=%d | bInCombat=%d"),
		*BaseChar->GetName(),
		bBackward ? TEXT("后闪") : TEXT("前冲"),
		*MoveInput.ToString(),
		CameraYaw,
		!MoveInput.IsNearlyZero(0.1f) ? 1 : 0,
		bBackward ? 1 : 0);

	// 从 AnimationSequence 创建临时 Montage
	UAnimMontage* TempMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(
		SelectedAnim, FName("DefaultSlot"), 0.0f, 0.0f, 1.0f);

	DodgeMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, TempMontage, 1.0f, NAME_None, false, 1.0f);
}

UAnimSequence* UGA_Dodge::SelectDodgeAnimationAndRotation(
	AHelloCharacterBase* BaseChar, const FVector2D& Input, float CameraYaw, bool& bOutBackward) const
{
	check(BaseChar);
	bOutBackward = false;

	if (!Input.IsNearlyZero(0.1f))
	{
		// 有方向输入：统一前冲，角色转向摄像机空间的目标方向
		// 输入约定 X=左右，Y=前后；Atan2(X, Y) 给出相对前向（Y+）的 Yaw 偏移
		const float InputYawOffset = FMath::Atan2(Input.X, Input.Y) * 180.f / PI;
		FRotator TargetRot(0.f, CameraYaw + InputYawOffset, 0.f);
		BaseChar->SetActorRotation(TargetRot);
		bOutBackward = false;
		return BaseChar->DodgeForwardMontage ? BaseChar->DodgeForwardMontage : BaseChar->DodgeMontage;
	}

	// 无方向输入：查战斗状态
	bool bInCombat = false;
	if (const AController* Ctrl = Cast<AController>(BaseChar->GetOwner()))
	{
		if (const AHelloPlayerController* PC = Cast<AHelloPlayerController>(Ctrl))
		{
			if (const AHelloPlayerState* PS = Cast<AHelloPlayerState>(PC->PlayerState))
			{
				bInCombat = PS->bInCombat;
			}
		}
	}

	if (bInCombat)
	{
		// 战斗中无输入：朝镜头内部后闪。
		// 后闪动画资产应为"角色面向镜头、向后退"（根运动朝屏幕里），故角色直接面向 CameraYaw。
		FRotator BackwardRot(0.f, CameraYaw, 0.f);
		BaseChar->SetActorRotation(BackwardRot);
		bOutBackward = true;
		return BaseChar->DodgeBackwardMontage ? BaseChar->DodgeBackwardMontage : BaseChar->DodgeMontage;
	}
	else
	{
		// 非战斗中无输入：朝镜头内部前冲（动画本身朝前，角色转到面向镜头）
		FRotator ForwardRot(0.f, CameraYaw, 0.f);
		BaseChar->SetActorRotation(ForwardRot);
		bOutBackward = false;
		return BaseChar->DodgeForwardMontage ? BaseChar->DodgeForwardMontage : BaseChar->DodgeMontage;
	}
}

void UGA_Dodge::OnDodgeMontageFinished()
{
	// 防重入：已被其他路径结束（如移动释放的直接收尾）直接返回
	if (!IsActive() || bSuppressMontageCallbacks) return;

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_Dodge::OnDodgeMontageInterrupted()
{
	// 防重入：已被其他路径结束（如移动释放的直接收尾）直接返回
	if (!IsActive() || bSuppressMontageCallbacks) return;

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UGA_Dodge::OnDodgeBackswingStarted(FGameplayEventData EventData)
{
	// 防护：任务已清理或 GA 已结束（清理中）时忽略事件
	if (!WaitBackswingTask || !IsActive()) return;

	// 进入闪避后摇区间（此后推杆即可释放）
	bInDodgeBackswing = true;

	UE_LOG(LogTemp, Warning, TEXT("[GA_Dodge] 闪避进入后摇区间（此后推杆可释放移动）"));
}

void UGA_Dodge::OnDodgeMovementStarted(FGameplayEventData EventData)
{
	// 防护：任务已清理或 GA 已结束（清理中）时忽略事件
	if (!WaitMovementTask || !IsActive()) return;

	// 只在后摇区间生效；翻滚主体（Root Motion 无敌位移段）推杆不释放
	if (!bInDodgeBackswing) return;
	if (!DodgeMontageTask || !DodgeMontageTask->IsActive()) return;

	UE_LOG(LogTemp, Warning, TEXT("[GA_Dodge] 闪避后摇中移动 → 提前结束闪避动画，交还移动控制"));

	// 与 GA_NormalAttack 剪断/释放同款的确定性收尾：抑制回调 + EndTask + 直接以自然结束语义
	// EndAbility——不依赖 EndTask 到底触发 OnInterrupted 还是 OnBlendOut（引擎行为不确定），
	// 收尾路径唯一、无双结束。GA 结束 → Status.Dodge.Active 自动消失 → 可立即起手普攻；
	// 无敌 GE 是时间制，不受提前结束影响（承诺的是时间不是姿势）
	bSuppressMontageCallbacks = true;

	// 显式停 Montage：EndTask 是否代停同为引擎不确定行为——闪避是 Root Motion 驱动，
	// 若没真停住，GA 已结束但翻滚位移仍锁死角色到动画自然结束（症状：释放日志出现却还得等播完）。
	// Montage_Stop 幂等（对已停/在停的 Montage 再调无副作用）；0.1s 混合与攻击侧 SegmentBlendTime 观感一致
	if (const FGameplayAbilityActorInfo* CurrentInfo = GetCurrentActorInfo())
	{
		if (UAnimInstance* AnimInstance = CurrentInfo->GetAnimInstance())
		{
			AnimInstance->Montage_Stop(0.1f);
		}
	}

	DodgeMontageTask->EndTask();
	DodgeMontageTask = nullptr;
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_Dodge::OnPerfectDodge(FGameplayEventData EventData)
{
	// Instigator = 攻击者（ApplyDamageToActor 事件侧填的 this）；
	// 不同引擎版本该字段 const 修饰有差异，统一剥掉保证可编译
	AActor* Attacker = const_cast<AActor*>(EventData.Instigator.Get());
	AHelloCharacterBase* AttackerChar = Cast<AHelloCharacterBase>(Attacker);
	if (!AttackerChar)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GA_Dodge] 完美闪避事件缺少有效攻击者，跳过减速"));
		return;
	}

	// 攻击者进入慢动作；恢复由攻击者侧的世界计时器负责（不受其减速影响，准时恢复）
	AttackerChar->SlowTime(PerfectSlowScale, PerfectSlowDuration);
	UE_LOG(LogTemp, Warning, TEXT("[GA_Dodge] 完美闪避！[%s] 减速至 %.2f 倍，持续 %.2f 秒"),
		*AttackerChar->GetName(), PerfectSlowScale, PerfectSlowDuration);
}
