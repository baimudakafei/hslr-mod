# 第三方组件与致谢

本仓库主体以 MIT 许可发布（见 `LICENSE`）。下列组件来自第三方，各自保留原许可与版权。

**作者**：baimudakafei（笔名 CheN.exe）· <https://github.com/baimudakafei/>

本项目与第 2 节提到的早期作品「数字快捷键」出自同一作者，该作品当时署名 **Doomsdayer**。

---

## 1. MinHook

**位置**：`shared/minhook/`

The Minimalistic API Hooking Library for x64/x86。
Copyright (C) 2009-2017 Tsuda Kageyu。**BSD 2-Clause** 许可，全文见
`shared/minhook/LICENSE.txt`。

四个模块的 x64 内联挂钩（`MH_Initialize` / `MH_CreateHook` / `MH_EnableHook`）
全部基于它。源码原样引用，未做修改。

---

## 2. 上游参考实现：「数字快捷键」v2.1.5.1

**位置**：`hotkey/src/menu.h`、`shared/proxy.S`、各模块的 `proxy.def`

`menu.h` 里的「角色行动菜单门禁判定 + 原生点击派发」逻辑，
以及 d3d11 代理 DLL 的转发骨架，沿用作者早期作品 **「数字快捷键」v2.1.5.1**
（当时署名 Doomsdayer，MIT 许可）。

本仓库中的 `menu.h` 与参考实现保持一致，仅删除了本 Mod 用不到的
`menu_context_equal()`（否则会产生 unused-function 编译告警）。
代理骨架的 51 条 d3d11 导出转发表未做改动。

---

## 3. 游戏本体

「幻世錄 Remake」（Steam AppID **4030150**）及其全部资源、
`GameAssembly.dll`、IL2CPP 元数据等版权归 **UserJoy Technology** 及各自权利人所有。

本仓库**不包含**游戏的任何二进制、资源或反编译产物：

- 不含 `GameAssembly.dll`
- 不含 IL2CPP 元数据 (`global-metadata.dat`)
- 不含游戏素材、文本、音频
- `rva_anchors.h` / `fingerprints*.h` 只包含**我们自己**选取的少量机器码字节
  片段（每条 ≤ 1 KB），用于在运行时确认地址正确，不构成对游戏代码的复制。

本项目与游戏官方无任何关联，未获其授权或背书。

---

## 4. 许可兼容性说明

| 组件 | 许可 | 与本项目 MIT 的兼容性 |
|---|---|---|
| MinHook | BSD 2-Clause | 兼容（保留原许可文件即可） |
| 「数字快捷键」菜单逻辑 | MIT | 兼容 |
| 本项目自有代码 | MIT | — |

引用 MinHook 时请保留 `shared/minhook/LICENSE.txt`；
若你要再分发本项目的二进制，请一并附带 `LICENSE` 与 `NOTICE.md`。
