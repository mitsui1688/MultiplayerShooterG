// Fill out your copyright notice in the Description page of Project Settings.


#include "BotSpawnPoint.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"

ABotSpawnPoint::ABotSpawnPoint()
{
	// 出生点不需要每帧 tick
	PrimaryActorTick.bCanEverTick = false;

	// 箭头作根组件:编辑器和 PIE 里都能看到位置与朝向
	UArrowComponent* Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->ArrowColor = FColor::Cyan;
	Arrow->ArrowSize = 0.6f;
	SetRootComponent(Arrow);

	// 再挂一个 billboard,让它在远处也显眼
	UBillboardComponent* Billboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("Billboard"));
	Billboard->SetupAttachment(RootComponent);
}
