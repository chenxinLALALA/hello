// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_EnableWeaponTrace.generated.h"

class AHelloCharacterBase;
class UAnimSequenceBase;

/**
 * 动画通知：开启武器碰撞检测
 * 在攻击动画的有效帧调用此通知，激活武器碰撞体以检测敌人
 */
UCLASS(Blueprintable, meta = (DisplayName = "Enable Weapon Trace"))
class HELLO_API UAnimNotify_EnableWeaponTrace : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("EnableWeaponTrace"); }
};
