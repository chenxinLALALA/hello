// Copyright Hello Project. All Rights Reserved.

#include "Animation/AnimNotify_HelloCameraPunch.h"
#include "Character/HelloCharacterBase.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotify_HelloCameraPunch::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
	if (!MeshComp)
	{
		return;
	}

	// 只上膛不震动：真正的冲击在 ApplyDamageToActor 结算到伤害后由角色消费（见 TryConsumeCameraPunch）。
	// 这样时间轴上仍能逐刀配置参数，但挥空/打无敌帧不会再晃镜头。
	if (AHelloCharacterBase* Character = Cast<AHelloCharacterBase>(MeshComp->GetOwner()))
	{
		Character->ArmCameraPunch(FOVDelta, ScreenOffset, Duration, ConfirmWindow);
	}
}
