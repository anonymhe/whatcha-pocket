# WATCHA POCKET

**WATCHA POCKET 开源固件：创造你的观猹世界。**
WATCH IT. POCKET IT. HACK IT.

[简体中文](#简体中文) · [English](#english)

---

## 简体中文

### 这是什么

WATCHA POCKET 是观猹的口袋掌机：一块 240×320 像素屏、三个按键，装着一群小猹。这里是它的全部固件代码。

- **口袋常驻**：首页的 6 个出厂玩法。
- **口袋上新**：持续更新的新玩法。首页选中「猹工坊」卡片按确认进入，在玩法里长按确认返回。
- **身份同步**：首页**长按 ▲** 打开同步页，用微信扫码连接观猹小程序。

6 只小猹，只是开始。下一个玩法，由你来写。

### 不会写代码也能做新玩法

代码小猹 **CHACKER** 住在 WATCHA POCKET 里，负责教它新本领。

1. **下载并解压** 这个仓库（页面上方 Code → Download ZIP）。
2. 用 **Cursor、Trae 等 AI 编程工具** 打开解压后的文件夹。工具要切到能改文件的模式（如 Cursor 的 Agent 模式、Trae 的 Builder 模式）；网页聊天版 AI 读不到你电脑上的文件，用不了。
3. 对它说：**「读 CHACKER.md」**。

CHACKER 会问你几个问题，把点子整理成一张玩法卡，确认后帮你写代码、编译，再带你刷进设备。做好的玩法出现在「口袋上新」里。

带 **#CHACKER** 话题，到观猹猹馆分享你的创作。

### 自己动手写

- 口袋上新开发规范：[`docs/watcha/pocket-guide.zh_CN.md`](docs/watcha/pocket-guide.zh_CN.md)
- 示例玩法：[`main/apps/app_decide.c`](main/apps/app_decide.c)（决策猹）
- 仓库总规则（给 AI 编程助手）：[`AGENTS.zh_CN.md`](AGENTS.zh_CN.md)
- 编译环境：ESP-IDF 5.5.3，见 [`docs/development/engineering/environment-setup.zh_CN.md`](docs/development/engineering/environment-setup.zh_CN.md)

```bash
idf.py build
idf.py -p <端口> flash
```

设备刷完起不来：关机后按住 ▲ 开机，保持 5 秒进入恢复模式，可以重新刷入。

### 授权

- **代码**：MIT 协议，见 [`LICENSE`](LICENSE)。
- **小猹等美术素材**：CC BY-NC 4.0（可转载和二创，需署名，不可商用），见 [`ASSETS_LICENSE.md`](ASSETS_LICENSE.md)。
- 「观猹」「WATCHA」名称与标识归观猹所有，不随以上协议授权。

### 致谢

基于 FoloToy AI Passport 开源固件开发。`docs/` 下的硬件、构建与工程文档大部分来自上游。

---

## English

### What is this

WATCHA POCKET is Watcha's pocket handheld: a 240×320 pixel screen, three buttons and a crew of chas. This repository is its complete firmware.

- **Pocket Classics (口袋常驻)**: the 6 built-in apps on the home screen.
- **Pocket New (口袋上新)**: the continuously updated app list. Select the 猹工坊 card on the home screen and press OK to open it; long-press OK inside an app to return.
- **Profile sync**: **long-press ▲** on the home screen to open the sync screen and scan the QR code with WeChat to connect the Watcha mini-program.

### Build a new app without coding

**CHACKER**, the coding cha, lives in WATCHA POCKET and teaches it new tricks.

1. **Download and unzip** this repository (Code → Download ZIP).
2. Open the folder in an **AI coding tool** that can edit files (e.g. Cursor in Agent mode). Web chat assistants cannot read your local files.
3. Say: **"Read CHACKER.md"**.

CHACKER turns your idea into an app card, writes and builds the code with you, and walks you through flashing. Share your creation with **#CHACKER**.

### Build it yourself

- Pocket New spec: [`docs/watcha/pocket-guide.md`](docs/watcha/pocket-guide.md)
- Example app: [`main/apps/app_decide.c`](main/apps/app_decide.c)
- Repository rules for AI agents: [`AGENTS.md`](AGENTS.md)
- Toolchain: ESP-IDF 5.5.3

If the device does not boot after flashing, power it off, then hold ▲ while powering on for 5 seconds to enter Recovery.

### License

Code is MIT ([`LICENSE`](LICENSE)). Cha artwork is CC BY-NC 4.0 ([`ASSETS_LICENSE.md`](ASSETS_LICENSE.md)). The Watcha / 观猹 names and logos are not licensed.

Based on the open-source FoloToy AI Passport firmware.
