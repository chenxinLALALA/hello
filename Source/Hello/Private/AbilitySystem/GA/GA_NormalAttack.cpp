// Copyright Hello Project. All Rights Reserved.

#include "AbilitySystem/GA/GA_NormalAttack.h"
#include "AbilitySystemComponent.h"
#include "Character/HelloCharacterBase.h"
#include "DataAsset/ComboConfigDataAsset.h"
#include "GameplayTags/HelloGameplayTags.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

UGA_NormalAttack::UGA_NormalAttack()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// 闪避进行中禁起手普攻（GA_Dodge 的 ActivationOwnedTags 持有此 Tag）
	ActivationBlockedTags.AddTag(HelloGameplayTags::Status_Dodge_Active);
}

void UGA_NormalAttack::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bComboMemorySaved = false;

	// 应用连段状态 GE（Status.Combo.Attacking）
	if (ComboAttackingGE)
	{
		FGameplayEffectSpecHandle AttackingSpec = MakeOutgoingGameplayEffectSpec(ComboAttackingGE, 1);
		ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, AttackingSpec);
	}
	else
	{
		// 若没配置 GE，手动挂 Loose Tag（不推荐，仅兜底）
		UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
		ASC->AddLooseGameplayTag(HelloGameplayTags::Status_Combo_Attacking);
	}

	// 启动最大连击时间计时器
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(MaxComboTimerHandle, this,
			&UGA_NormalAttack::OnMaxComboTimeout,
			MaxComboDuration, false);  // false = 单次触发
	}

	// 播放第一段动画
	PlayCurrentSegment();
}

