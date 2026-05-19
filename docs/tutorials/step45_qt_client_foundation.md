# Step 45：Qt 客户端基础工程和资源规范

## 0. 本 Step 结论

- 目标：Step 45 创建可选 Qt Widgets 客户端工程，为后续登录、三栏主窗口和聊天 UI 做基础。
- 构建边界：默认 CMake 不构建 Qt；只有显式传入 `-DLITEIM_BUILD_QT_CLIENT=ON` 才进入 `client_qt/`。
- 主要交付：新增 `client_qt/`、`liteim_qt_client`、最小 `MainWindow`、QSS 资源和图标资源规范。
- 运行边界：本 Step 只启动空窗口，不连接 server，不收发 TLV，不实现登录注册。
- 资源边界：Qt 客户端不得使用 WeChat / Weixin / QQ 等第三方产品品牌资源。

## 1. 为什么需要这个 Step

Step 41-44 已经提供 CLI、E2E、benchmark 和测试硬化。进入 Qt 阶段前，需要先把 GUI 工程边界固定下来：

- Qt 是展示层，不能影响 server、测试和 CLI 的默认构建。
- Qt 代码需要独立目录，避免混进 `server/` 或 `src/` 服务端模块。
- 资源目录和图标规范要提前定清楚，避免后续误用第三方品牌素材。
- 后续 Step 46-52 要逐步加入协议、登录、三栏主窗口、消息气泡和心跳提示，本 Step 先提供可编译的空壳。

## 2. 本 Step 边界

### 本 Step 做

- 根 CMake 增加 `LITEIM_BUILD_QT_CLIENT` 选项。
- 新增 `client_qt/CMakeLists.txt`，当前作为 Qt 客户端顶层入口。
- 新增 `client_qt/src/CMakeLists.txt` 和 `client_qt/tests/CMakeLists.txt`，分别管理 Qt 客户端 target 和 Qt 测试 target。
- 新增 `client_qt/include/liteim_client/ui/MainWindow.hpp`。
- 新增 `client_qt/src/ui/MainWindow.cpp` 和 `client_qt/src/main.cpp`。
- 新增 `client_qt/resources/qss/app.qss` 和 `.qrc` 资源入口。
- 新增 `client_qt/resources/icons/README.md`，说明图标素材规则。
- 新增 CTest 元数据测试，保证 Qt 工程骨架和资源规范存在。

### 本 Step 不做

- 不实现 `PacketCodec`、`FrameDecoder` 或 `QTcpSocket`。
- 不连接 LiteIM server。
- 不实现登录、注册、好友、私聊、群聊、历史、离线消息或心跳。
- 不实现三栏布局、会话列表、消息气泡、未读数或断线提示。
- 不引入 WeChat / Weixin / QQ 等第三方品牌资源。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `CMakeLists.txt` | 修改 | 增加 `LITEIM_BUILD_QT_CLIENT`，按需进入 `client_qt/` |
| `client_qt/CMakeLists.txt` | 新增 | 查找 Qt Widgets，开启 AUTOMOC/AUTORCC，进入 `src/` 和 `tests/` |
| `client_qt/src/CMakeLists.txt` | 新增 | 构建 `liteim_qt_client_core` 和 `liteim_qt_client` |
| `client_qt/tests/CMakeLists.txt` | 新增 | 构建 `liteim_qt_client_tests` 并注册 Qt CTest |
| `client_qt/include/liteim_client/ui/MainWindow.hpp` | 新增 | 声明 Qt 主窗口 |
| `client_qt/src/ui/MainWindow.cpp` | 新增 | 实现空窗口标题、尺寸和 central widget |
| `client_qt/src/main.cpp` | 新增 | 启动 `QApplication`、加载 QSS、显示窗口 |
| `client_qt/resources/liteim_client.qrc` | 新增 | 注册 Qt 资源 |
| `client_qt/resources/qss/app.qss` | 新增 | 放置应用基础样式 |
| `client_qt/resources/icons/README.md` | 新增 | 定义图标素材使用规则 |
| `tests/cmake/qt_client_foundation_test.sh` | 新增 | 检查 Qt 工程骨架和资源规范 |
| `tests/CMakeLists.txt` | 修改 | 注册 `LiteIMCMake.QtClientFoundation` |
| `README.md` / planning 文件 | 更新 | 记录 Qt 可选构建和 Step 45 边界 |

