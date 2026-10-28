// Fill out your copyright notice in the Description page of Project Settings.

#include "PlayerController/HelloPlayerController.h"
#include "AbilitySystemComponent.h"
#include "Camera/HelloPlayerCameraManager.h"
#include "EnhancedInputSubsystems.h"
#include "Hello/HelloCharacter.h"
#include "PlayerState/HelloPlayerState.h"
#include "GameplayTags/HelloGameplayTags.h"
#include "Net/UnrealNetwork.h"

AHelloPlayerController::AHelloPlayerController()
{
	// 阶段 1：指定 Hello 项目的相机管理器，由 Controller 层负责镜头。
	PlayerCameraManagerClass = AHelloPlayerCameraManager::StaticClass();
}

void AHelloPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHelloPlayerController, TeamMembers);
}

void AHelloPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Started, this, &AHelloPlayerController::MoveStarted);
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AHelloPlayerController::Move);
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Completed, this, &AHelloPlayerController::MoveCompleted);
		EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AHelloPlayerController::Look);

		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &AHelloPlayerController::Jump);
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &AHelloPlayerController::StopJump);

		EnhancedInput->BindAction(SwitchToNextAction, ETriggerEvent::Started, this, &AHelloPlayerController::OnSwitchNext);
		EnhancedInput->BindAction(SwitchTo1Action, ETriggerEvent::Started, this, &AHelloPlayerController::OnSwitchTo1);

		// 普通攻击输入：点按（Started）+ 长按持续（Triggered）+ 松开清理（Completed）
		EnhancedInput->BindAction(NormalAttackAction, ETriggerEvent::Started, this, &AHelloPlayerController::OnNormalAttackStarted);
		EnhancedInput->BindAction(NormalAttackAction, ETriggerEvent::Triggered, this, &AHelloPlayerController::OnNormalAttackTriggered);
		EnhancedInput->BindAction(NormalAttackAction, ETriggerEvent::Completed, this, &AHelloPlayerController::OnNormalAttackCompleted);

		// TODO:技能输入，暂时没实现，只绑定函数
		EnhancedInput->BindAction(MainCharacterQAction, ETriggerEvent::Started, this, &AHelloPlayerController::OnMainCharacterQAction);
		EnhancedInput->BindAction(MainCharacterZAction, ETriggerEvent::Started, this, &AHelloPlayerController::OnMainCharacterZAction);

		// 闪避输入：点按立即触发
		EnhancedInput->BindAction(DodgeAction, ETriggerEvent::Started, this, &AHelloPlayerController::OnDodgeStarted);
	}
}

void AHelloPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (DefaultMappingContext)
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}
}

void AHelloPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!HasAuthority() || !TeamConfig) return;

	if (TeamMembers.Num() == 0)
	{
		AHelloCharacter* Leader = Cast<AHelloCharacter>(InPawn);
		if (Leader)
		{
			TeamMembers.Add(Leader);
			CurrentMemberIndex = 0;
			SpawnTeammatesFromConfig();
		}
	}
}

void AHelloPlayerController::OnSwitchNext()
{
	if (TeamMembers.Num() <= 1) return;

	int32 NextIndex = (CurrentMemberIndex + 1) % TeamMembers.Num();
	RequestSwitchToCharacter(TeamMembers[NextIndex]);
	CurrentMemberIndex = NextIndex;
}

void AHelloPlayerController::OnSwitchTo1()
{
	if (TeamMembers.Num() <= 1) return;

	AHelloCharacter* Target = TeamMembers[1];
	if (!Target || Target == GetPawn()) return;

	CurrentMemberIndex = 1;
	RequestSwitchToCharacter(Target);
}

void AHelloPlayerController::OnMainCharacterZAction()
{
}

void AHelloPlayerController::OnMainCharacterQAction()
{
	AHelloCharacter* MainCharacter = Cast<AHelloCharacter>(GetPawn());
	if (MainCharacter)
	{
		UAbilitySystemComponent* ASC = MainCharacter->GetAbilitySystemComponent();
		if (ASC)
		{
			FGameplayTagContainer TagContainer;
			TagContainer.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.ActiveSkills")));
			ASC->TryActivateAbilitiesByTag(TagContainer);
		}
	}
}