void UGA_NormalAttack::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();

	// ---- 连段记忆：取消结束时记下段号（必须趁 Count.N Tag 还在时读）----
	// 挂 GE 延后到清 Tag 之后：GE 授予的 Tag 与 Loose Tag 同名，
	// 若先挂 GE 再执行 RemoveLooseGameplayTag（包括取消链第二次进入本函数的清理），
	// 会计数减穿、把 GE 授予的 Tag 一并从 ASC 移除——上一版丢记忆的根因
	int32 InterruptedIndex = 0;
	if (bWasCancelled && !bComboMemorySaved)
	{
		if (ComboMemoryGE)
		{
			InterruptedIndex = GetCurrentComboIndex();

			// 后摇期/窗口期/宽容期被打断 = 本段攻击已交付：视作完成，记忆推进到下一段（第 5 段循环回第 1 段）；
			// 有效帧打断（未进后摇）维持现状：记 N，闪避后重播第 N 段。
			// 记 N+1（而非记 N 重播）的理由：记 N 会出现"等得越久反而越亏"的语义倒挂（T4.4，2026-09-12）。
			// 宽容期（重启期）记 N+1 的真正依据是「闪避永不亏连段」哲学——该期按攻击/自然到期均为第 1 段、
			// 闪避独享 N+1 的倒挂被明确接受为取舍（2026-09-20 复审，方案文档 §10 #9）；T4.4 完成度论证不覆盖此期
			const bool bWindowActive = ASC && ASC->HasMatchingGameplayTag(HelloGameplayTags::Status_Combo_Window);
			const bool bAttackDelivered = bInBackswing
				|| bWindowActive
				|| (ASC && ASC->HasMatchingGameplayTag(HelloGameplayTags::Status_Combo_ResetPending));
			if (bAttackDelivered && InterruptedIndex > 0)
			{
				const int32 InterruptedFromIndex = InterruptedIndex;
				InterruptedIndex = (InterruptedIndex >= 5) ? 1 : InterruptedIndex + 1;

				UE_LOG(LogTemp, Warning,
					TEXT("[GA_NormalAttack] [%s] 第 %d 段%s被闪避打断 → 视作完成，连段记忆第 %d 段"),
					*GetAvatarActorFromActorInfo()->GetName(), InterruptedFromIndex,
					bInBackswing ? TEXT("后摇期") : (bWindowActive ? TEXT("窗口期") : TEXT("重启期")), InterruptedIndex);
			}
		}
		else
		{
			// 未配置记忆GE报错日志
			UE_LOG(LogTemp, Error, TEXT("[GA_NormalAttack] 记忆跳过：ComboMemoryGE 未配置（应填在 GA_NormalAttack 蓝图 Combo|ComboMemory 分类）"));
		}
	}

	// 清所有连段相关 Tag（Plan B：Window/ResetPending 为 Loose Tag，此处即主清理路径）
	if (ASC)
	{
		ASC->RemoveLooseGameplayTag(HelloGameplayTags::Status_Combo_Attacking);
		ASC->RemoveLooseGameplayTag(HelloGameplayTags::Status_Combo_Window);
		ASC->RemoveLooseGameplayTag(HelloGameplayTags::Status_Combo_ResetPending);
		//ASC->RemoveLooseGameplayTag(HelloGameplayTags::Input_Buffer_Combo);

		// 清段数 Loose Tag：已挂记忆（取消链第二次进入）时必须跳过，防对 GE 授予的同名 Tag 减穿
		if (!bComboMemorySaved)
		{
			for (int32 i = 1; i <= 5; ++i)
			{
				ASC->RemoveLooseGameplayTag(HelloGameplayTags::GetComboCountTag(i));
			}
		}

		// 重置角色身上的连段段号（下次伤害不会误用上一轮的段配置）
		if (AHelloCharacterBase* BaseChar = Cast<AHelloCharacterBase>(GetAvatarActorFromActorInfo()))
		{
			BaseChar->CurrentComboSegment = 0;
		}

		// Loose 清理完成后挂记忆 GE：Tag 计数从此完全归属 GE，到期自动回收（过期=回第 1 段）
		if (InterruptedIndex > 0)
		{
			SaveComboMemory(InterruptedIndex);
		}
	}

	// 连段结束兜底收武器：取消链（闪避打断等）会跳过未触发的 DisableWeaponTrace 通知，
	// 不收武器会一直热着（甚至热过整个闪避动作）——幂等，已关则直接返回
	if (AHelloCharacterBase* EndChar = Cast<AHelloCharacterBase>(GetAvatarActorFromActorInfo()))
	{
		EndChar->DisableWeaponTrace();
	}

	// 清理任务
	if (CurrentMontageTask)
	{
		CurrentMontageTask->EndTask();
		CurrentMontageTask = nullptr;
	}
	if (WaitInputTask)
	{
		WaitInputTask->EndTask();
		WaitInputTask = nullptr;
	}
	if (WaitBackswingTask)
	{
		WaitBackswingTask->EndTask();
		WaitBackswingTask = nullptr;
	}
	if (WaitMovementTask)
	{
		WaitMovementTask->EndTask();
		WaitMovementTask = nullptr;
	}

	// 清理计时器
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MaxComboTimerHandle);
		World->GetTimerManager().ClearTimer(ComboExpiryTimerHandle);
		World->GetTimerManager().ClearTimer(WindowTimerHandle);
	}

	// 重置状态标志，防跨连段残留（bInBackswing/bBackswingReleased 的闪避记忆读取发生在函数更早处，互不影响）。
	// bHasPendingInput 必须一并清：GA 是 InstancedPerActor 实例复用，连段超时/结束时若动画已播完，
	// 死区里点的按键留下的预输入会跨连段存活——下次激活第 1 段播完即被 OnMontageCompleted 消费自动接第 2 段
	// （实测症状：一次点击出两段攻击）
	bInBackswing = false;
	bBackswingReleased = false;
	bSuppressMontageCallbacks = false;
	bHasPendingInput = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

int32 UGA_NormalAttack::GetCurrentComboIndex() const
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return 0;

	for (int32 i = 1; i <= 5; ++i)
	{
		if (ASC->HasMatchingGameplayTag(HelloGameplayTags::GetComboCountTag(i)))
			return i;
	}
	return 0;
}

const FComboSegmentData* UGA_NormalAttack::GetSegmentData(int32 InSegmentIndex) const
{
	// ComboConfig 是基类成员，敌人（HelloEnemy）也能配连段——必须 Cast 到基类；
	// Cast 到玩家类 AHelloCharacter 会让敌人侧拿到空指针、攻击瞬间 EndCombo
	AHelloCharacterBase* BaseChar = Cast<AHelloCharacterBase>(GetAvatarActorFromActorInfo());
	if (!BaseChar || !BaseChar->ComboConfig) return nullptr;

	return &BaseChar->ComboConfig->GetSegment(InSegmentIndex);
}

