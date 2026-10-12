// Fill out your copyright notice in the Description page of Project Settings.


#include "BlasterAIController.h"
#include "BlasterLearing/Character/BlasterCharacter.h"
#include "BlasterLearing/BlasterComponent/CombatComponent.h"
#include "BlasterLearing/Weapon/Weapon.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Sight.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"

ABlasterAIController::ABlasterAIController()
{
	// bot 需要每帧跑状态机
	PrimaryActorTick.bCanEverTick = true;

	AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));
	SetPerceptionComponent(*AIPerception);

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = SightRadius;
	SightConfig->LoseSightRadius = LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionAngleDegrees;
	SightConfig->SetMaxAge(5.f);

	// 视觉和听觉都能触发。这里只用视觉,噪声留作扩展。
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	AIPerception->ConfigureSense(*SightConfig);
	AIPerception->SetDominantSense(SightConfig->GetSenseImplementation());
}

void ABlasterAIController::BeginPlay()
{
	Super::BeginPlay();

	// 蓝图里改的 Sight 参数是在 CDO 构造之后才覆盖成员变量的,
	// 而 SightConfig 在 C++ 构造函数里就已经用旧默认值初始化过了。
	// 这里必须重新同步一遍,否则蓝图里调的 SightRadius 等根本不生效。
	if (SightConfig)
	{
		SightConfig->SightRadius = SightRadius;
		SightConfig->LoseSightRadius = LoseSightRadius;
		SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionAngleDegrees;
	}

	if (AIPerception)
	{
		AIPerception->OnTargetPerceptionUpdated.AddDynamic(this, &ABlasterAIController::OnPerceptionUpdated);
	}
}

void ABlasterAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	BlasterBot = Cast<ABlasterCharacter>(InPawn);
	if (BlasterBot == nullptr) return;

	// 关键一步:告诉角色和它的 CombatComponent "我是 bot",
	// 从而跳过所有依赖玩家 HUD / 视口的分支。
	BlasterBot->SetIsAI(true);

	SetBotState(EBotState::EBS_Patrol);
}

void ABlasterAIController::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	ABlasterCharacter* SeenCharacter = Cast<ABlasterCharacter>(Actor);
	if (SeenCharacter == nullptr || SeenCharacter == BlasterBot) return;

	// 自己人 / 已淘汰的不追
	if (SeenCharacter->IsElimed()) return;

	// bot 只打玩家,不把其他 bot 当目标。
	// (bot 没有 PlayerState,GetTeam() 全是 NoTeam,团队判断对 bot 之间不生效,必须显式排除。)
	if (BlasterBot && BlasterBot->IsAI() && SeenCharacter->IsAI()) return;

	if (BlasterBot && SeenCharacter->GetTeam() == BlasterBot->GetTeam() &&
		BlasterBot->GetTeam() != ETeam::ET_NoTeam)
	{
		return;
	}

	if (Stimulus.WasSuccessfullySensed())
	{
		CurrentTarget = SeenCharacter;
		LastTargetSeenTime = GetWorld()->GetTimeSeconds();

		// 巡逻中被打断,进入追击
		if (BotState == EBotState::EBS_Patrol)
		{
			SetBotState(EBotState::EBS_Chase);
		}
	}
	else if (Actor == CurrentTarget)
	{
		// 目标丢失视线,但先不立刻放弃 —— Tick 里用 LoseTargetDelay 判断是否脱战。
		LastTargetSeenTime = GetWorld()->GetTimeSeconds();
	}
}

void ABlasterAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (BlasterBot == nullptr || BlasterBot->IsElimed())
	{
		// 死了(或被销毁)就停手
		if (BotState != EBotState::EBS_Dead)
		{
			SetBotState(EBotState::EBS_Dead);
		}
		return;
	}

	switch (BotState)
	{
	case EBotState::EBS_Patrol:
		TickPatrol(DeltaTime);
		break;
	case EBotState::EBS_Chase:
		TickChase(DeltaTime);
		break;
	case EBotState::EBS_Attack:
		TickAttack(DeltaTime);
		break;
	case EBotState::EBS_Dead:
		TickDead(DeltaTime);
		break;
	}

#if !UE_BUILD_SHIPPING
	DrawDebugMarkers();
#endif
}

