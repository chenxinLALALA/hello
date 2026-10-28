// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_DisableWeaponTrace.generated.h"

class AHelloCharacterBase;
class UAnimSequenceBase;

/**
 * 动画通知：关闭武器碰撞检测
 * 在攻击动画结束时调用此通知，停用武器碰撞体
 */
UCLASS(Blueprintable, meta = (DisplayName = "Disable Weapon Trace"))
class HELLO_API UAnimNotify_DisableWeaponTrace : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("DisableWeaponTrace"); }
};
