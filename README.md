# MultiplayerShooterG — UE5 C++ 多人射击学习项目

> UE 5.8 / C++ / 网络复制与预测学习记录
> 本项目是我跟随 **Stephen Ulibarri《Unreal Engine 5 C++ Multiplayer Shooter》**（Blaster 项目）课程学习后整理的工作副本。**课程主体代码与资产来自原作者，不是我原创；其中的「AI 敌人系统」等模块为我在课程之外的自研扩展**（见下方「自研扩展」一节）。本 README 主要记录我自己的理解、Debug 过程与踩坑总结。

---

## ⚠️ 关于来源与版本（请先看这段）

- **原作者**：Stephen Ulibarri（课程 / Blaster 项目）。上游学习仓库名 `UE5BlasterLearning`。
- **建议版本**：原作者课程基于 **UE 5.0**；本副本运行在 **UE 5.8**。**大版本差异会引入不少 Bug**，如果你要对照学习，建议尽量对齐版本。
- **我的看法**：这个课程本身比较老，照抄复刻意义不大。**真正有价值的是它演示的各种多人网络复制与延迟预测（Server-Side Rewind）的实现思路。** 我没有复刻出他的steam联机功能。

---

## 项目简介

一个可联机的第三人称射击游戏，覆盖了从"单机逻辑"到"多端同步"的完整链路：

- 角色移动、瞄准、跳跃、蹲伏、闪避（RootMotion）
- 多种武器：Hitscan / Projectile / 火箭 / 霰弹枪 / 手雷，含换弹、后坐力、散布
- **服务器端回滚（Server-Side Rewind）** 延迟补偿
- **客户端预测**（弹药数量预测 + 换弹预测，零延迟手感且服务器权威）
- **高延迟自适应降级**（Ping 超阈值自动关闭 SSR，退化为纯服务器权威）
- 游戏模式：混战 / 团队 / 夺旗（Capture the Flag）/ 大厅
- 拾取物（血量 / 护甲 / 弹药 / 速度 / 跳跃）与刷新点
- HUD、击杀公告、队伍计分
- **AI 敌人（bot）系统 —— 自研扩展**（C++ 状态机 / 感知 / 寻路 / 手动出生点 / 重生）
- Steam 会话（`MultiplayerSessions` 插件，测试 AppId 480）

---

## 目录结构

```
MultiplayerShooterG/
├── BlasterLearing.uproject          # 工程文件（名字沿用原作者拼写）
├── Config/                          # 引擎 / 输入 / 游戏配置
├── Content/                         # 蓝图、地图、资产
│   ├── BluePrints/
│   └── Maps/                        # BlasterMap / GameStartupMap / TransitionMap
├── GameAssets/                      # 武器、特效、材质等原始资产
├── Plugins/
│   └── MultiplayerSessions/         # 会话 / 菜单插件
└── Source/BlasterLearing/
    ├── AI/                          # ★ 自研：AI 敌人（AIController 状态机 + 出生点）
    ├── BlasterComponent/            # Combat / Buffer / LagCompensation 组件
    ├── BlasterTypes/                # 枚举与结构（队伍、战斗状态等）
    ├── CaptureTheFlag/              # 夺旗旗区逻辑
    ├── Character/                   # 角色与动画实例
    ├── GameMode/                    # 各游戏模式
    ├── GameState/                   # 全局状态
    ├── HUD/                         # HUD 与控件
    ├── Interfaces/                  # 准星交互接口
    ├── Pickups/                     # 各类拾取物
    ├── PlayerController/            # 控制器
    ├── PlayerStart/                 # 队伍出生点
    ├── PlayerState/                 # 玩家状态（血量、击杀数）
    └── Weapon/                      # 武器体系
```

---

## 核心模块说明

| 模块 | 位置 | 职责 |
|------|------|------|
| `UCombatComponent` | `BlasterComponent/` | 装备、开火、瞄准、换弹，持有当前武器 |
| `UBufferComponent` | `BlasterComponent/` | 战斗状态缓冲（开火前置状态判定） |
| `ULagCompensationComponent` | `BlasterComponent/` | **服务器端回滚核心**：历史快照与命中校验 |
| `AWeapon` 及派生类 | `Weapon/` | `HitScanWeapon` / `ProjectileWeapon` / `Shotgun` / `ProjectileRocket` / `ProjectileGrenade` |
| `ABlasterCharacter` | `Character/` | 角色本体，网络复制属性与 RPC 入口 |
| `ABlasterPlayerController` | `PlayerController/` | 输入、HUD、延迟与时间同步 |
| `ATeamsGameMode` / `ACaptureTheFlagGameMode` | `GameMode/` | 团队与夺旗规则 |
| `APickup` 系列 | `Pickups/` | 血量 / 护甲 / 弹药 / 速度 / 跳跃增益 |
| `ABlasterAIController` ★ | `AI/` | **自研**：AI 敌人状态机（巡逻 / 追击 / 攻击 / 阵亡） |
| `ABotSpawnPoint` ★ | `AI/` | **自研**：关卡内手动摆放的 bot 出生点 |