#if !UE_BUILD_SHIPPING
void ABlasterAIController::DrawDebugMarkers()
{
	if (BlasterBot == nullptr) return;

	const FVector BotLocation = BlasterBot->GetActorLocation();

	// 颜色按状态区分:巡逻=黄,追击=橙,开火=红,阵亡=灰
	FColor StateColor = FColor::Yellow;
	switch (BotState)
	{
	case EBotState::EBS_Patrol: StateColor = FColor::Yellow; break;
	case EBotState::EBS_Chase:  StateColor = FColor::Orange; break;
	case EBotState::EBS_Attack: StateColor = FColor::Red;    break;
	case EBotState::EBS_Dead:   StateColor = FColor::Silver; break;
	}

	// 头顶上的球:每帧重画,跟着 bot 走。哪怕角色没模型,这个球也看得见。
	DrawDebugSphere(GetWorld(), BotLocation + FVector(0.f, 0.f, 120.f),
		50.f, 12, StateColor, false, -1.f, 0, 2.f);

	// 脚下画个圈,方便在地面上定位
	DrawDebugCircle(GetWorld(), BotLocation + FVector(0.f, 0.f, 5.f),
		80.f, 24, StateColor, false, -1.f, 0, 2.f,
		FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);

	// 有目标就画一条线连过去,一眼看出它在盯谁
	if (CurrentTarget)
	{
		DrawDebugLine(GetWorld(), BotLocation, CurrentTarget->GetActorLocation(),
			StateColor, false, -1.f, 0, 2.f);
	}

	// 屏幕文字:状态 + 有没有目标 + 距目标多远
	// 注意:变量名不要叫 StateName,AController 里已有一个 StateName 成员,会遮蔽。
	static const UEnum* BotStateEnum = StaticEnum<EBotState>();
	const FString BotStateText = BotStateEnum ? BotStateEnum->GetNameStringByValue((int64)BotState) : TEXT("?");
	const FString TargetInfo = CurrentTarget
		? FString::Printf(TEXT("%s (%.0f)"), *CurrentTarget->GetName(),
			FVector::Dist(BotLocation, CurrentTarget->GetActorLocation()))
		: TEXT("无目标");

	if (GEngine)
	{
		// Key 用 int32 显式转一下,避免 uint32 在 int32/uint64 两个重载之间产生歧义
		GEngine->AddOnScreenDebugMessage(
			static_cast<int32>(GetUniqueID()),   // 用 Controller 的 ID 做 key,每个 bot 一行,不会互相覆盖
			0.1f,
			StateColor,
			FString::Printf(TEXT("[Bot %s] State: %s  目标: %s"),
				*BlasterBot->GetName(), *BotStateText, *TargetInfo));
	}
}
#endif

void ABlasterAIController::SetBotState(EBotState NewState)
{
	if (BotState == NewState) return;

	// 离开 Attack 时松开扳机,否则自动武器会一直响
	if (BotState == EBotState::EBS_Attack && BlasterBot && BlasterBot->GetCombat())
	{
		BlasterBot->GetCombat()->FireButtonPressed(false);
	}

	BotState = NewState;
}

AActor* ABlasterAIController::FindVisibleEnemy() const
{
	if (AIPerception == nullptr || BlasterBot == nullptr) return nullptr;

	TArray<AActor*> PerceivedActors;
	AIPerception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), PerceivedActors);

	AActor* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	for (AActor* Actor : PerceivedActors)
	{
		ABlasterCharacter* SeenCharacter = Cast<ABlasterCharacter>(Actor);
		if (SeenCharacter == nullptr || SeenCharacter == BlasterBot || SeenCharacter->IsElimed()) continue;

		// bot 只打玩家,不把其他 bot 当目标。
		if (BlasterBot->IsAI() && SeenCharacter->IsAI()) continue;

		// 跳过同队(夺旗/团队模式下)
		if (SeenCharacter->GetTeam() == BlasterBot->GetTeam() &&
			BlasterBot->GetTeam() != ETeam::ET_NoTeam)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(BlasterBot->GetActorLocation(), SeenCharacter->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = SeenCharacter;
		}
	}
	return Best;
}

void ABlasterAIController::FaceTarget(AActor* Target, float DeltaTime)
{
	if (Target == nullptr || BlasterBot == nullptr) return;

	const FVector ToTarget = Target->GetActorLocation() - BlasterBot->GetActorLocation();
	const FRotator DesiredRotation(0.f, ToTarget.Rotation().Yaw, 0.f);
	const FRotator Smoothed = FMath::RInterpTo(BlasterBot->GetActorRotation(), DesiredRotation, DeltaTime, 8.f);

	BlasterBot->SetActorRotation(Smoothed);
	if (BlasterBot->GetController() == this)
	{
		SetControlRotation(Smoothed);
	}
}

