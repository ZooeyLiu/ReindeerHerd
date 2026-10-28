# Niagara 搭建教程（System 1 GPU 和 System 2 CPU）

HLSL 代码都在 `Docs/HLSL/`，整段复制粘贴就行。
**参数名必须和下面写的完全一样**（大小写也一样），因为 C++ 是按名字找参数的。

---

## 0. 先做一次（编译 C++）

1. 在编辑器里保存所有（Ctrl+Shift+S），然后**关掉编辑器**。
2. 让 Claude 编译，或者双击 `ReindeerHerd.uproject`，弹出 "rebuild modules?" 时选 Yes。
3. 重新打开项目。

---

## 1. System 1：NS_HerdGPU（GPU、Sprite、10 万只以上，三条 boids 规则）

你现在的 `Content/FX/NS_HerdGPU` 已经有：GPU 模拟、Spawn Burst、Sprite 渲染器，
以及 User 参数 SpawnCount / DogPosition / DogRadius / PenPosition / PenRadius / PenIndex。
下面接着做。

### 1.1 补一个 User 参数
在左边 **Parameters** 面板 → **User Exposed** 旁边的 **+** → 类型选 **Object**（找不到就搜 "Object"）→ 命名 `CallbackHandler`。

### 1.2 Emitter Properties（点发射器最上面那一栏）
- **Sim Target**：GPUCompute Sim（已经是了）
- **Local Space**：不勾
- **Calculate Bounds Mode**：Fixed
- **Fixed Bounds**：Min `(-25000, -25000, -1000)`，Max `(25000, 25000, 2000)`
  （GPU 粒子必须用固定包围盒；这也是作业里列出的优化手段之一，报告里可以提）

### 1.3 三个 Emitter 级的 Data Interface
在 **Emitter Spawn** 那一组点 **+** → **Set new or existing parameter directly**，
在弹出的 Set Parameters 模块里点 **+** 加下面三个（命名空间都是 Emitter）：

| 名字 | 类型 | 点开它后要改的属性 |
|---|---|---|
| `Emitter.Grid` | Neighbor Grid 3D | Num Cells `256, 256, 1`；Max Neighbors Per Cell `32`；World BBox Size `40000, 40000, 2000` |
| `Emitter.ParticleReader` | Particle Attribute Reader | Emitter Binding → 选这个发射器自己（SimpleSpriteBurst） |
| `Emitter.Exporter` | Export Particle Data | Callback Handler Parameter → `User.CallbackHandler`；GPU Allocation Mode = Fixed Size；GPU Allocation Fixed Size = `4096` |

> 格子大小 = 40000 / 256 ≈ 156，正好接近 boids 的感知半径 150。每只驯鹿只需要看身边 3×3 个格子，
> 这就是 "Neighbor Grid 把 O(n²) 变成 O(n)"，报告里要解释这一点。

### 1.4 Particle Spawn
- **Spawn Burst Instantaneous** → Spawn Count：点右边小箭头 → Link Inputs → `User.SpawnCount`
- **Initialize Particle**：Lifetime 改成 `100000`（相当于永远不死）；Sprite Size `40`；Color 选一个驯鹿棕色
- **Shape Location**：Box，Box Size `12000, 12000, 0`
- 点 **+** 加 **Add Velocity**：Velocity 点小箭头 → **Random Range Vector**，Min `(-50,-50,0)` Max `(50,50,0)`
- 删掉 Spawn 里跟重力/旋转无关的东西没关系，不删也行

### 1.5 Particle Update
- **删掉**（或取消勾选）：Gravity Force、Drag、**Solve Forces and Velocity**、Scale Color 等。
  位置和速度由我们自己的 HLSL 在模拟阶段里算，不能让 Solve Forces 再算一次。
- 点 **+** 加 **Kill Particles** → 它的 Kill Particles 输入 → Link Inputs → `Particles.Penned`
  （如果列表里还没有 Particles.Penned，先做完 1.7 再回来连）

### 1.6 模拟阶段 1：PopulateGrid（把每只驯鹿放进格子）
1. 发射器堆栈最下面点 **+ Stage**（Add Simulation Stage），改名 `PopulateGrid`。
2. 选中这个 Stage，右边 Details 里 **Iteration Source = Particles**。
3. 在这个 Stage 里点 **+** → **New Scratch Pad Module**，打开它：
   - 右键加 **Map Get**，在它上面点 **+** 加：`Emitter.Grid`、`Particles.Position`、`Engine.Owner.Position`
   - 右键搜 **Custom Hlsl** 加一个节点，把 `Docs/HLSL/GPU_PopulateGrid.hlsl` 整段贴进去
   - 在 Custom Hlsl 节点上加输入引脚（点引脚旁的 **+**，选类型，然后双击改名）：
     `Grid`（Neighbor Grid 3D）、`Position`（Position）、`GridCenter`（Position）
   - 加输出引脚：`InGrid`（bool）
   - 连线：Map Get 的三个输出 → 对应输入引脚
   - 右键加 **Map Set**，点 **+** 加 `Particles.InGrid`（bool），把 InGrid 连上
   - Map Set 的输出连到最右边的 Output Module 节点
   - 点 **Apply**