> ★ = 我在课程之外自研扩展的模块。

---

## 自研扩展：AI 敌人（bot）系统

课程本身**没有 AI 敌人**。这部分是我在原有（只为真人玩家写的）网络架构之上**自己设计并实现**的功能，也是本项目里最有挑战的一环 —— 因为整个代码库处处假设"操控角色的永远是 `PlayerController`"，而 AI 不是。

### 设计

- **手写 C++ 状态机**（不用 BehaviorTree / StateTree）：`EBotState { Patrol, Chase, Attack, Dead }`，在 `ABlasterAIController::Tick` 里分派 `TickPatrol / TickChase / TickAttack / TickDead`。
- **感知**：`UAIPerceptionComponent` + `UAISenseConfig_Sight`（视野半径 / 视锥角 / 失去目标延迟均可配）。
- **寻路**：基于 NavMesh，`MoveToLocation` 在 `WanderRadius` 内随机可达点巡逻；发现目标后 `MoveToActor` 追击、进入 `FireRange` 转为攻击。
- **手动出生点**：`ABotSpawnPoint`（关卡内可手动摆放的 actor），生成时优先使用，没有则回退到 `PlayerStart` 并投射到 NavMesh。
- **重生与计分**：击杀 bot 固定 **30 秒延迟重生**；击杀 bot 只计 0.5 分（低于真人 1.0），**bot 自身不进排行榜 / 计分板**，只作为击杀来源。
- **难度旋钮**（`EditDefaultsOnly`，可在 `BP_BlasterAIController` 调）：`FireRange`、`AimSpreadDegrees`（瞄准散布，最直接的难度/枪法参数）、`SightRadius`、`WanderRadius`、`PatrolWaitTime` 等。

### 关键实现：让 AI 绕开"玩家专属"的代码路径

原项目的开火与 HUD 逻辑强依赖 `PlayerController` / `GameViewport`（例如准星用的 `TraceUnderCrosshairs` 在专用服务器上无视口会空指针）。为此：

- `ABlasterCharacter` 增加 `bIsAI` 标记；`UCombatComponent` 增加 `IsAI()` / `WriteAsAI()`，`TickComponent` 对 bot 直接跳过所有 HUD / 准星分支。
- bot 的瞄准走独立的 `UCombatComponent::AIFireAt(TargetActor, AimSpreadDegrees)` —— 自己从枪口向目标做射线、叠加随机散布，绕开玩家的准星路径。

### 踩过的坑（这一节值得单独看）

给一个"只为真人写的"多人项目加 AI，会集中引爆一批隐藏的架构耦合。我遇到并修复的三处，**必须全修才生效**，只修一处 AI 依然打不出伤害：

1. **AI 攻击完全没有伤害。** 根因是伤害路径写了 `IsLocallyControlled()` 和 `Cast<ABlasterPlayerController>()` 两处玩家专属判断：
   - `ProjectileWeapon::Fire` 用 `IsLocallyControlled()` 区分"权威子弹 / SSR 回滚子弹"。AI 由 `AIController` 驱动，`IsLocallyControlled()` **恒为 false**，于是被误判成"远程客户端"，给它生成了不会结算伤害的回滚子弹。→ 改为 `IsLocallyControlled() || !IsPlayerControlled()`。
   - `ProjectileBullet::OnHit` 把 Owner 的 Controller `Cast` 成 `ABlasterPlayerController`，对 AI（Controller 是 `ABlasterAIController`）返回 **nullptr**，整段 `ApplyDamage` 被跳过。→ 改用 `AController*`，仅在 SSR 回滚分支里再窄化到 `ABlasterPlayerController`。
   - `HitScanWeapon` / `Shotgun` 的 `bCauseAuthDamage` 同理，一并修正。
2. **AI 之间互相攻击。** bot 没有 `PlayerState`，`GetTeam()` 对所有 bot 都返回 `ET_NoTeam`，原有的同队过滤完全失效。→ 在 `FindVisibleEnemy` / `OnPerceptionUpdated` 里显式跳过 AI 目标：**bot 只攻击玩家，bot 之间不互殴，AI 与玩家之间互相有伤害**。
3. **bot 生成后没有 Controller。** `SpawnActor` **不会自动 Possess**。→ 在生成流程里兜底调用 `SpawnDefaultController()` 并设置 `AutoPossessAI`。

> 定位过程：一开始怀疑是射线打空，加了调试日志后发现 `HitActor` 明确就是玩家（几何没问题）、且日志里出现了 `BP_ProjectileBullet_*`（说明 bot 用的是**投射物武器**，不是 HitScan），才把方向锁定到投射物伤害链路上。

---

## 踩坑记录（重点）

以下是我在把这个 **原课程（UE5.0）移植 / 运行到 UE 5.8** 过程中真实遇到的问题。

### 1. 武器弹药在客户端显示不同步

装备武器时，客户端的弹药显示会和服务器不一致。

