// Copyright Hello Project. All Rights Reserved.

#include "Animation/AnimNotify_EnableWeaponTrace.h"
#include "Character/HelloCharacterBase.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotify_EnableWeaponTrace::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
	if (AHelloCharacterBase* Character = Cast<AHelloCharacterBase>(MeshComp->GetOwner()))
	{
		Character->EnableWeaponTrace();
	}
}