void ABlasterAIController::TickPatrol(float DeltaTime)
{
	// 巡逻期间如果感知到目标,立刻进入追击。
	AActor* Enemy = FindVisibleEnemy();
	if (Enemy)
	{
		CurrentTarget = Enemy;
		LastTargetSeenTime = GetWorld()->GetTimeSeconds();
		SetBotState(EBotState::EBS_Chase);
		return;
	}

	// 随机游走:走到点后歇一会儿,再在 NavMesh 上找下一个点。
	// 如果 NavMesh 没配好,GetRandomWanderPoint 会退回当前位置,
	// bot 就停在原地 —— 这是安全的降级,不会崩。
	if (GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		PatrolWaitRemaining -= DeltaTime;
		if (PatrolWaitRemaining <= 0.f)
		{
			MoveToLocation(GetRandomWanderPoint());
			PatrolWaitRemaining = PatrolWaitTime;
		}
	}
}

FVector ABlasterAIController::GetRandomWanderPoint() const
{
	if (BlasterBot == nullptr) return FVector::ZeroVector;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (NavSys == nullptr) return BlasterBot->GetActorLocation();

	// 在自身周围 WanderRadius 内随机找一个导航网格上的可达点。
	FNavLocation RandomPoint;
	const bool bFound = NavSys->GetRandomReachablePointInRadius(
		BlasterBot->GetActorLocation(),
		WanderRadius,
		RandomPoint);

	return bFound ? RandomPoint.Location : BlasterBot->GetActorLocation();
}

void ABlasterAIController::TickChase(float DeltaTime)
{
	AActor* Enemy = FindVisibleEnemy();
	if (Enemy)
	{
		CurrentTarget = Enemy;
		LastTargetSeenTime = GetWorld()->GetTimeSeconds();
	}
	else if (CurrentTarget == nullptr ||
			 GetWorld()->GetTimeSeconds() - LastTargetSeenTime > LoseTargetDelay)
	{
		// 跟丢了,回去巡逻
		CurrentTarget = nullptr;
		SetBotState(EBotState::EBS_Patrol);
		return;
	}

	if (CurrentTarget == nullptr) return;

	const float Distance = FVector::Dist(BlasterBot->GetActorLocation(), CurrentTarget->GetActorLocation());
	if (Distance <= FireRange)
	{
		SetBotState(EBotState::EBS_Attack);
		return;
	}

	// NavMesh 没放好时 MoveToActor 会失败,失败也不崩,只是站着。
	MoveToActor(CurrentTarget, FireRange * 0.8f);
}

void ABlasterAIController::TickAttack(float DeltaTime)
{
	AActor* Enemy = FindVisibleEnemy();
	if (Enemy)
	{
		CurrentTarget = Enemy;
		LastTargetSeenTime = GetWorld()->GetTimeSeconds();
	}
	else if (CurrentTarget == nullptr ||
			 GetWorld()->GetTimeSeconds() - LastTargetSeenTime > LoseTargetDelay)
	{
		CurrentTarget = nullptr;
		SetBotState(EBotState::EBS_Patrol);
		return;
	}

	if (CurrentTarget == nullptr || BlasterBot == nullptr) return;

	const float Distance = FVector::Dist(BlasterBot->GetActorLocation(), CurrentTarget->GetActorLocation());
	if (Distance > FireRange * 1.1f)
	{
		// 目标跑远了,回到追击
		SetBotState(EBotState::EBS_Chase);
		return;
	}

	// 停下来瞄准,避免边跑边打抖得厉害
	StopMovement();
	FaceTarget(CurrentTarget, DeltaTime);

	// 交给 CombatComponent 做"枪口 -> 目标"的射线并开火。
	// 这条路径绕开了 TraceUnderCrosshairs,不需要视口和 HUD。
	if (UCombatComponent* Combat = BlasterBot->GetCombat())
	{
		Combat->AIFireAt(CurrentTarget, AimSpreadDegrees);
	}
}

void ABlasterAIController::TickDead(float DeltaTime)
{
	// 阵亡处理:停下所有动作。
	// 重生由 GameMode 的定时器负责重新 SpawnActor 一个新 bot。
	StopMovement();
	if (BlasterBot && BlasterBot->GetCombat())
	{
		BlasterBot->GetCombat()->FireButtonPressed(false);
	}
}