### 1.7 模拟阶段 2：Boids（三条规则 + 逃离狗 + 进圈计分）
1. 再加一个 Stage，改名 `Boids`，**Iteration Source = Particles**，放在 PopulateGrid **下面**。
2. 同样建一个 Scratch Pad，贴 `Docs/HLSL/GPU_Boids.hlsl`。
3. 输入/输出引脚的名字和类型在 HLSL 文件最上面的注释里写好了，照着加、照着连。
   - 输出 `OutPosition` → Map Set `Particles.Position`
   - 输出 `OutVelocity` → Map Set `Particles.Velocity`
   - 输出 `OutPenned` → Map Set `Particles.Penned`（新建一个 bool 属性）
4. Apply，然后点工具栏 **Compile**。

### 1.8 Sprite 渲染器（让驯鹿"躺"在雪地上、朝着跑的方向）
- Material：`M_ReindeerDot`
- **Alignment**：Velocity Aligned
- **Facing Mode**：Custom Facing Vector，Custom Facing Vector = `(0, 0, 1)`
- Sort Mode：None（10 万个粒子排序很贵）

### 1.9 出错怎么办
- Compile 后点 **Log / Messages** 面板看红字。最常见的是引脚名字或类型没对上，把那行红字发给 Claude。
- 粒子看不到：检查 Fixed Bounds 有没有包住相机；HerdManager 是不是放在平面中间附近。
- 驯鹿不动：检查 Solve Forces and Velocity 是不是关了以外，Add Velocity 有没有加。

---

## 2. System 2：NS_HerdCPU（CPU、Mesh、500–5000 只，状态机）

复杂的行为（吃草→受惊→跟随头鹿、视野锥、绕障碍、恐慌传染、空间哈希）都在 C++ 的
`HerdCpuSim.cpp` 里，每帧把位置/速度/状态通过三个数组传给 Niagara。Niagara 这边只负责
"按编号读数组 → 摆放模型 → 按状态上色"。

### 2.1 新建系统
内容浏览器 `Content/FX` 右键 → Niagara System → **New system from selected emitter(s)** → 选 **Simple Sprite Burst** → 命名 `NS_HerdCPU`。

### 2.2 User 参数（名字必须完全一样）
SpawnCount (int)、DogPosition (Vector)、DogRadius (float)、PenPosition (Vector)、PenRadius (float)、PenIndex (int)，
再加三个数组：

| 名字 | 类型（在 + 里搜） |
|---|---|
| `AgentPositions` | Array Position |
| `AgentVelocities` | Array Vector |
| `AgentStates` | Array Int32 |

### 2.3 Emitter Properties
- **Sim Target**：CPUSim
- Local Space：不勾
- Calculate Bounds Mode：Fixed，范围同上

### 2.4 Particle Spawn
- Spawn Burst Instantaneous → Spawn Count 连 `User.SpawnCount`
- Initialize Particle：Lifetime `100000`；Mesh Scale 先填 `1`
- 删掉 Shape Location（位置由 C++ 给）

### 2.5 Particle Update
- 删掉 Gravity / Drag / Solve Forces and Velocity / Scale Color
- 加一个 Scratch Pad，命名 `ReadAgent`：
  - Map Get 加：`Particles.UniqueID`、`User.AgentPositions`、`User.AgentVelocities`、`User.AgentStates`
  - 从每个数组引脚往外拖 → 搜 **Get** → 得到 Get 节点；Index 都连 `Particles.UniqueID`
  - 加 Custom Hlsl，贴 `Docs/HLSL/CPU_StateToColor.hlsl`；输入 `State`(int) 连 AgentStates 的 Get 输出
  - Map Set 加：`Particles.Position` ← 位置 Get；`Particles.Velocity` ← 速度 Get；
    `Particles.Color` ← OutColor；`Particles.Penned`（bool）← OutPenned
  - Apply
  - 如果 Custom Hlsl 在 CPU 上编译报错：删掉它，改用 **Select** 节点按 State 选颜色，把截图发给 Claude
- 加 **Kill Particles**，输入连 `Particles.Penned`

### 2.6 渲染器
- 删掉 Sprite Renderer，加 **Mesh Renderer**
- Mesh：先用 `/Engine/BasicShapes/Cube`，Initialize Particle 里 Mesh Scale 设成 `(1.2, 0.4, 0.8)` 当占位驯鹿
  （以后可以从 Fab 下一个免费低模驯鹿换上，这是 Tech Art 加分项）
- **Facing Mode**：Velocity（模型 X 轴朝跑的方向）
- 材质要能用粒子颜色：材质里用 **Particle Color** 节点接 Base Color

---

## 3. 放进关卡

1. 打开 `HerdLevel`，选中 HerdManager：
   - Simple System = `NS_HerdGPU`，Complex System = `NS_HerdCPU`
   - Pens：三个圆柱已经放好（按顺序）
   - Aerial Camera：放一个 CameraActor 在高空朝下看（比如高度 15000），拖进来
   - Benchmark Viewpoint：放一个空 Actor 当跑分时的固定视角，拖进来
   - Obstacles：放几块石头 / 圆柱当障碍，拖进数组（CPU 驯鹿会绕开它们）
2. 运行（Alt+P）。按 **M** 切换模式，**[ / ]** 减半/翻倍数量。
3. 控制台（` 键）常用命令：
   - `herd.Mode 0` / `herd.Mode 1`
   - `herd.SimpleCount 200000`、`herd.ComplexCount 5000`
   - `herd.SpatialHash 0`（关掉优化）/ `1`（打开）
   - `stat fps`、`stat unit`、`stat Niagara`、`stat GPU`、`stat Herd`
   - `herd.Sweep`：自动跑一轮扩展实验，CSV 存在 `Saved/Profiling/HerdSweep/`