## 4. 核心接口与契约

### `LITEIM_BUILD_QT_CLIENT`

```bash
cmake -S . -B build
cmake --build build

cmake -S . -B build-qt -DLITEIM_BUILD_QT_CLIENT=ON
cmake --build build-qt --target liteim_qt_client
```

契约：

- 默认值是 `OFF`。
- 默认构建不查找 Qt，不影响 server、CLI、bench 和 tests。
- 开启后要求本机存在 Qt Widgets 开发包。
- 本 Step 本地验证使用当前环境中的 Qt5 Widgets。

### `MainWindow`

```cpp
namespace liteim::client {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
};

}
```

契约：

- 只表示 Qt 客户端最外层窗口。
- 当前只设置窗口标题、默认尺寸、最小尺寸和空 `centralWidget`。
- 不持有网络连接，不访问 server，不调用 MySQL / Redis。
- 后续 Step 48 再扩展三栏布局。

### Qt 资源规范

契约：

- QSS 放在 `client_qt/resources/qss/`。
- 图标放在 `client_qt/resources/icons/` 或其直接子目录。
- 不允许使用 WeChat / Weixin / QQ / Telegram / Slack / Discord 等产品 logo、名称、截图或可识别品牌素材。
- 第三方素材必须记录 license、来源、作者和修改信息。

## 5. 运行流程

### 1. 默认服务端构建

```text
cmake -S . -B build
    -> LITEIM_BUILD_QT_CLIENT = OFF
    -> 不进入 client_qt/
    -> server / CLI / bench / tests 正常构建
```

### 2. Qt 客户端构建

```text
cmake -S . -B build-qt -DLITEIM_BUILD_QT_CLIENT=ON
    -> find_package(Qt Widgets)
    -> add_subdirectory(client_qt/src)
    -> add_subdirectory(client_qt/tests)
    -> 编译 main.cpp / ui/MainWindow.cpp / qrc
```

### 3. Qt 客户端启动

```text
main()
    -> QApplication
    -> 从 :/qss/app.qss 加载样式
    -> 创建 MainWindow
    -> show()
    -> QApplication::exec()
```

本 Step 只验证窗口能进入事件循环，不做任何网络连接。

## 6. 关键实现点

### 1. Qt 默认不参与构建

Qt 是展示层，不是服务端主链路依赖。如果默认构建就查找 Qt，CI、server-only 开发和无 GUI 环境都会被污染。因此根 CMake 使用：

```cmake
option(LITEIM_BUILD_QT_CLIENT "Build the optional Qt Widgets demo client" OFF)

if(LITEIM_BUILD_QT_CLIENT)
    add_subdirectory(client_qt)
endif()
```

### 2. Qt 代码放在独立目录

`client_qt/` 只保存 Qt 客户端代码。服务端模块仍在 `include/liteim/` 和 `src/`，不会把 `QWidget`、`QTcpSocket` 或信号槽引入 server。

### 3. 空窗口只建立工程边界

本 Step 的窗口是空壳，价值是证明 Qt 工程、资源系统和可选构建能跑通。后续 UI 复杂度分散到 Step 47-52，不在本 Step 把登录、三栏、聊天全部塞进去。

### 4. 资源规范先于真实素材

先写 icon 规则，再加真实素材。这样后续截图、图标和按钮不会无意使用第三方 IM 产品品牌。

## 7. 测试设计

