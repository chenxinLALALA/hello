// Copyright Hello Project. All Rights Reserved.

#include "Animation/AnimNotify_DisableWeaponTrace.h"
#include "Character/HelloCharacterBase.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotify_DisableWeaponTrace::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
	if (AHelloCharacterBase* Character = Cast<AHelloCharacterBase>(MeshComp->GetOwner()))
	{
		Character->DisableWeaponTrace();
	}
}
