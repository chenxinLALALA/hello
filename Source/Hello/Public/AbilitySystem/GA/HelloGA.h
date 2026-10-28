// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Camera/HelloCameraMode_Anchored.h"
#include "HelloGA.generated.h"

class AHelloPlayerCameraManager;

/**
 * 项目 GA 基类。
 * 提供技能镜头 Push/Pop 与镜头冲击的薄封装，所有技能 GA 照抄这两个接口。
 */
UCLASS()
class HELLO_API UHelloGA : public UGameplayAbility
{
	GENERATED_BODY()

public:
	/**
	 * 压入一个瞬态技能镜头（Anchored），返回 Handle；拿不到本地相机管理器时返回 INDEX_NONE。
	 * BlendTime < 0 时沿用模式类默认值。
	 */
	UFUNCTION(BlueprintCallable, Category = "Hello|Camera")
	int32 PushSkillCamera(const FHelloCameraModeConfig& Config, float InBlendTime = -1.f, float InBlendOutTime = -1.f);

	/** 幂等弹出本 GA 压入的技能镜头（重复调用安全） */
	UFUNCTION(BlueprintCallable, Category = "Hello|Camera")
	void PopSkillCamera();

	/** 触发一次镜头冲击（FOV + 屏幕偏移），多次调用可叠加 */
	UFUNCTION(BlueprintCallable, Category = "Hello|Camera")
	void TriggerCameraPunch(float FOVDelta = -6.f, FVector2D ScreenOffset = FVector2D::ZeroVector, float Duration = 0.2f);

protected:
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	/** 当前本地玩家的相机管理器；非本地控制或无相机管理器时返回 nullptr */
	AHelloPlayerCameraManager* GetHelloCameraManager() const;

	/** 本 GA 压入的技能镜头句柄 */
	int32 SkillCameraHandle = INDEX_NONE;
};
