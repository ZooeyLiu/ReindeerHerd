# 演示视频（60–90 秒）录制方案

作业要求：60–90 秒，画面上要有 stat 数据叠加。
`Tools/RunDemo.bat` 会以 1080p 窗口启动游戏，并自动打开 `stat fps` 和 `stat unit`。

## 录之前
1. 插上原装电源，Armoury Crate 切到 Turbo 或 Performance（Fn+F5）。关掉 Chrome、Visual Studio 这类占内存的程序。
2. 输入法切到**英文**（Shift 或 Win+空格）。中文输入法会吃掉 ` 键，控制台就打不开。
3. 双击 `Tools/RunDemo.bat`，等游戏加载完（第一次大约 2 分钟）。
4. 先空跑一遍下面的分镜，熟悉一下。

## 怎么录
- **NVIDIA App**：Alt+Z 打开叠加层 → 录制，或者直接按 **Alt+F9** 开始/停止。
- **Windows Game Bar**：**Win+Alt+R** 开始/停止，视频保存在 `此电脑\视频\Captures`。
- 录 2–3 分钟的原始素材，剪辑时再剪到 80 秒左右。

## 分镜（约 80 秒）

| 时间 | 画面 | 你要做的 | 字幕 / 想说明的点 |
|---|---|---|---|
| 0–6 s | 标题卡 | （剪辑时加） | ReindeerHerd — Niagara at Scale, UE 5.7 |
| 6–22 s | System 1：空中视角，10 万只鹿 | WASD 跑向鹿群，冲进去 | 100,000 GPU sprites · boids with Neighbor Grid 3D · herd flees the dog |
| 22–34 s | 把一部分鹿往东赶进第 1 个圈（右侧圆柱，坐标 10000,0） | 从西边往东推 | Pens read back from the GPU (Export Particle Data) → the counter goes up |
| 34–42 s | 改变数量 | 按 **]**（20 万），停 3 秒，再按 **[** 回到 10 万 | Count via cvar: stat unit shows the frame-time cost |
| 42–60 s | 按 **M** 切到 System 2，2000 只网格鹿 | 按 ` → 输入 `stat Herd` → 回车；然后慢慢靠近鹿群，再冲进去 | CPU meshes · state machine: brown graze / red spooked / yellow follow leader · panic spreads |
| 60–70 s | 优化前后对比 | 按 ` → `herd.SpatialHash 0` → 回车，停 4 秒（看 stat Herd 的毫秒数变大）；再按 ` → `herd.SpatialHash 1` → 回车 | Optimization: spatial hash vs brute force, 2–3× faster |
| 70–82 s | 结尾卡 | （剪辑时加：报告里的两张图截图） | GPU: in budget to 50k · CPU: in budget to 10k · CPU config = better game |

小提示：
- 控制台每输入一条命令按回车后就会关掉，下一条命令要重新按 ` 打开。
- `stat Herd` 的面板会出现在 stat unit 旁边，录之前先确认它不会挡住鹿群。
- 录制本身会占一点 GPU，所以视频里的帧时间可能比报告里的数据高 1–2 ms，这是正常的。

## 剪辑（CapCut）
1. 导入原始素材，剪掉加载画面和多余的部分，控制在 60–90 秒。
2. 加标题卡、结尾卡，每段配一行英文字幕（用上面表格里的字幕）。
3. 导出 1080p、30 或 60 fps 的 MP4，文件名用 `ReindeerHerd_Demo.mp4`。