void AHelloPlayerController::OnDodgeStarted()
{
	AHelloCharacter* MyChar = Cast<AHelloCharacter>(GetPawn());
	if (!MyChar) return;

	UAbilitySystemComponent* ASC = MyChar->GetAbilitySystemComponent();
	if (!ASC) return;

	// 直接按 Tag 激活闪避 GA——GAS 自动处理：
	// 取消进行中的普攻（CancelAbilitiesWithTag）、Block 检查、InstancedPerActor 重复激活防抖
	FGameplayTagContainer AbilityTags;
	AbilityTags.AddTag(HelloGameplayTags::Ability_Dodge_Dodge);
	ASC->TryActivateAbilitiesByTag(AbilityTags);
}

void AHelloPlayerController::OnNormalAttackStarted()
{
	// 点按触发：立刻处理一次攻击输入
	HandleAttackInput();
}

void AHelloPlayerController::OnNormalAttackTriggered()
{
	// 长按持续触发：标记为按住状态，由计时器控制频率
	if (!bIsHoldingAttack)
	{
		bIsHoldingAttack = true;
		// 第一帧不调用 HandleAttackInput()，避免与 Started 重复（防抖）
		// 启动计时器，按间隔持续触发
		GetWorldTimerManager().SetTimer(AttackHeldTimerHandle, this,
			&AHelloPlayerController::AttackHeldTimerCallback,
			AttackHoldInterval, true);
	}
}

void AHelloPlayerController::OnNormalAttackCompleted()
{
	// 松开：清理计时器和状态
	bIsHoldingAttack = false;
	GetWorldTimerManager().ClearTimer(AttackHeldTimerHandle);
}

void AHelloPlayerController::AttackHeldTimerCallback()
{
	// 长按计时器回调：每隔 AttackHoldInterval 触发一次
	HandleAttackInput();
}

void AHelloPlayerController::HandleAttackInput()
{
	AHelloCharacter* MyChar = Cast<AHelloCharacter>(GetPawn());
	if (!MyChar) return;

	UHelloASC* ASC = Cast<UHelloASC>(MyChar->GetAbilitySystemComponent());
	if (!ASC) return;

	// 检查是否在连段中（通过状态标签 Status.Combo.Attacking）
	if (ASC->HasMatchingGameplayTag(HelloGameplayTags::Status_Combo_Attacking))
	{
		// 已在连段中：直接发事件给 GA（不再写缓冲 Tag）
		FGameplayEventData Payload;
		ASC->HandleGameplayEvent(HelloGameplayTags::Event_Combo_AttackPressed, &Payload);
	}
	else
	{
		// 新连段：尝试激活（GAS 会检查 Block / Cooldown）
		FGameplayTagContainer AbilityTags;
		AbilityTags.AddTag(HelloGameplayTags::Ability_Combo_NormalAttack);

		ASC->TryActivateAbilitiesByTag(AbilityTags);
	}
}

void AHelloPlayerController::RequestSwitchToCharacter(AHelloCharacter* NewTarget)
{
	if (!NewTarget) return;
	ServerSwitchToCharacter(NewTarget);
}

