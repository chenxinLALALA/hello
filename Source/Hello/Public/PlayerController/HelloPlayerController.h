// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "InputMappingContext.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "DataAsset/TeamConfigDataAsset.h"
#include "HelloPlayerController.generated.h"

class AHelloCharacter;

UCLASS()
class HELLO_API AHelloPlayerController : public APlayerController
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputMappingContext* DefaultMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* LookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* SwitchToNextAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* SwitchTo1Action;

	//平A攻击IA
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* NormalAttackAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* MainCharacterQAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* MainCharacterZAction;

	//闪避IA
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* DodgeAction;

protected:
	AHelloPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnPossess(APawn* InPawn) override;

public:
	void RequestSwitchToCharacter(AHelloCharacter* NewTarget);

	UFUNCTION(Server, Reliable)
	void ServerSwitchToCharacter(AHelloCharacter* NewTarget);

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Team")
	UTeamConfigDataAsset* TeamConfig;

	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Team")
	TArray<AHelloCharacter*> TeamMembers;

	int32 CurrentMemberIndex = 0;

private:
	void SpawnTeammatesFromConfig();

	void Move(const FInputActionValue& Value);
	void MoveStarted(const FInputActionValue& Value);
	void MoveCompleted(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void Jump();
	void StopJump();
	void OnSwitchNext();
	void OnSwitchTo1();

	// 攻击输入：点按/长按统一处理
	void OnNormalAttackStarted();
	void OnNormalAttackTriggered();
	void OnNormalAttackCompleted();
	void HandleAttackInput();
	void AttackHeldTimerCallback();

	// 长按计时器（控制触发频率）
	FTimerHandle AttackHeldTimerHandle;
	float AttackHoldInterval = 0.15f;
	bool bIsHoldingAttack = false;

	void OnMainCharacterQAction();
	void OnMainCharacterZAction();

	// 闪避输入：点按立即触发（重复按下由 GAS 侧激活状态防重入）
	void OnDodgeStarted();

public:
	/** 最近一次移动输入向量（X=左右，Y=前后），供 GA_Dodge 读取闪避方向。
	 *  电平语义：Triggered 期间每帧更新真实值，Completed（松开）清零；
	 *  不依赖 Started 边沿（PIE 启动时按住/清 flag 后不松手都会丢边沿） */
	UPROPERTY(BlueprintReadOnly, Category = "Input")
	FVector2D LastMoveInput = FVector2D::ZeroVector;

	/** 闪避结束后清空 LastMoveInput（兜底；Completed 已保证松开即清） */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void ClearLastMoveInput() { LastMoveInput = FVector2D::ZeroVector; }
};