void UGA_NormalAttack::PlayCurrentSegment()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) { EndCombo(); return; }

	// 新段开始：重置后摇标志、释放标志与回调抑制（剪断/释放路径残留的防御性清理）
	bInBackswing = false;
	bBackswingReleased = false;
	bSuppressMontageCallbacks = false;

	// 上一段的窗口/重启 Tag 残留清理（Plan B 漏斗：推进/重启/接回全走这里，与定时器清理成对）
	ASC->RemoveLooseGameplayTag(HelloGameplayTags::Status_Combo_Window);
	ASC->RemoveLooseGameplayTag(HelloGameplayTags::Status_Combo_ResetPending);

	// 每播一段就重置最大连击时间计时器（连段只在"停止攻击超时"时才重置，而非从第一段累计计时）
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(MaxComboTimerHandle, this,
			&UGA_NormalAttack::OnMaxComboTimeout,
			MaxComboDuration, false);

		// 新段开始 = 上一段的窗口/重启周期作废（推进、重启期重启、连段记忆接回全走这里，统一清理）
		World->GetTimerManager().ClearTimer(WindowTimerHandle);
		World->GetTimerManager().ClearTimer(ComboExpiryTimerHandle);
	}

	//连段索引
	int32 CurrentIndex = GetCurrentComboIndex();

	if (CurrentIndex == 0)
	{
		// 无段数 Tag，从第 1 段开始
		CurrentIndex = 1;
		ASC->AddLooseGameplayTag(HelloGameplayTags::GetComboCountTag(1));
	}
	else if (ComboMemoryEffectHandle.IsValid())
	{
		// 从连段记忆接回（段数 Tag 来自记忆 GE）：移除 GE 及其 Tag，改挂 Loose
		// —— 后续推进/清理走既有连段路径，零特判
		ASC->RemoveActiveGameplayEffect(ComboMemoryEffectHandle);
		ComboMemoryEffectHandle.Invalidate();
		ASC->AddLooseGameplayTag(HelloGameplayTags::GetComboCountTag(CurrentIndex));

		UE_LOG(LogTemp, Warning, TEXT("[GA_NormalAttack] 连段记忆命中 → 接回第 %d 段"), CurrentIndex);
	}

	// 同步段号到角色基类（ApplyDamageToActor 据此从 ComboConfig 取该段 DamageGE）
	if (AHelloCharacterBase* BaseChar = Cast<AHelloCharacterBase>(GetAvatarActorFromActorInfo()))
	{
		BaseChar->CurrentComboSegment = CurrentIndex;
	}

	const FComboSegmentData* Segment = GetSegmentData(CurrentIndex);
	if (!Segment || !Segment->SegmentSequence)
	{
		EndCombo();
		return;
	}

	
	USkeletalMeshComponent* Mesh = Cast<USkeletalMeshComponent>(GetAvatarActorFromActorInfo()->GetComponentByClass(USkeletalMeshComponent::StaticClass()));
	if (!Mesh || !Mesh->GetAnimInstance())
	{
		EndCombo();
		return;
	}
	// 从 AnimationSequence 创建临时 Montage（只创建，不播放；播放交给 PlayMontageAndWait 单独完成）
	// BlendIn/Out = SegmentBlendTime：剪断推进/自然换段时短混合过渡（原来是 0 硬切）
	UAnimMontage* TempMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(
		Segment->SegmentSequence, FName("DefaultSlot"), SegmentBlendTime, SegmentBlendTime, 1.0f);

	if (!TempMontage)
	{
		EndCombo();
		return;
	}

	// 播动画
	CurrentMontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, TempMontage, 1.0f, NAME_None, false, 1.0f);
	// 动画自然播放完毕（不是被打断）时触发
	CurrentMontageTask->OnBlendOut.AddDynamic(this, &UGA_NormalAttack::OnMontageCompleted);
	// 动画被中断（角色受击、切换技能、移动打断）时触发 → 调 OnMontageCancelled（结束连段）
	CurrentMontageTask->OnInterrupted.AddDynamic(this, &UGA_NormalAttack::OnMontageCancelled);
	// AbilityTask 才真正开始工作
	CurrentMontageTask->ReadyForActivation();

	// 启动按键等待
	WaitInputTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, HelloGameplayTags::Event_Combo_AttackPressed);
	WaitInputTask->EventReceived.AddDynamic(this, &UGA_NormalAttack::OnAttackPressed);
	WaitInputTask->ReadyForActivation();

	// 启动后摇事件等待（WaitGameplayEvent 单 Tag，需要独立任务实例）
	WaitBackswingTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, HelloGameplayTags::Event_Combo_BackswingStart);
	WaitBackswingTask->EventReceived.AddDynamic(this, &UGA_NormalAttack::OnBackswingStarted);
	WaitBackswingTask->ReadyForActivation();

	// 启动移动事件等待（后摇移动释放；事件每帧发、bBackswingReleased 幂等消化）
	WaitMovementTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, HelloGameplayTags::Event_Movement_Started);
	WaitMovementTask->EventReceived.AddDynamic(this, &UGA_NormalAttack::OnMovementStarted);
	WaitMovementTask->ReadyForActivation();

	// 测试日志：动画开始（墙钟时间）。减速测试判读：持续时长 / 动画资产时长 = 1/流速
	/*SegmentStartTime = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Warning, TEXT("[GA_NormalAttack] [%s] 第 %d 段攻击动画开始 (t=%.3f)"),
		*GetAvatarActorFromActorInfo()->GetName(), CurrentIndex, SegmentStartTime);*/
}

