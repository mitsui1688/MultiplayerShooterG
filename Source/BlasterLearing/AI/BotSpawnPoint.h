// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BotSpawnPoint.generated.h"

/**
 * bot 的专用出生点。
 *
 * 在关卡里摆若干个这种 Actor,bot 就会精确生成在这些点的位置,
 * 而不再从玩家 PlayerStart 或 NavMesh 随机投射。
 * 编辑器里选中它会显示一个青色箭头,方便辨认和旋转朝向。
 */
UCLASS()
class BLASTERLEARING_API ABotSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	ABotSpawnPoint();
};