| 风险 | 测试覆盖 |
| --- | --- |
| 默认构建误依赖 Qt | 默认 `cmake -S . -B build` 和 `cmake --build build --target liteim_tests` |
| Qt option 漏接根 CMake | `LiteIMCMake.QtClientFoundation` 检查 `LITEIM_BUILD_QT_CLIENT` |
| `client_qt` 目录不完整 | `LiteIMCMake.QtClientFoundation` 检查 CMake、header、source、QSS、icon README |
| 资源文件误用第三方品牌名 | `LiteIMCMake.QtClientFoundation` 扫描非 README 资源文件和文件名 |
| Qt target 不能编译 | `cmake --build /tmp/liteim-qt-check --target liteim_qt_client` |
| 空窗口不能启动 | `QT_QPA_PLATFORM=offscreen timeout 2s .../liteim_qt_client || test $? -eq 124` |

## 8. 验证命令

```bash
cmake -S . -B build
ctest --test-dir build -R LiteIMCMake.QtClientFoundation --output-on-failure
cmake --build build --target liteim_server liteim_tests -j2
ctest --test-dir build -R "LiteIMCMake.QtClientFoundation|ConfigTest" --output-on-failure
```

Qt-enabled 构建验证：

```bash
cmake -S . -B /tmp/liteim-qt-check -DLITEIM_BUILD_QT_CLIENT=ON
cmake --build /tmp/liteim-qt-check --target liteim_qt_client -j2
QT_QPA_PLATFORM=offscreen timeout 2s /tmp/liteim-qt-check/client_qt/liteim_qt_client || test $? -eq 124
```

本机 Qt 发现结果：

```text
Qt5Widgets_DIR=/home/yolo/anaconda3/lib/cmake/Qt5Widgets
```

## 9. 面试表达

> Step 45 开始进入 Qt 客户端阶段。我没有让 Qt 影响服务端默认构建，而是通过 `LITEIM_BUILD_QT_CLIENT` 做成可选模块。默认 server、CLI、bench 和 tests 仍然不依赖 Qt；开启选项后才构建 `client_qt/` 下的 Qt Widgets 空窗口。同时我先固定 QSS 和图标资源规范，避免后续 UI 误用第三方 IM 品牌素材。

展开说：

> 这个 Step 只做工程地基，不做网络和业务。Qt 主窗口现在只是一个空 `QMainWindow`，后续 Step 再加 PacketCodec、QTcpSocket、登录注册、三栏布局、消息气泡和心跳提示。这样每一步都能编译验证，也不会把 Qt 的信号槽和 UI 逻辑混进 C++ server。

容易被追问：

> 为什么 Qt 不默认构建？因为很多服务端 CI 或无 GUI 环境没有 Qt 开发包。Qt 是 demo 展示层，不应该成为服务端构建和测试的硬依赖。

## 10. 面试常见追问

### 为什么不直接一次性做完整 Qt 客户端？

完整 Qt 客户端会同时涉及 UI、网络、协议、登录态、会话状态、消息渲染和断线处理。一次性做完风险很高，也不利于测试。Step 45 只先把工程边界和资源规范固定下来。

### 为什么 Qt 客户端不放进 `src/`？

`src/` 是 LiteIM 服务端和公共库实现。Qt 是 demo 客户端展示层，放在 `client_qt/` 能避免服务端 target 误依赖 Qt，也符合 `server/`、`client_cli/`、`bench/` 这种入口分离方式。

### 为什么不用第三方 IM 图标？

项目可以借鉴常见 IM 三栏布局，但不能使用 WeChat / QQ 等品牌图标、名称或素材。这样既避免版权和品牌风险，也能保证 LiteIM 是独立项目。

### 现在空窗口有什么价值？

空窗口证明 Qt 工程、CMake option、资源系统和运行入口已经打通。后续每一步都可以在这个基础上增量加入协议、登录和聊天 UI，而不是边搭工程边写业务。
