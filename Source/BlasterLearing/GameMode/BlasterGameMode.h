// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "BlasterGameMode.generated.h"

namespace MatchState
{
	extern BLASTERLEARING_API const FName Cooldown; //������������ʾӮ�Ҳ���ʼ��ȴ����ʱ
}

/**
 * 
 */
UCLASS()
class BLASTERLEARING_API ABlasterGameMode : public AGameMode
{
	GENERATED_BODY()
	
public:
	ABlasterGameMode();
	virtual void Tick(float DeltaTime) override;
	virtual void PlayerEliminated(class ABlasterCharacter* ElimmedCharacter, class ABlasterPlayerController* VictimController, ABlasterPlayerController* AttackerController);
	virtual void RequestRespawn(ACharacter* ElimmedCharacter, AController* ElimmedController);
	void PlayerLeftGame(class ABlasterPlayerState* PlayerLeaving);
	virtual float CalculateDamage(AController* Attacker, AController* Victim, float BaseDamage);

	UPROPERTY(EditDefaultsOnly)
	float WarmupTime = 10.f;

	UPROPERTY(EditDefaultsOnly)
	float MatchTime = 120.f;

	UPROPERTY(EditDefaultsOnly)
	float CooldownTime = 10.f;

	float LevelStartingTime = 0.f;

	bool bTeamsMatch = false;

	/*
	* ---- AI bot 配置 ----
	* BotCount 默认 0:不配就是原来的纯 PvP 行为,对现有模式零影响。
	*/

	/** 开局生成的 bot 数量 */
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	int32 BotCount = 0;

	/** bot 用的角色蓝图(必须把 AIControllerClass 设成 ABlasterAIController) */
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	TSubclassOf<class ABlasterCharacter> BotClass;

	/** bot 阵亡后多久重生 */
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	float BotRespawnDelay = 30.f;

	/** 击杀一个 bot 得到的分数(低于击杀玩家的 1.0) */
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	float BotKillScore = 0.5f;

	/** bot 出生点离最近 PlayerStart 的最小距离(在 NavMesh 上找点,避免和玩家脸贴脸) */
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	float MinSpawnDistanceFromPlayers = 800.f;

	/** bot 出生点离最近 PlayerStart 的最大距离 */
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	float MaxSpawnDistanceFromPlayers = 2000.f;

protected:
	virtual void BeginPlay() override;
	virtual void OnMatchStateSet() override;

	/** 在随机 PlayerStart 生成一个 bot */
	void SpawnOneBot();

	/** 挂一个 30 秒的定时器,到点再生成一个 bot */
	void ScheduleBotRespawn();

	/** 在离 AwayFrom 一定距离的 NavMesh 上找出生点,找不到就退回 AwayFrom */
	FVector FindBotSpawnLocation(const FVector& AwayFrom) const;

private:
	float CountdownTime = 0.f;

public:
	FORCEINLINE float GetCountdownTime() const { return CountdownTime; }
	FORCEINLINE bool IsTeamsMatch() const { return bTeamsMatch; }
};
