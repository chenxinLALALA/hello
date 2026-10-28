// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/GA/HelloGA.h"
#include "Camera/HelloPlayerCameraManager.h"
#include "Character/HelloCharacterBase.h"
#include "GameFramework/PlayerController.h"

AHelloPlayerCameraManager* UHelloGA::GetHelloCameraManager() const
{
	// 优先用本能力的 ActorInfo.PlayerController：多玩家时也能拿到“自己的”相机管理器
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (ActorInfo)
	{
		if (APlayerController* PC = ActorInfo->PlayerController.Get())
		{
			if (AHelloPlayerCameraManager* CameraMgr = Cast<AHelloPlayerCameraManager>(PC->PlayerCameraManager))
			{
				return CameraMgr;
			}
		}
	}

	// 兜底：按 WorldContext 取第一个本地玩家（单机/测试够用）
	return AHelloPlayerCameraManager::GetHelloCameraManager(GetAvatarActorFromActorInfo());
}

int32 UHelloGA::PushSkillCamera(const FHelloCameraModeConfig& Config, float InBlendTime, float InBlendOutTime)
{
	AHelloPlayerCameraManager* CameraMgr = GetHelloCameraManager();
	if (!CameraMgr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[HelloCamera] PushSkillCamera 失败：找不到本地相机管理器"));
		return INDEX_NONE;
	}

	SkillCameraHandle = CameraMgr->PushCameraMode(
		UHelloCameraMode_Anchored::StaticClass(), Config, InBlendTime, InBlendOutTime, /*bTransient=*/true);

	return SkillCameraHandle;
}

void UHelloGA::PopSkillCamera()
{
	if (SkillCameraHandle == INDEX_NONE)
	{
		return;
	}

	if (AHelloPlayerCameraManager* CameraMgr = GetHelloCameraManager())
	{
		CameraMgr->PopCameraMode(SkillCameraHandle);
	}

	SkillCameraHandle = INDEX_NONE;
}

void UHelloGA::TriggerCameraPunch(float FOVDelta, FVector2D ScreenOffset, float Duration)
{
	if (AHelloPlayerCameraManager* CameraMgr = GetHelloCameraManager())
	{
		CameraMgr->AddCameraPunch(FOVDelta, ScreenOffset, Duration);
	}
}

void UHelloGA::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 技能镜头兜底：GA 自己压入的 + 动画通知压入的（NotifyEnd 在打断时可能不触发）
	const bool bHadGACameraMode = (SkillCameraHandle != INDEX_NONE);

	PopSkillCamera();

	bool bHadNotifyCamera = false;
	if (AHelloCharacterBase* Character = Cast<AHelloCharacterBase>(GetAvatarActorFromActorInfo()))
	{
		bHadNotifyCamera = (Character->GetActiveSkillCameraHandle() != INDEX_NONE);
		Character->PopActiveSkillCamera();

		// “命中才生效”的技能镜头若还没等到命中就被打断，清掉待命状态（否则会漏到下一次攻击）
		Character->ClearSkillCameraWindow();
	}

	// 只有本能力/本能力动画确实压过镜头时才全量清理，避免没碰相机的 GA 误清别人的瞬态镜头
	if (bHadGACameraMode || bHadNotifyCamera)
	{
		if (AHelloPlayerCameraManager* CameraMgr = GetHelloCameraManager())
		{
			CameraMgr->PopAllTransientModes();
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