void UGA_NormalAttack::StartNextSegment()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) { EndCombo(); return; }

	int32 CurrentIndex = GetCurrentComboIndex();
	if (CurrentIndex == 0) { EndCombo(); return; }

	// 消费缓冲（缓冲职责最终由 GA 内部的 bHasPendingInput 这个 bool负责，此处已经失效）
	//ASC->RemoveLooseGameplayTag(HelloGameplayTags::Input_Buffer_Combo);

	// 推进即关窗（Plan B 版）：摘窗口/重启 Loose Tag + 清定时器——旧 GE 方案的"句柄移除"退役。
	// Tag 残留漏进下一段会造成连点加速（2026-09-15 修正的既有行为，语义不变）；
	// PlayCurrentSegment 漏斗还会再兜底清一次（幂等）
	ASC->RemoveLooseGameplayTag(HelloGameplayTags::Status_Combo_Window);
	ASC->RemoveLooseGameplayTag(HelloGameplayTags::Status_Combo_ResetPending);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WindowTimerHandle);
		World->GetTimerManager().ClearTimer(ComboExpiryTimerHandle);
	}

	// 清当前段 Tag
	ASC->RemoveLooseGameplayTag(HelloGameplayTags::GetComboCountTag(CurrentIndex));

	// 增加段数 Tag：第 5 段循环回第 1 段（连段成环——第 5 段后摇剪断/窗口推进都接下一套）
	int32 NextIndex = (CurrentIndex >= 5) ? 1 : CurrentIndex + 1;
	ASC->AddLooseGameplayTag(HelloGameplayTags::GetComboCountTag(NextIndex));

	// 先清空预输入标志，防止清理任务时重新触发设置
	bHasPendingInput = false;

	// 剪断/换段期间抑制 Montage 回调：EndTask 会 Stop 在播 Montage，
	// 可能同步触发 OnBlendOut/OnInterrupted → 由两回调开头的守卫拦下（PlayCurrentSegment 重置）
	bSuppressMontageCallbacks = true;

	// 剪断会跳过剪断点之后的所有 AnimNotify——若武器碰撞还开着（后摇里也有检测窗口），
	// 那个 DisableWeaponTrace 永远不会触发。显式收武器，保证下一段从冷状态起手（幂等，已关则直接返回）
	if (AHelloCharacterBase* BaseChar = Cast<AHelloCharacterBase>(GetAvatarActorFromActorInfo()))
	{
		BaseChar->DisableWeaponTrace();
	}

	// 清理旧任务（加 IsActive 守卫，避免对已自然结束的任务重复 EndTask）
	if (CurrentMontageTask)
	{
		if (CurrentMontageTask->IsActive())
		{
			CurrentMontageTask->EndTask();
		}
		CurrentMontageTask = nullptr;
	}
	if (WaitInputTask)
	{
		if (WaitInputTask->IsActive())
		{
			WaitInputTask->EndTask();
		}
		WaitInputTask = nullptr;
	}
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

	// 播下一段
	PlayCurrentSegment();
}