void AHelloPlayerController::ServerSwitchToCharacter_Implementation(AHelloCharacter* NewTarget)
{
	if (!NewTarget) return;

	AHelloCharacter* PreviousChar = Cast<AHelloCharacter>(GetPawn());
	if (!PreviousChar || PreviousChar == NewTarget) return;

	// 对调两个角色的世界位置
	FVector PrevLoc = PreviousChar->GetActorLocation();
	FRotator PrevRot = PreviousChar->GetActorRotation();
	FVector NewLoc = NewTarget->GetActorLocation();
	FRotator NewRot = NewTarget->GetActorRotation();
	PreviousChar->SetActorLocation(NewLoc);
	PreviousChar->SetActorRotation(NewRot);
	NewTarget->SetActorLocation(PrevLoc);
	NewTarget->SetActorRotation(PrevRot);

	// 换人后保留玩家当前视角：Possess 会把 ControlRotation 对齐到新 Pawn 的 ActorRotation。
	// 本项目角色朝向跟随移动方向（HelloCharacter: bOrientRotationToMovement = true），
	// 身体朝向与视线 Yaw 经常不一致，不还原的话切人瞬间镜头会绕到新角色“身体背后”，
	// 看起来像相机位置被重置。
	// 注：本引擎版本的 Possess 不可作为可覆盖虚函数，且 OnPossess 执行时旧 Pawn 已被松开，
	// 所以在换人路径内直接包围 Possess 调用。
	const FRotator SavedControlRotation = GetControlRotation();

	// 切换 PC 控制的 Pawn
	Possess(NewTarget);

	if (GetPawn() == NewTarget)
	{
		SetControlRotation(SavedControlRotation);

		UE_LOG(LogTemp, Log, TEXT("[HelloCamera] Possess preserved ControlRotation: Pitch=%.2f Yaw=%.2f Roll=%.2f (Pawn=%s)"),
			SavedControlRotation.Pitch, SavedControlRotation.Yaw, SavedControlRotation.Roll,
			*GetNameSafe(NewTarget));
	}

	// 通知 GA_SwitchCharacter 执行特效
	AHelloPlayerState* PS = GetPlayerState<AHelloPlayerState>();
	if (PS)
	{
		UAbilitySystemComponent* ASC = PS->GetAbilitySystemComponent();
		if (ASC)
		{
			FGameplayEventData EventData;
			EventData.Instigator = PreviousChar;
			EventData.OptionalObject = NewTarget;
			ASC->HandleGameplayEvent(FGameplayTag::RequestGameplayTag("Event.Switch.Request"), &EventData);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("Switched from %s to %s"), *PreviousChar->GetName(), *NewTarget->GetName());
}

void AHelloPlayerController::SpawnTeammatesFromConfig()
{
	AHelloCharacter* Leader = TeamMembers.Num() > 0 ? TeamMembers[0] : GetPawn<AHelloCharacter>();
	if (!Leader) return;

	FVector BaseLoc = Leader->GetActorLocation();
	FRotator BaseRot = Leader->GetActorRotation();

	for (int32 i = 0; i < TeamConfig->TeamMembers.Num(); ++i)
	{
		if (i == 0) continue;

		const FTeamMemberConfig& Config = TeamConfig->TeamMembers[i];
		if (!Config.CharacterClass) continue;

		FVector SpawnLoc = BaseLoc + Config.SpawnOffset;

		AHelloCharacter* NewChar = GetWorld()->SpawnActor<AHelloCharacter>(Config.CharacterClass, SpawnLoc, BaseRot);
		if (NewChar)
		{
			TeamMembers.Add(NewChar);
		}
	}
}

void AHelloPlayerController::MoveStarted(const FInputActionValue& Value)
{
	// 电平语义下无需处理（Triggered 负责更新值，Completed 负责清零）；保留绑定占位
}

void AHelloPlayerController::MoveCompleted(const FInputActionValue& Value)
{
	// 按键松开 = 没有方向输入意图；惯性滑行不会触发 Triggered，值保持零
	LastMoveInput = FVector2D::ZeroVector;
	UE_LOG(LogTemp, Log, TEXT("[Input] MoveCompleted"));
}

void AHelloPlayerController::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();

	// 无条件更新：Triggered 期间每帧都是真实输入值（按住即非零，含后摇释放后的重新推杆）
	LastMoveInput = MovementVector;

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn) return;

	// 移动事件：推杆值非零时每帧发，GA 侧幂等消化（阶段 2 GA_NormalAttack / 阶段 3 GA_Dodge 的
	// 后摇移动释放监听此事件）。故意不做边沿检测——若只在"从停到动"发一次，玩家在后摇开始前
	// 已推杆、后摇开始时按住不松，就永远等不到新事件，后摇释放失效（方案避坑 #1）。
	// 每帧一次本地 HandleGameplayEvent、无消费者时即刻丢弃，开销可忽略
	if (!MovementVector.IsNearlyZero(0.1f))
	{
		if (AHelloCharacter* Char = Cast<AHelloCharacter>(ControlledPawn))
		{
			if (UAbilitySystemComponent* ASC = Char->GetAbilitySystemComponent())
			{
				FGameplayEventData Payload;
				Payload.EventTag = HelloGameplayTags::Event_Movement_Started;
				ASC->HandleGameplayEvent(HelloGameplayTags::Event_Movement_Started, &Payload);
			}
		}
	}

	const FRotator YawRotation(0, GetControlRotation().Yaw, 0);

	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	ControlledPawn->AddMovementInput(ForwardDirection, MovementVector.Y);
	ControlledPawn->AddMovementInput(RightDirection, MovementVector.X);
}

void AHelloPlayerController::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();
	AddYawInput(LookAxisVector.X);
	AddPitchInput(LookAxisVector.Y);
}

void AHelloPlayerController::Jump()
{
	if (ACharacter* MyCharacter = Cast<AHelloCharacter>(GetPawn()))
	{
		MyCharacter->Jump();
	}
}

void AHelloPlayerController::StopJump()
{
	if (ACharacter* MyCharacter = Cast<AHelloCharacter>(GetPawn()))
	{
		MyCharacter->StopJumping();
	}
}