- **原做法**：装备时用 **多播 RPC（NetMulticast）** 校正弹药——`MulticastAmmo()`，发送给**所有**客户端。
- **问题**：真正需要校正的**只有玩家自己控制的那个客户端**，多播会给其它客户端发无意义的网络包。

```cpp
// 原实现（本副本仍在使用）：多播到所有客户端
UFUNCTION(NetMulticast, Reliable)
void MulticastAmmo(int32 UpdateAmmo);

// 优化方向：改为指定客户端 RPC，只发给武器 Owner
UFUNCTION(Client, Reliable)
void ClientSyncAmmo(int32 UpdateAmmo);
```

- **注意**：改为 `Client` RPC 时，**必须先确认武器 Owner 已设置**，否则服务器不知道发给谁。即在 `SetOwner(Character)` **之后**再调用。

> 📌 状态说明：本工作副本**仍使用 `MulticastAmmo`**，上述 `ClientSyncAmmo` 优化是学习时记录的方向，尚未落地。保留在此作为思路参考。

### 2. 爆头判定"后背也算爆头"

对局中从背后、侧面打人也被判为爆头。

- **根因**：伤害回滚判定时，**先计算头部 HitBox**，并把其它部位的 HitBox 设为无碰撞。结果子弹穿透外层 HitBox，直接命中头部 —— 所以除了正面对枪，从哪个角度打都算爆头。
- **本项目实际实现**：按骨骼名判定 —— `Hit.BoneName == "head"` 即爆头。

```cpp
// ProjectileBullet.cpp —— 本副本的实际写法
const float DamageToCause =
    Hit.BoneName.ToString() == FString("head") ? HeadShotDamage : Damage;
```

- **可选修法**（记录下来，未全部采用）：
  1. 调整头部 HitBox 尺寸，不与其它部位重叠；
  2. 按射击角度先判定其它部位，再判定头部（如下方思路）；
  3. 分开身体 / 头部的射线通道判定。霰弹枪 8 发弹丸若走角度判定就是 8 次循环，代价太高，**不划算**。

```cpp
// 按角度修正的思路（实验性记录，未合入代码）
FVector CharacterForward = HitCharacter->GetActorForwardVector();
FVector TargetDirection   = (CharacterCenter - HitLocation).GetSafeNormal();
float DotProduct = FVector::DotProduct(CharacterForward, TargetDirection);
float Angle      = FMath::RadiansToDegrees(FMath::Acos(DotProduct));
bool bHitFront   = true;
if (Angle <= 60.f) bHitFront = false;   // 判定是否从正面命中
```

> 我的处理：**改了霰弹枪的爆头倍率** —— 收益不高，但近距离一枪击杀的手感基本保留。

### 3. 自定义碰撞通道

`Config/DefaultEngine.ini` 里为本项目新增了两个自定义碰撞通道，理解爆头判定时要留意：

```ini
DefaultChannelResponses=(Channel=ECC_GameTraceChannel1,...,Name="SkeletalMesh")
DefaultChannelResponses=(Channel=ECC_GameTraceChannel2,...,Name="HitBox")
```

### 4. 网络延迟测试

`Config/DefaultEngine.ini` 中的 `[PacketSimulationSettings]` 用于模拟丢包 / 延迟：

```ini
[PacketSimulationSettings]
PktLag = 0     ; 调大此值（如 300）可模拟高延迟，用于验证 SSR 命中
```

---

## 编译与运行

### 环境
- **Unreal Engine 5.8**
- **Visual Studio 2022**（含 "Game development with C++" 工作负载）
- Steam 客户端（会话测试用，`SteamDevAppId=480` 为公用测试 AppId）

### 步骤
1. 右键 `BlasterLearing.uproject` → **Generate Visual Studio project files**
2. 打开生成的 `BlasterLearing.sln`，编译 `BlasterLearingEditor` 目标（Development Editor / Win64）
3. 双击 `.uproject` 打开编辑器
4. 联机测试：编辑器 **Play → Number of Players ≥ 2**，或使用 `MultiplayerSessions` 插件的菜单开服 / 加入

### 关于改名
本工程目录名曾为 `UE5BlasterLearning-main 5.8 - 2`，现改名为 `MultiplayerShooterG`。
**仅目录名改动，内部模块名、`.uproject` 文件名、`Source/` 结构均未改动。**
因此改名后需删除含旧绝对路径的生成物（`.vs` / `Binaries` / `Intermediate` / `DerivedDataCache` / `*.sln` / `*.slnx`）并重新生成 VS 工程文件。

---

## 声明

- 本项目**仅供个人学习**使用。
- **课程主体**的代码、资产、课程内容版权归 **Stephen Ulibarri** 及原仓库作者所有。
- **「AI 敌人系统」等标注 ★ 的模块**为我在课程之外的独立扩展，由我本人设计与实现。
- 本 README 中的理解与 Debug 记录由我本人整理，**不保证完全正确**，如有错误欢迎指出。

---

*文档结束。*
