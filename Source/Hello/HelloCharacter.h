// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/HelloCharacterBase.h"
#include "HelloCharacter.generated.h"

/**
 * 玩家角色。
 * 相机系统已迁移到 PlayerController 层的 AHelloPlayerCameraManager，
 * 本类不再持有 SpringArm/FollowCamera（阶段 1 退役）。
 */
UCLASS(config=Game)
class AHelloCharacter : public AHelloCharacterBase
{
	GENERATED_BODY()

public:
	AHelloCharacter();

protected:
	virtual void BeginPlay() override;
};
