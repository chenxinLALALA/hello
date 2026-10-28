// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EnemyHealthBarWidget.generated.h"

class UProgressBar;

/**
 * 敌人头顶血条：纯 C++ 构建（无需 WBP 蓝图资源）
 * 显示 Health / MaxHealth 占比的进度条，由 AHelloEnemy 的属性回调驱动
 */
UCLASS()
class HELLO_API UEnemyHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 更新血条（按 CurrentHealth/MaxHealth 计算百分比） */
	void UpdateHealthBar(float CurrentHealth, float MaxHealth);

protected:
	/** 纯代码构建 Widget 树（引擎构建 UI 的正式入口，早于渲染，优于 NativeConstruct） */
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** 血条进度条（RebuildWidget 中纯代码创建，挂到 WidgetTree 根） */
	UPROPERTY()
	TObjectPtr<UProgressBar> HealthProgressBar = nullptr;
};
