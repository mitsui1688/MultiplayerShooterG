// Fill out your copyright notice in the Description page of Project Settings.


#include "BlasterGameMode.h"
#include "BlasterLearing/Character/BlasterCharacter.h"
#include "BlasterLearing/PlayerController/BlasterPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerStart.h"
#include "BlasterLearing/PlayerState/BlasterPlayerState.h"
#include "BlasterLearing/GameState/BlasterGameState.h"
#include "DrawDebugHelpers.h"
#include "BlasterLearing/AI/BlasterAIController.h"
#include "BlasterLearing/AI/BotSpawnPoint.h"
#include "EngineUtils.h"   // TActorIterator
#include "NavigationSystem.h"

namespace MatchState
{
	const FName Cooldown = FName("Cooldown");
}

ABlasterGameMode::ABlasterGameMode()
{
	bDelayedStart = true;
}

void ABlasterGameMode::BeginPlay()
{
	Super::BeginPlay();

	LevelStartingTime = GetWorld()->GetTimeSeconds();

	// 开局就把 bot 生成到位(不等热身结束)。
	// BotCount 默认 0,所以什么都不配时这里是个 no-op。
	if (HasAuthority() && BotClass && BotCount > 0)
	{
		for (int32 i = 0; i < BotCount; i++)
		{
			SpawnOneBot();
		}
	}
}

void ABlasterGameMode::SpawnOneBot()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BotSpawn] 失败:World 为空"));
		return;
	}
	if (BotClass == nullptr)
	{
		// 最常见的原因:忘了在 GameMode 蓝图的 AI 分类里填 Bot Class
		UE_LOG(LogTemp, Warning, TEXT("[BotSpawn] 失败:BotClass 未配置,请在 GameMode 蓝图里设置 Bot Class"));
		return;
	}

	// 出生点来源,优先级:
	//   1. 关卡里摆的 ABotSpawnPoint(你手动指定位置)
	//   2. 退回:玩家 PlayerStart + 离它一段距离的 NavMesh 投射
	//
	// 第一种让你能精确控制每个 bot 出生在哪;第二种是没摆点时的自动兜底。
	TArray<AActor*> SpawnPoints;
	UGameplayStatics::GetAllActorsOfClass(this, ABotSpawnPoint::StaticClass(), SpawnPoints);
	const bool bUsingManualSpawnPoints = SpawnPoints.Num() > 0;

	if (!bUsingManualSpawnPoints)
	{
		// 没有手动点,退回 PlayerStart 那一套。
		UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), SpawnPoints);
	}

	if (SpawnPoints.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BotSpawn] 失败:地图里既没有 BotSpawnPoint 也没有 PlayerStart"));
		return;
	}

	// 收集已经被"活着的 bot"占住的点。判断依据是 bot 和该点的距离。
	// 手动点时 bot 就生成在点的 Transform 上,所以离得极近的点就是被占的。
	constexpr float OccupiedRadius = 200.f;

	TArray<int32> FreeIndices;
	for (int32 Index = 0; Index < SpawnPoints.Num(); Index++)
	{
		bool bOccupied = false;
		for (TActorIterator<ABlasterCharacter> It(World); It; ++It)
		{
			ABlasterCharacter* ExistingBot = *It;
			if (ExistingBot == nullptr || !ExistingBot->IsAI() || ExistingBot->IsElimed()) continue;

			if (FVector::DistSquared(ExistingBot->GetActorLocation(),
				SpawnPoints[Index]->GetActorLocation()) < FMath::Square(OccupiedRadius))
			{
				bOccupied = true;
				break;
			}
		}
		if (!bOccupied)
		{
			FreeIndices.Add(Index);
		}
	}

	// 空闲点不够时(比如重生时 bot 数已超过出生点数)退回全集,保证一定能生成。
	const int32 Selection = FreeIndices.Num() > 0
		? FreeIndices[FMath::RandRange(0, FreeIndices.Num() - 1)]
		: FMath::RandRange(0, SpawnPoints.Num() - 1);

	FTransform SpawnTransform = SpawnPoints[Selection]->GetActorTransform();

	if (!bUsingManualSpawnPoints)
	{
		// 兜底模式下,让 bot 离玩家 PlayerStart 有一段距离,而不是脸贴脸。
		const FVector PlayerStartLocation = SpawnPoints[Selection]->GetActorLocation();
		SpawnTransform.SetLocation(FindBotSpawnLocation(PlayerStartLocation));
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// controller 传 nullptr,让角色用自己蓝图里配的 AIControllerClass
	// (BP_Bot 里应当设成 ABlasterAIController)。
	// ABlasterAIController::OnPossess 会把角色标记成 AI。
	ABlasterCharacter* Bot = World->SpawnActor<ABlasterCharacter>(BotClass, SpawnTransform, SpawnParams);
	if (Bot == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BotSpawn] SpawnActor 返回空,生成失败"));
		return;
	}

	// 给 bot 一个名字,方便调试和击杀播报里区分。
	Bot->SetIsAI(true);

	// 兜底:确保 bot 一定有 AI Controller。
	// SpawnActor 本身不会 Possess —— 正常应该由 APawn 的 AutoPossessAI 在生成时
	// 调 SpawnDefaultController() 完成。但如果 BP_Bot 上的 AIControllerClass
	// 没配、或 AutoPossessAI 被设成了 Disabled,这里就会是"无 Controller"。
	// 与其依赖蓝图配置,不如在这里明确补一手:只要还没 Controller 就自己造一个。
	if (Bot->GetController() == nullptr)
	{
		if (Bot->AIControllerClass == nullptr)
		{
			// 蓝图没配就退回 C++ 默认值,保证 bot 一定能思考。
			Bot->AIControllerClass = ABlasterAIController::StaticClass();
		}
		Bot->SpawnDefaultController();
	}

