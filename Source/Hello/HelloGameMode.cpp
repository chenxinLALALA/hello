// Copyright Epic Games, Inc. All Rights Reserved.

#include "HelloGameMode.h"
#include "PlayerController/HelloPlayerController.h"

AHelloGameMode::AHelloGameMode()
{
	// DefaultPawnClass 在蓝图 GameMode 中设置（DataAsset 的角色 0 类）
	// RestartPlayer 会自动创建角色 0 → Possess → ClientRestart → 摄像机初始化
	PlayerControllerClass = AHelloPlayerController::StaticClass();
}
