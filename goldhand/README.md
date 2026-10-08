# goldhand — 金之手强化 + 杀敌金钱倍数（`mod-goldhand.dll`）

**卫星模块**。由伞形 `d3d11.dll` 在启动时 `LoadLibraryW` 进来，
每帧调用它导出的 `gh_frame_tick()`。

```
伞形 d3d11.dll ──LoadLibraryW──► mod-goldhand.dll
               ──每帧调用─────► gh_frame_tick()
               ──焦点变化─────► gh_input_focus(int)
```

## 做什么

| 效果 | 手段 |
|---|---|
| 金之手 / 高级金之手 **命中率 100%** | `DesSkill` 36/38 与 `DesMagic` 3601/3801 的 `HitRatio` 目标值改写 |
| 金之手 **气力只要 1 格** | 同上的 `Expend` 目标值改写（金之手 3→1、高级 2→1） |
| **必定偷到**（除非偷完） | 全表 `DesItem.GetRatio` 钳值 |
| **杀敌金钱 × N**（默认 10） | `StageManager.GetAtkShowDatas` 内的结算写点 inline patch |

## 实现方式：数据钳值，不挂钩

**v1.0.5 起本模块不再使用 MinHook**（唯一的例外是 `trace = 1` 时安装的只读探针）。

做法是：等游戏的配置表加载完成后，**直接改写表里的目标值**。
好处是运行时零挂钩开销、也不会有跳板被别的 Mod 踩掉的问题。

因为游戏读取命中率（`CombatUtils._calcHitRatio`）与消耗检查
（`CheckSkillCost`）走的是同一批对象，所以改写一处，界面提示与实际判定天然一致。

### 命中率为什么要写成 200 而不是 100

这是实测出来的坑，值得记一笔。游戏的命中公式是：

```
if (HitRatio <= 100)  result = clamp(HitRatio + 修正, 20, 100) + 物命 - 闪避
if (HitRatio >  100)  result = (HitRatio - 100) + clamp(...)     + 物命 - 闪避
```

也就是说 `HitRatio = 100` 时**仍然会被对方的 `DodgeRatio` 扣下去**（下限 10），
实战中就是会 miss。而 `HitRatio > 100` 走「溢出补偿」分支，
超出 100 的部分原样保留 —— 于是写 200 之后结果恒 ≥ 100，`_rand(100)` 恒真，
必定命中。

**界面不会显示 200%**：`CombatUtils.GetShowHitRatio` 第一步就把值钳回 100，
所以玩家看到的仍是 100%。

### 「必定偷到」为什么要动 `DesItem.GetRatio`

`DesMagic` 3601/3801 的 `MagicFunc = 50`（偷窃系）且没有任何伤害字段 →
金之手打的其实不是「伤害」，而是走偷窃判定。命中判定里
`desItem[+0x78]`（`DesItem` 字段布局里就是第 20 个实例字段 `GetRatio`）
参与计算，与 `HitRatio` **完全无关** ——
这就是「气力 1 格生效了、攻击也命中了，却还是偷不到」的原因。

所以 `steal_always = 1` 时把全表 `DesItem.GetRatio` 钳到 100。

### 杀敌金钱倍数

写点在 `StageManager.GetAtkShowDatas` 内部的两条互斥
`add [实体+0x8C], r14d` 指令上（同值只累加一次）。实现是 inline patch：
把原指令替换成跳进 stub（`mov edx,[ratio]; imul r14d,edx; 原 add; ret`），
ratio 槽与 stub 在同一块 `VirtualAlloc` 里（保证在 ±2 GB 内可跳）。

**真入账点**是杀敌结算的 `<DoAtkDone>d__490.MoveNext` 里的
`add [StageManager+0xB8], …` —— 那是纯战斗结算路径，
商店买卖 / 任务奖励 / 卡牌小游戏都不经过它。这也是「只影响杀敌金币」的证据。

安全措施：半装回滚（patch 装了一半失败会恢复原字节）+
**字节不符即拒绝**（目标处不是预期字节就不动手）。

## 源码

| 文件 | 作用 |
|---|---|
| `goldhand.c` | 全部实现。顶部有大段注释解释上面每一个机制及其实测依据 |
| `config.h` | ini 解析（`[goldhand]` 段）+ 默认配置生成 |
| `fingerprints.h` | 一致性指纹表 |
| `rva_anchors.h` | 生成物：运行时重定位锚点表 |

## 配置

完整说明见 `../docs/04-配置说明-完整.md` 第 4 节：

```ini
[goldhand]
enable = 1              ; 0 = 整体停用并还原游戏原值
hit_ratio = 100
expend = 1
steal_always = 1
kill_gold_ratio = 10    ; 1 = 原版
```

伞形 `hslr-wait-hotkey.ini` 的 `[features] goldhand = 0` 可整体关掉本模块。

## 编译

```powershell
./build.ps1 -Zig <zig.exe 路径> -OutDir ./dist
```

注意点见 `../docs/05-构建与发布.md`。

## 版本

`GH_VERSION`（`goldhand.c` 顶部）。

## 许可

MIT。
