# ilpd-extract

[English](README.md)｜[简体中文](README.zh.md)

ILPD（Immersive Lens Profile Data，沉浸式镜头特性数据）是 Apple Immersive Video 制作体系中用于描述沉浸相机和镜头成像特性的文件，本质上是以 .ilpd 扩展名存储的 JSON 文件。

ILPD 通常由沉浸相机制造商提供，可能会内嵌在原始摄影机文件中（如 .braw）。提取出来的 ILPD 可用于生成 AIME 文件或 STMap。

## BRAW to ILPD Extractor

命令行工具，用于从 URSA Cine Immersive 拍摄的 Blackmagic RAW 文件中提取 ILPD。目前仅实现了 macOS 版本。

### macOS 依赖项
- macOS 版 Blackmagic RAW SDK，版本 5.0 及以上
- Xcode Command Line Tools 或 Xcode
- CMake 3.10 及以上
- CoreFoundation 框架

### 运行方法

开始前，请确保已下载并安装对应版本的 Blackmagic RAW SDK。下载地址：[Blackmagic Developer Website](https://www.blackmagicdesign.com/developer/products/braw/sdk-and-software)。

以 macOS 为例，SDK 位于 `/Applications/Blackmagic RAW/Blackmagic RAW SDK`。

然后选择以下两种方法之一：
- **使用Release中的预编译版本**
    - macOS：将 `Blackmagic RAW SDK/Mac/Libraries/BlackmagicRawAPI.framework` 放在 `braw2ilpd` 可执行文件旁。

- **自行编译二进制文件**：自定义SDK位置并在`CMakeLists.txt` 中指定其路径。当前 `CMakeLists.txt` 假设 `Blackmagic RAW SDK` 文件夹位于项目根目录。

### 基本用法

```bash
./braw2ilpd <input.braw> <output.ilpd>
```
或使用 `-a` 或 `--all` 选项将所有沉浸属性提取到额外的 .txt 文件：

```bash
./braw2ilpd <input.braw> <output.ilpd> -a
# 或者
./braw2ilpd <input.braw> <output.ilpd> --all
```

### 参数说明

- `<input.braw>`：输入的 Blackmagic RAW 沉浸视频文件路径
- `<output.ilpd>`：提取出的 ILPD 文件保存路径
- `-a, --all`：可选参数，同时生成详细沉浸属性 txt 文件

### 支持的属性

`*_detailed_attributes.txt` 文件包含以下 BRAW 沉浸视频属性：

| 属性 | 说明 |
|-----------|-------------|
| OpticalLensProcessingDataFileUUID | ILPD文件的UUID |
| OpticalILPDFileName | 机内ILPD文件名 |
| OpticalInteraxial | 镜头光轴间距 |
| OpticalProjectionKind | 投影类型（Apple Immersive Video为 'fish'）|
| OpticalCalibrationType | 标定类型（ILPD 镜头投影为 'meiRives'）|
| OpticalProjectionData | 实际ILPD数据内容 |

## 开发

### 1. 克隆/下载项目
```bash
git clone <repository-url>
cd ilpd-extract
```
### 2. 下载 Blackmagic RAW SDK

下载 `Blackmagic RAW SDK`，并确保 `CMakeLists.txt` 中的 `BRAW_SDK_PATH` 指向 SDK 路径。

### 3. 使用 CMake 构建

```bash
mkdir build
cd build
cmake ..
make
```

可执行文件 `braw2ilpd` 会在 build 目录下生成。

## 许可协议

本项目使用 Blackmagic RAW SDK。有关许可信息，请参阅 `Blackmagic RAW SDK/Documents/` 下的 SDK 许可文件。


## 已知问题

- 某些 braw 文件中提取到的 `OpticalInteraxial` 值为 0，可能是相机固件的 bug，或是当前读取方式存在问题。

## 参考资料

- Blackmagic RAW SDK 文档
- [Apple Immersive Media Support/ImmersiveCameraLensDefinition](https://developer.apple.com/documentation/immersivemediasupport/immersivecameralensdefinition)