void UGA_NormalAttack::EndCombo()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_NormalAttack::ScheduleAdvance()
{
	// 已安排过就跳过，防止重复调度
	if (bAdvancePending) return;
	bAdvancePending = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UGA_NormalAttack::AdvanceComboSegment));
	}
}

void UGA_NormalAttack::AdvanceComboSegment()
{
	if (!bAdvancePending) return;
	bAdvancePending = false;

	// 若 GA 已结束，不再推进
	if (!IsActive())
	{
		return;
	}

	StartNextSegment();
}

void UGA_NormalAttack::OnAttackPressed(FGameplayEventData EventData)
{
	// 保护：如果 WaitInputTask 为空或 GA 未激活，说明正在清理中或已结束，忽略事件
	if (!WaitInputTask || !IsActive())
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return;

	// 设置预输入标志（动画中按，等动画播完自动连）
	bHasPendingInput = true;

	// 检查窗口是否开启（窗口开时立即推进，不需要等动画播完）
	bool bHasWindow = ASC->HasMatchingGameplayTag(HelloGameplayTags::Status_Combo_Window);

	if (bHasWindow)
	{
		// 窗口开 → 推进段数（延迟到下一帧，避免在 WaitInputTask 回调内重入）
		bHasPendingInput = false;  // 消费预输入
		ScheduleAdvance();
	}
	else if (bInBackswing)
	{
		// 后摇区间内按下 → 立即剪断剩余后摇接下一段（第 5 段循环回第 1 段），不必等动画自然播完
		bHasPendingInput = false;  // 消费预输入
		ScheduleAdvance();
	}
	else if (ASC->HasMatchingGameplayTag(HelloGameplayTags::Status_Combo_ResetPending))
	{
		// 重启期（窗口已过期、宽容期尾巴）内按 → 重启第 1 段——原输入死区（存预输入干等超时）的修复。
		// GA 内部特权通道：不走 CanActivateAbility 激活检查（闪避/硬直瞬间按键也不丢），
		// 与连段记忆同思路的"余量"设计；方案第 11 节
		bHasPendingInput = false;  // 消费预输入
		// 重启 Tag/定时器由随后的 PlayCurrentSegment 漏斗统一清理（推进即关窗同语义，无需此处重复）

		// 清段数 Tag
		for (int32 i = 1; i <= 5; ++i)
		{
			ASC->RemoveLooseGameplayTag(HelloGameplayTags::GetComboCountTag(i));
		}

		// 重启（ActivateAbility 已经调过，这里直接播第 1 段；PlayCurrentSegment 内会清段结束定时器）
		ASC->AddLooseGameplayTag(HelloGameplayTags::GetComboCountTag(1));
		PlayCurrentSegment();
	}
	else
	{
		// 死区诊断日志：此刻窗口/后摇/重启期三个消费者都不在。预输入的消费者只有"动画播完"——
		// MontageInPlaying=1 属正常（动画中预输入）；=0 即死区实锤（输入将沉睡到连段结束被 EndAbility 清除）。
		// Plan B：Tag 即状态本身（无 GE 中间层），窗口Tag/重启Tag 直接反映定时器生命周期
		UE_LOG(LogTemp, Warning,
			TEXT("[GA_NormalAttack] [%s] 攻击输入未命中任何分支 → 存预输入待消费（Montage在播=%d）｜按键时状态：窗口Tag=%d 重启Tag=%d"),
			*GetAvatarActorFromActorInfo()->GetName(),
			(CurrentMontageTask && CurrentMontageTask->IsActive()) ? 1 : 0,
			bHasWindow ? 1 : 0,
			ASC->HasMatchingGameplayTag(HelloGameplayTags::Status_Combo_ResetPending) ? 1 : 0);
	}
}