#if !UE_BUILD_SHIPPING
	// 调试:把生成位置、用的哪个 PlayerStart、有没有被 AIController 接管都印出来。
	// 一眼就能看出是"没生成" "生成了但没 Controller" 还是 "正常"。
	const FString ControllerInfo = Bot->GetController()
		? Bot->GetController()->GetName()
		: TEXT("无 Controller(检查 BP_Bot 的 AI Controller Class)");

	UE_LOG(LogTemp, Warning, TEXT("[BotSpawn] %s 生成于 %s (出生点: %s), Controller: %s"),
		*Bot->GetName(),
		*SpawnTransform.GetLocation().ToString(),
		*SpawnPoints[Selection]->GetName(),
		*ControllerInfo);

	// 明确报告 AutoPossessAI 和 AIControllerClass 的实际值,方便定位到底哪一环断了。
	UE_LOG(LogTemp, Warning, TEXT("[BotSpawn] 诊断: AutoPossessAI=%d, AIControllerClass=%s"),
		(int32)Bot->AutoPossessAI,
		Bot->AIControllerClass ? *Bot->AIControllerClass->GetName() : TEXT("nullptr"));

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1, 15.f, FColor::Green,
			FString::Printf(TEXT("[BotSpawn] %s @ %s  Controller: %s"),
				*Bot->GetName(),
				*SpawnTransform.GetLocation().ToString(),
				*ControllerInfo));
	}

	// 在生成点画个红圈,3D 视图里一眼定位
	DrawDebugSphere(GetWorld(), SpawnTransform.GetLocation(), 100.f, 12, FColor::Red, false, 15.f);
#endif
}

void ABlasterGameMode::ScheduleBotRespawn()
{
	if (BotClass == nullptr || BotRespawnDelay <= 0.f) return;

	FTimerHandle RespawnTimer;
	GetWorldTimerManager().SetTimer(
		RespawnTimer,
		this,
		&ABlasterGameMode::SpawnOneBot,
		BotRespawnDelay,
		false
	);
}

FVector ABlasterGameMode::FindBotSpawnLocation(const FVector& AwayFrom) const
{
	UWorld* World = GetWorld();
	if (World == nullptr) return AwayFrom;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (NavSys == nullptr) return AwayFrom;

	// 尝试若干次:随机方向 + 随机距离,再投射到导航网格。
	// 找到一个离 AwayFrom 在 [Min, Max] 区间内的可达点就返回。
	constexpr int32 MaxAttempts = 12;
	for (int32 Attempt = 0; Attempt < MaxAttempts; Attempt++)
	{
		const float Angle = FMath::FRandRange(0.f, 2.f * PI);
		const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
		const float Distance = FMath::FRandRange(MinSpawnDistanceFromPlayers, MaxSpawnDistanceFromPlayers);

		const FVector Candidate = AwayFrom + Direction * Distance;

		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(Candidate, Projected, FVector(100.f, 100.f, 200.f)))
		{
			return Projected.Location;
		}
	}

	// 找不到(比如 NavMesh 没覆盖这个范围)就退回参考点本身。
	return AwayFrom;
}

void ABlasterGameMode::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (MatchState == MatchState::WaitingToStart)
	{
		CountdownTime = WarmupTime - GetWorld()->GetTimeSeconds() + LevelStartingTime;
		if (CountdownTime <= 0.f)
		{
			StartMatch();
		}
	}
	else if (MatchState == MatchState::InProgress)
	{
		CountdownTime = WarmupTime + MatchTime - GetWorld()->GetTimeSeconds() + LevelStartingTime;
		if (CountdownTime <= 0.f)
		{
			SetMatchState(MatchState::Cooldown);
		}
	}
	else if (MatchState == MatchState::Cooldown)
	{
		CountdownTime = CooldownTime + WarmupTime + MatchTime - GetWorld()->GetTimeSeconds() + LevelStartingTime;
		if (CountdownTime <= 0.f)
		{
			RestartGame();
		}
	}
}

