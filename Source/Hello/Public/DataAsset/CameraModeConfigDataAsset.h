// Copyright Hello Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "Camera/HelloCameraMode.h"
#include "CameraModeConfigDataAsset.generated.h"

/**
 * 镜头模式配置资产：Tag → 镜头模式类。
 * 相机管理器用它把角色的 CameraModeTag 解析成具体镜头模式。
 */
USTRUCT()
struct HELLO_API FCameraModeEntry
{
    GENERATED_BODY()

    /** 触发该镜头模式的 GameplayTag（如 Camera.Mode.ThirdPerson） */
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    FGameplayTag CameraTag;

    /** 对应的镜头模式类（TSubclassOf<UHelloCameraMode>） */
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    TSubclassOf<UHelloCameraMode> CameraModeClass;
};

UCLASS()
class HELLO_API UCameraModeConfigDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    /** 根据 Tag 查找镜头模式类；找不到返回 nullptr */
    UFUNCTION(BlueprintCallable, Category = "Hello|Camera")
    TSubclassOf<UHelloCameraMode> FindCameraModeClass(const FGameplayTag& InTag) const;

    /** 列出本资产支持的所有 Tag，调试用 */
    UFUNCTION(BlueprintCallable, Category = "Hello|Camera")
    void GetAllCameraTags(TArray<FGameplayTag>& OutTags) const;

protected:
    /** Tag → 镜头模式类 查找表 */
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    TArray<FCameraModeEntry> CameraModeEntries;
};