void UGA_NormalAttack::OnBackswingStarted(FGameplayEventData EventData)
{
	// 防护：任务已清理或 GA 已结束（清理中）时忽略事件
	if (!WaitBackswingTask || !IsActive())
	{
		return;
	}

	// 进入后摇区间（bInBackswing 同时供闪避记忆路径判断——阶段 3 接入）
	bInBackswing = true;

	// 有预输入 → 剪掉后摇直接推进（第 5 段循环回第 1 段，连段成环）
	if (bHasPendingInput)
	{
		bHasPendingInput = false;  // 消费预输入
		ScheduleAdvance();         // 下一帧执行，避免在通知回调栈内 EndTask Montage

		// 后摇剪断 Log
		const int32 CurIdx = GetCurrentComboIndex();
		const int32 NextIdx = (CurIdx >= 5) ? 1 : CurIdx + 1;
		UE_LOG(LogTemp, Warning, TEXT("[GA_NormalAttack] [%s] 第 %d 段攻击中已有预输入 → 后摇起点剪断，接第 %d 段"),
			*GetAvatarActorFromActorInfo()->GetName(), CurIdx, NextIdx);
	}
}

void UGA_NormalAttack::OnMovementStarted(FGameplayEventData EventData)
{
	// 防护：任务已清理或 GA 已结束（清理中）时忽略事件
	if (!WaitMovementTask || !IsActive())
	{
		return;
	}

	// 只在后摇区间且尚未释放时生效；攻击有效帧推杆不释放（保持现有站桩攻击）
	if (!bInBackswing || bBackswingReleased)
	{
		return;
	}
	if (!CurrentMontageTask || !CurrentMontageTask->IsActive())
	{
		return;
	}

	bBackswingReleased = true;

	UE_LOG(LogTemp, Warning, TEXT("[GA_NormalAttack] [%s] 第 %d 段后摇中移动 → 释放动画，窗口提前开启"),
		*GetAvatarActorFromActorInfo()->GetName(), GetCurrentComboIndex());

	// ① 收武器：释放同样会跳过后摇里未触发的 DisableWeaponTrace 通知（后摇里可能有检测窗口）
	if (AHelloCharacterBase* BaseChar = Cast<AHelloCharacterBase>(GetAvatarActorFromActorInfo()))
	{
		BaseChar->DisableWeaponTrace();
	}

	// ② 停 Montage：回调抑制（与 StartNextSegment 剪断路径同款）。
	//    bInBackswing 保持 true——释放只换表现，不改变"攻击已交付"事实（闪避打断记忆 N+1 语义不变）
	bSuppressMontageCallbacks = true;

	// 显式停 Montage：EndTask 是否代停为引擎不确定行为（与 GA_Dodge 释放同坑，用户实测暴露）。
	// 释放后 GA 继续存活等窗口输入，若 Montage 没真停，全身动画会一直盖住跑步姿态直到自然播完
	if (const FGameplayAbilityActorInfo* CurrentInfo = GetCurrentActorInfo())
	{
		if (UAnimInstance* AnimInstance = CurrentInfo->GetAnimInstance())
		{
			AnimInstance->Montage_Stop(SegmentBlendTime);
		}
	}

	CurrentMontageTask->EndTask();
	CurrentMontageTask = nullptr;

	// ③ 窗口提前开（+第 5 段宽容期）：释放后 OnMontageCompleted 被抑制标志吞掉、
	//    永远不会走到自然播完开窗的旧路径——不开窗就接不了段（方案避坑 #2）
	OpenWindowAndGrace();
}

