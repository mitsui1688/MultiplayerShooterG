// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "BlasterAIController.generated.h"

class ABlasterCharacter;
class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UBehaviorTree;

/**
 * bot 的状态机。
 * 用 C++ 手写状态机而不是 BehaviorTree:全部逻辑集中在这一个文件里,
 * 便于逐行讲解和代码审查,也不用维护一堆 .uasset。
 */
UENUM(BlueprintType)
enum class EBotState : uint8
{
	EBS_Patrol UMETA(DisplayName = "Patrol"),   // 沿巡逻点游走
	EBS_Chase  UMETA(DisplayName = "Chase"),    // 发现目标,接近到射程内
	EBS_Attack UMETA(DisplayName = "Attack"),   // 在射程内,持续开火
	EBS_Dead   UMETA(DisplayName = "Dead")      // 已阵亡,等重生
};

/**
 * 驱动 ABlasterCharacter 的 AI 控制器。
 *
 * 分工:
 *   - 感知:AIPerception 的 Sight 配置负责"看见玩家"。
 *   - 决策:Tick 里的状态机负责 Patrol/Chase/Attack 切换。
 *   - 动作:寻路用 AAIController::MoveToActor,开火用 UCombatComponent::AIFireAt。
 *
 * bot 只在服务器上存在并思考,不需要复制 Controller。
 */
UCLASS()
class BLASTERLEARING_API ABlasterAIController : public AAIController
{
	GENERATED_BODY()

public:
	ABlasterAIController();

	virtual void OnPossess(APawn* InPawn) override;
	virtual void Tick(float DeltaTime) override;

	/** 当前状态,给调试和蓝图看 */
	UPROPERTY(BlueprintReadOnly, Category = "AI")
	EBotState BotState = EBotState::EBS_Patrol;

protected:
	virtual void BeginPlay() override;

	/** 感知更新回调:记住/清除当前目标 */
	UFUNCTION()
	void OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	/*
	* ---- 状态处理 ----
	*/
	void TickPatrol(float DeltaTime);
	void TickChase(float DeltaTime);
	void TickAttack(float DeltaTime);
	void TickDead(float DeltaTime);

	/** 切换状态,顺带处理每个状态的进入/退出动作 */
	void SetBotState(EBotState NewState);

	/** 找一个还没被淘汰的、可见的敌人 */
	AActor* FindVisibleEnemy() const;

	/** 让角色朝目标方向转(寻路时由 MovementComponent 接管朝向,这里用于开火时瞄准) */
	void FaceTarget(AActor* Target, float DeltaTime);

	/** 在 NavMesh 上,以自身为圆心找一个新的随机可达点作为巡逻目标。找不到就返回当前位置。 */
	FVector GetRandomWanderPoint() const;

#if !UE_BUILD_SHIPPING
	/** 调试绘制:头顶状态球、脚下圈、连线,以及屏幕上的状态文字。仅非 Shipping 构建。 */
	void DrawDebugMarkers();
#endif

private:
	/** 拥有者角色(缓存) */
	UPROPERTY()
	ABlasterCharacter* BlasterBot;

	UPROPERTY(VisibleAnywhere, Category = "AI")
	UAIPerceptionComponent* AIPerception;

	UPROPERTY()
	UAISenseConfig_Sight* SightConfig;

	/** 当前锁定的目标(可见时非空) */
	UPROPERTY()
	AActor* CurrentTarget;

	/*
	* ---- 可调参数(NavMesh 放好后在蓝图里调)----
	* 下面几个都是"难度旋钮",要调低 AI 难度主要动它们。
	*/

	/** 视觉半径:bot 多远能看到你。调小 = 更难发现你,可以绕后 */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Perception")
	float SightRadius = 2000.f;

	/** 丢失视觉半径(略大于 SightRadius,给点脱离缓冲) */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Perception")
	float LoseSightRadius = 2400.f;

	/** 视野半角(度)。70 = 只能看到正前方约 140 度。调小 = 背后偷袭更容易 */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Perception")
	float PeripheralVisionAngleDegrees = 70.f;

	/** 进入开火状态的距离 */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Combat")
	float FireRange = 1500.f;

	/** 命中散布(度)。0 = 百发百中;调大(如 5~10)= 明显打不准。这是最直接的难度旋钮 */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Combat")
	float AimSpreadDegrees = 0.f;

	/** 丢失目标后重新搜索的间隔 */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Perception")
	float LoseTargetDelay = 3.f;

	/** 视线检测的高度偏移(瞄躯干而不是脚) */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Combat")
	float AimHeightOffset = 60.f;

	/** 巡逻时在自身周围找随机点的半径(要能被 NavMesh 覆盖) */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Patrol")
	float WanderRadius = 2000.f;

	/** 到达巡逻点后的停留时间(随机游走时的"歇脚") */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Patrol")
	float PatrolWaitTime = 2.f;

	/** 到达巡逻点后到开始下一次游走的计时 */
	float PatrolWaitRemaining = 0.f;

	/** 转到 Chase/Attack 时记录,用于判断是否脱战 */
	float LastTargetSeenTime = 0.f;
};