// Copyright Hello Project. All Rights Reserved.

#include "UI/EnemyHealthBarWidget.h"
#include "Components/ProgressBar.h"
#include "Blueprint/WidgetTree.h"

TSharedRef<SWidget> UEnemyHealthBarWidget::RebuildWidget()
{
	// 纯代码构建进度条（不走 WBP），挂为 Widget 根、填满整个血条区域
	if (!HealthProgressBar && WidgetTree)
	{
		HealthProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("HealthProgressBar"));
		WidgetTree->RootWidget = HealthProgressBar;

		// 样式：红色填充（后期美化可换 WBP 或继续补 Style）
		HealthProgressBar->SetFillColorAndOpacity(FLinearColor::Red);
		HealthProgressBar->SetPercent(1.f);
	}

	return Super::RebuildWidget();
}

void UEnemyHealthBarWidget::UpdateHealthBar(float CurrentHealth, float MaxHealth)
{
	if (!HealthProgressBar) return;

	// MaxHealth 非法时按 0% 处理，百分比 clamp 到 [0,1]
	const float Percent = (MaxHealth > KINDA_SMALL_NUMBER)
		? FMath::Clamp(CurrentHealth / MaxHealth, 0.f, 1.f)
		: 0.f;

	HealthProgressBar->SetPercent(Percent);
}