void UGA_NormalAttack::OpenWindowAndGrace()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) { EndCombo(); return; }

	const FComboSegmentData* Segment = GetSegmentData(GetCurrentComboIndex());
	if (!Segment) { EndCombo(); return; }

	// Plan B（2026-09-17）：窗口/重启期改为 Loose Tag + 世界定时器自管。
	// 原 GE+SetDuration 方案的运行时覆盖被应用路径从资产 Def 顶替（引擎源码定案），
	// 实际时长一直由资产 Magnitude 决定、每段差异化配置失效——现在时长唯一真值 = ComboConfig。
	// Loose Tag 与 GE 授予 Tag 的查询语义完全一致（HasMatchingGameplayTag），所有消费方零改动
	ASC->AddLooseGameplayTag(HelloGameplayTags::Status_Combo_Window);

	if (UWorld* World = GetWorld())
	{
		// 窗口到期 → OnWindowExpired：摘窗口 Tag、挂重启 Tag（重启期开始）
		World->GetTimerManager().SetTimer(WindowTimerHandle, this,
			&UGA_NormalAttack::OnWindowExpired, Segment->WindowDuration, false);

		// 段结束 → OnComboExpired：重启期也过完，连段正式结束（EndAbility 内清 Tag/定时器）
		const float ComboExpiryDelay = Segment->WindowDuration + Segment->GraceDuration;
		World->GetTimerManager().SetTimer(ComboExpiryTimerHandle, this,
			&UGA_NormalAttack::OnComboExpired, ComboExpiryDelay, false);

		// MaxComboTimer 使命结束（2026-09-17 方法2）：动画一旦播完，单段生命周期全权交给
		// ComboExpiryTimer（每段必到期必 EndCombo，比 MaxCombo 更严密）。
		// 不清的话 MaxComboDuration 会成为重启期的隐形上限——Grace 配长时（如 5 > 3）
		// 连段在重启期中途被静默截断（动画起算 vs 结束起算的时序差），用户实测发现
		World->GetTimerManager().ClearTimer(MaxComboTimerHandle);
	}
	/*// 窗口检验日志
	// 即时校验 + 开窗日志（打出的时长即 ComboConfig 真值，非 GE 资产值）
	const bool bTagAlive = ASC->HasMatchingGameplayTag(HelloGameplayTags::Status_Combo_Window);
	UE_LOG(LogTemp, Warning,
		TEXT("[GA_NormalAttack] 窗口开启：接续 %.2f 秒 + 重启尾巴 %.2f 秒（Window Tag即时校验=%d，时长来源=ComboConfig）"),
		Segment->WindowDuration, Segment->GraceDuration, bTagAlive ? 1 : 0);*/
}

void UGA_NormalAttack::OnMontageCompleted()
{
	// 剪断/换段期间被 Stop 的 Montage 触发的回调 → 直接丢弃（防重入）
	if (bSuppressMontageCallbacks) return;

	// 动画自然播完 = 后摇结束（窗口期/超时死区内不再视作后摇中）
	bInBackswing = false;

	// 测试日志：动画自然播完（BlendOut 触发），输出实际持续时长（墙钟）
	// 减速生效时：持续时长应明显大于动画资产时长（0.3 倍速下 1 秒动画 ≈ 3.3 秒）
	UE_LOG(LogTemp, Warning, TEXT("[GA_NormalAttack] [%s] 第 %d 段攻击动画结束，持续 %.3f 秒"),
		*GetAvatarActorFromActorInfo()->GetName(), GetCurrentComboIndex(),
		FPlatformTime::Seconds() - SegmentStartTime);

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) { EndCombo(); return; }

	// 检查是否有预输入（动画中按过攻击键）
	if (bHasPendingInput)
	{
		bHasPendingInput = false;
		// 延迟到下一帧执行，避免在 Montage 的 OnBlendOut 回调内 EndTask 旧任务导致重入
		ScheduleAdvance();
		return; // 直接返回，不开窗口
	}

	// 无预输入 → 开窗口（与后摇移动释放路径共用同一实现，见 OpenWindowAndGrace）
	OpenWindowAndGrace();
}

void UGA_NormalAttack::OnMontageCancelled()
{
	// 已在其他路径（闪避的取消链）中结束 → 防重入直接返回；
	// 剪断/换段期间被 Stop 的 Montage 触发的回调也在此丢弃
	if (!IsActive() || bSuppressMontageCallbacks) return;

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();

	// 闪避打断（Dodge GA 存续中，其 Tag 此时必然已挂上）：
	// 以"取消"状态结束——连段记忆在 EndAbility 内部按"读段号→清 Loose→挂 GE"的顺序完成
	if (ASC && ASC->HasMatchingGameplayTag(HelloGameplayTags::Status_Dodge_Active))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	// 测试日志：动画被中断时的实际时长（墙钟）——受击等其他打断场景
	UE_LOG(LogTemp, Warning, TEXT("[GA_NormalAttack] [%s] 第 %d 段攻击动画被中断，实际播了 %.3f 秒"),
		*GetAvatarActorFromActorInfo()->GetName(), GetCurrentComboIndex(),
		FPlatformTime::Seconds() - SegmentStartTime);

	// 动画中断（受击/技能打断），直接结束
	EndCombo();
}

