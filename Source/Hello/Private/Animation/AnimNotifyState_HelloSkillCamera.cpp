// Copyright Hello Project. All Rights Reserved.

#include "Animation/AnimNotifyState_HelloSkillCamera.h"
#include "Camera/HelloPlayerCameraManager.h"
#include "Character/HelloCharacterBase.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotifyState_HelloSkillCamera::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	AHelloCharacterBase* Character = Cast<AHelloCharacterBase>(MeshComp ? MeshComp->GetOwner() : nullptr);
	if (!Character)
	{
		return;
	}

	// 只驱动本地玩家的相机
	AHelloPlayerCameraManager* CameraMgr = AHelloPlayerCameraManager::GetForActor(Character);
	if (!CameraMgr)
	{
		return;
	}

	FHelloCameraModeConfig Config = CameraConfig;
	// 存活时长由通知区间控制，禁用 Hold 到期逻辑（否则会与 NotifyEnd 双路径打架）
	Config.HoldDuration = 0.f;

	// 命中才生效：区间开始只上膛，不推镜头；命中由 ApplyDamageToActor 确认后推入
	if (bRequireHit)
	{
		Character->ArmSkillCameraForHit(Config, BlendInTime, BlendOutTime, MinHoldAfterHit);
		return;
	}

	// 同一角色上一个技能镜头先收掉，避免区间重叠时叠加两层
	Character->PopActiveSkillCamera();
	Character->ClearSkillCameraWindow();

	const int32 Handle = CameraMgr->PushCameraMode(
		UHelloCameraMode_Anchored::StaticClass(), Config, BlendInTime, BlendOutTime, /*bTransient=*/true);

	Character->SetActiveSkillCameraHandle(Handle);
}

void UAnimNotifyState_HelloSkillCamera::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	if (AHelloCharacterBase* Character = Cast<AHelloCharacterBase>(MeshComp ? MeshComp->GetOwner() : nullptr))
	{
		if (bRequireHit)
		{
			// 未命中 → 清膛（全程没推过镜头）；已命中 → 回位（可能按 MinHoldAfterHit 延迟）
			Character->EndSkillCameraWindow();
			return;
		}

		// 幂等：GA EndAbility 可能已经提前清理过
		Character->PopActiveSkillCamera();
	}
}