void ABlasterGameMode::OnMatchStateSet()
{
	Super::OnMatchStateSet();

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ABlasterPlayerController* BlasterPlayer = Cast<ABlasterPlayerController>(*It);
		if (BlasterPlayer)
		{
			BlasterPlayer->OnMatchStateSet(MatchState, bTeamsMatch);
		}
	}
}

float ABlasterGameMode::CalculateDamage(AController* Attacker, AController* Victim, float BaseDamage)
{
	return BaseDamage;
}

void ABlasterGameMode::PlayerEliminated(ABlasterCharacter* ElimmedCharacter, ABlasterPlayerController* VictimController, ABlasterPlayerController* AttackerController)
{
	ABlasterPlayerState* AttackerPlayerState = AttackerController ? Cast<ABlasterPlayerState>(AttackerController->PlayerState) : nullptr;
	ABlasterPlayerState* VictimPlayerState = VictimController ? Cast<ABlasterPlayerState>(VictimController->PlayerState) : nullptr;

	ABlasterGameState* BlasterGameState = GetGameState<ABlasterGameState>();

	if (AttackerPlayerState && AttackerPlayerState != VictimPlayerState && BlasterGameState)
	{
		TArray<ABlasterPlayerState*> PlayersCurrentlyInTheLead;
		for (auto LeadPlayer : BlasterGameState->TopScoringPlayers)
		{
			PlayersCurrentlyInTheLead.Add(LeadPlayer);
		}

		// 击杀 bot 的分数低于击杀真人:bot 更好打,不该和玩家同价。
		const bool bVictimIsBot = ElimmedCharacter && ElimmedCharacter->IsAI();
		AttackerPlayerState->AddToScore(bVictimIsBot ? BotKillScore : 1.f);
		BlasterGameState->UpdateTopScore(AttackerPlayerState);
		if (BlasterGameState->TopScoringPlayers.Contains(AttackerPlayerState))
		{
			ABlasterCharacter* Leader = Cast<ABlasterCharacter>(AttackerPlayerState->GetPawn());
			if (Leader)
			{
				Leader->MulticastGainedTheLead();
			}
		}

		for (INT32 i = 0; i < PlayersCurrentlyInTheLead.Num(); i++)
		{
			if (!BlasterGameState->TopScoringPlayers.Contains(PlayersCurrentlyInTheLead[i]))
			{
				ABlasterCharacter* Loser = Cast<ABlasterCharacter>(PlayersCurrentlyInTheLead[i]->GetPawn());
				if (Loser)
				{
					Loser->MulticastLostTheLead();
				}
			}
		}
	}
	if (VictimPlayerState)
	{
		VictimPlayerState->AddToDefeats(1);
	}

	if (ElimmedCharacter)
	{
		ElimmedCharacter->Elim(false);

		// bot 死了就排一个重生定时器,补上一个新 bot。
		// 真人玩家走 RequestRespawn 那条路,不受影响。
		if (ElimmedCharacter->IsAI())
		{
			ScheduleBotRespawn();
		}
	}

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ABlasterPlayerController* BlasterPlayer = Cast<ABlasterPlayerController>(*It);
		if (BlasterPlayer && AttackerPlayerState && VictimPlayerState)
		{
			BlasterPlayer->BroadcastElim(AttackerPlayerState, VictimPlayerState);
		}
	}
}

void ABlasterGameMode::RequestRespawn(ACharacter* ElimmedCharacter, AController* ElimmedController)
{
	if (ElimmedCharacter)
	{
		ElimmedCharacter->Reset();
		ElimmedCharacter->Destroy();
	}
	if (ElimmedController)
	{
		TArray<AActor*> PlayerStarts;
		UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), PlayerStarts);
		int32 Selection = FMath::RandRange(0, PlayerStarts.Num() - 1);
		RestartPlayerAtPlayerStart(ElimmedController, PlayerStarts[Selection]);
	}
}

void ABlasterGameMode::PlayerLeftGame(ABlasterPlayerState* PlayerLeaving)
{
	//����Elim���������ݲ���bLeftGameΪ��
	if (PlayerLeaving == nullptr) return;
	ABlasterGameState* BlasterGameState = GetGameState<ABlasterGameState>();
	if (BlasterGameState && BlasterGameState->TopScoringPlayers.Contains(PlayerLeaving))
	{
		BlasterGameState->TopScoringPlayers.Remove(PlayerLeaving);
	}
	ABlasterCharacter* CharacterLeaving = Cast<ABlasterCharacter>(PlayerLeaving->GetPawn());
	if (CharacterLeaving)
	{
		CharacterLeaving->Elim(true);
	}
}