void UGA_NormalAttack::SaveComboMemory(int32 InterruptedIndex)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC || InterruptedIndex <= 0) return;

	// 空壳 Duration GE：时长运行时覆盖为记忆窗口，段数 Tag 用 DynamicGrantedTags 动态授予
	// （引擎原生 Cooldown 授予 Tag 就是这个机制）；GE 到期 Tag 自动回收 → 过期后连段回第 1 段
	FGameplayEffectSpecHandle MemorySpec = MakeOutgoingGameplayEffectSpec(ComboMemoryGE, 1);
	MemorySpec.Data->SetDuration(ComboMemoryWindow, false);
	MemorySpec.Data->DynamicGrantedTags.AddTag(HelloGameplayTags::GetComboCountTag(InterruptedIndex));
	ComboMemoryEffectHandle = ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, MemorySpec);
	bComboMemorySaved = true;

	// 即时校验：GE 刚挂上，Tag 此刻必须在。不在 = GE 资产本身有问题（如 Duration Policy 配成 Instant）
	const bool bTagAlive = ASC->HasMatchingGameplayTag(HelloGameplayTags::GetComboCountTag(InterruptedIndex));
	UE_LOG(LogTemp, Warning,
		TEXT("[GA_NormalAttack] 被【闪避】打断 → 连段记忆第 %d 段（%.1f 秒，GE=%d，Tag即时校验=%d）"),
		InterruptedIndex, ComboMemoryWindow, ComboMemoryEffectHandle.IsValid(), bTagAlive);
}

void UGA_NormalAttack::OnMaxComboTimeout()
{
	// 最大连击时间超时，强制重置。
	// 注意：MaxCombo 从动画开始起算（每段重置）、ComboExpiry 从动画结束起算——
	// 重启期配置得比 MaxComboDuration 长时（如 Grace=5 > 3），本回调先于"窗口+重启期已过"触发，
	// 连段在重启期中途被此路径静默结束（2026-09-17 用户实测发现，补日志消除观测盲区）
	UE_LOG(LogTemp, Warning,
		TEXT("[GA_NormalAttack] [%s] 最大连击时间（%.1f 秒）超时 → 强制结束连段（当前段=%d）"),
		*GetAvatarActorFromActorInfo()->GetName(), MaxComboDuration, GetCurrentComboIndex());

	EndCombo();
}

void UGA_NormalAttack::OnComboExpired()
{
	// 窗口期 + 重启期都已过完且无人接段 → 连段正式结束（不存记忆：玩家放弃连段的语义）。
	// 之后按键由 PlayerController 走 TryActivateAbilitiesByTag 重新激活起手（过完整激活检查）
	if (!IsActive()) return;  // 已被其他路径结束的兜底

	UE_LOG(LogTemp, Warning, TEXT("[GA_NormalAttack] [%s] 窗口+重启期已过，连段超时结束"),
		*GetAvatarActorFromActorInfo()->GetName());

	EndCombo();
}

void UGA_NormalAttack::OnWindowExpired()
{
	// 窗口自然到期 → 进入重启期：摘窗口 Tag、挂重启 Tag。
	// 推进/重启/结束路径都会清定时器，本回调正常仅在"窗口自然过期"时触发（无 TagRemoved 误触发问题）
	if (!IsActive()) return;

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) return;

	ASC->RemoveLooseGameplayTag(HelloGameplayTags::Status_Combo_Window);

	// GraceDuration > 0 才有重启期；= 0 时段结束定时器同帧收尾连段
	const FComboSegmentData* Segment = GetSegmentData(GetCurrentComboIndex());
	const float GraceDuration = Segment ? Segment->GraceDuration : 0.f;
	if (GraceDuration > 0.f)
	{
		ASC->AddLooseGameplayTag(HelloGameplayTags::Status_Combo_ResetPending);
	}

	/*// 窗口过期日志
	// 检查点：Loose Tag 直挂即在场，校验恒应为 1（异常时查漏斗清理路径）
	const bool bTagAlive = ASC->HasMatchingGameplayTag(HelloGameplayTags::Status_Combo_ResetPending);
	UE_LOG(LogTemp, Warning,
		TEXT("[GA_NormalAttack] [%s] 第 %d 段窗口期结束 → 进入重启期（%.2f 秒内：按攻击重启第 1 段 / 按闪避记 N+1）｜检查点：ResetPending Tag=%d"),
		*GetAvatarActorFromActorInfo()->GetName(), GetCurrentComboIndex(),
		GraceDuration, bTagAlive ? 1 : 0);*/
}
