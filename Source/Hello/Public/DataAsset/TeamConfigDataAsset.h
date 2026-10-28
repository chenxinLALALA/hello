// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "TeamConfigDataAsset.generated.h"

/**
 * 
 */

class AHelloCharacter;

USTRUCT(BlueprintType)
struct FTeamMemberConfig
{
	GENERATED_BODY()

	// 角色蓝图类（用于生成实体）
	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<AHelloCharacter> CharacterClass;

	// 角色名称（UI 显示用）
	UPROPERTY(EditDefaultsOnly)
	FText DisplayName;

	// 生成位置偏移（相对于某个基准点）
	UPROPERTY(EditDefaultsOnly)
	FVector SpawnOffset;

	// 角色专有能力的额外配置（可选，用 GameplayTag 标识）
	UPROPERTY(EditDefaultsOnly)
	FGameplayTagContainer InitialTags;
};

UCLASS(BlueprintType)
class HELLO_API UTeamConfigDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Team")
	TArray<FTeamMemberConfig> TeamMembers;
};
