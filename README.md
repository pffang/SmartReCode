# SmartReCode

使用 C++ 和 WinUI 3 的文本编码检测、预览与转换工具。打开或拖放一个文件后，uchardet 会给出可能的源编码；若预览不正确，可在源编码框中手动输入 libiconv 支持的编码名，或从常用编码列表选择。目标编码同样支持选择或输入。

转换使用原文件的字节和当前源编码，预览框只读；选择目标编码后点击“转换并另存为”。为保护原文件，必须选择**不存在的新文件名**，不会覆盖已有文件。输入不合法、目标编码无法表示字符、文件读取或写入失败时会显示错误，不会静默丢弃字符。Unicode 目标编码可选添加 BOM；默认不添加。源文件的 UTF-8/16/32 BOM 在选用对应标准名称时会自动去除，避免作为正文重复写入。

检测是启发式的，短文本和纯 ASCII 等内容可能有多种合理解释，请以预览为准。文件上限为 16 MiB；包含 NUL 字符的文件不支持预览和转换。程序一次处理一个本地文件。

## 构建

需要 Windows 10/11、Visual Studio C++ 桌面开发工具、Windows SDK、CMake 3.20+、Git、[WinApp CLI](https://github.com/microsoft/WinAppCli) 和联网下载依赖的权限。项目使用 x64 或 ARM64；运行机器需要 Windows App SDK 1.8 运行时。

在项目目录运行：

```powershell
winapp restore
cmake -S . -B build -A x64
cmake --build build --config Debug --target SmartRecoder recoder_tests
ctest --test-dir build -C Debug -R "^recoder_tests$" --output-on-failure
```

使用 Visual Studio 开发者命令提示符及 Ninja 时，可将配置命令改为 `cmake -S . -B build -G Ninja`，并省略 `-A x64`；应从对应架构的开发者环境运行。`winapp restore` 根据 `winapp.yaml` 生成 `.winapp` 并安装所需的 Windows App SDK 运行时。若使用 Ninja，请运行 `build\SmartRecoder.exe`；Visual Studio 多配置生成器的路径为 `build\Debug\SmartRecoder.exe`。

构建使用 CMake 下载 uchardet 0.0.8 和 libiconv-for-Windows 1.19；后者没有 CMake 项目，因此从其源码构建静态库。发布二进制时请遵守依赖的许可要求，包括 libiconv 的 LGPL。

## MSIX 打包

打包需要 Windows SDK 中的 MakeAppx 和 SignTool，并沿用上述编译工具及 `winapp restore`。以下在项目根目录执行，使用 Visual Studio 生成器构建 x64 Release：

```powershell
winapp restore
cmake -S . -B build\msix-x64 -A x64
powershell -NoProfile -ExecutionPolicy Bypass -File .\package-msix.ps1
```

输出为 `build\msix-x64\SmartRecoder-x64.msix`。脚本会构建 Release、识别可执行文件架构、生成图标、声明运行时依赖并打包。**未提供证书时生成的是未签名包，不能直接安装。** 如使用 Ninja，先在对应架构的 Visual Studio 开发者命令行中配置 `cmake -S . -B build\msix-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release`。使用其他构建目录时，通过 `-BuildDirectory` 指定；多配置生成器可用 `-Configuration` 指定非 Debug 配置。

### 签名和安装

1. 准备代码签名证书，将含私钥的证书导入签名电脑的 `Cert:\CurrentUser\My`。证书的 **Subject 必须与** [Package.appxmanifest](./Package.appxmanifest) 中的 `Publisher` 完全相同；当前值 `CN=SmartRecoder Development` 仅用于开发。正式发布前应修改为实际签名者身份，版本升级时也要更新清单中的 `Version`。MSIX 包标识、发布者和版本影响安装及升级。
2. 获取证书指纹，例如 `Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert`，然后运行：

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\package-msix.ps1 -CertificateThumbprint "证书指纹"
   ```

   脚本使用 SignTool 签名并按 Windows 策略验证证书链；验证失败不会替换已有输出。它不会生成证书，也不会修改系统信任。自签名证书仅供测试，必须先由使用者明确决定并配置目标机器的信任；参见微软的[签名指南](https://learn.microsoft.com/en-us/windows/msix/package/signing-package-overview)。正式分发应使用受信任的签名方案，并考虑时间戳；脚本内的签名不添加时间戳。
3. 本包是**依赖框架的 MSIX**，不包含 Windows App SDK Framework 或 VCLibs Desktop 框架包。目标机器须安装与架构匹配、满足清单版本的依赖；侧载时不能假设系统自动下载。Windows App SDK 1.8.6 的安装程序可从[微软下载页面](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/downloads-archive)获取；C++ 桌面框架包及安装方式见 [Microsoft 文档](https://learn.microsoft.com/en-us/troubleshoot/developer/visualstudio/cpp/libraries/c-runtime-packages-desktop-bridge)。普通 VC++ Redistributable 不能代替 MSIX 的 VCLibs Desktop 框架包。
4. 在证书和依赖均已满足后，双击 MSIX 或使用 `Add-AppxPackage .\build\msix-x64\SmartRecoder-x64.msix` 安装。先在干净的目标系统验证安装、打开文件、拖放和转换流程。

ARM64 需使用 ARM64 C++ 工具链、以 `-A ARM64` 配置单独的构建目录，并在脚本中指定该目录；生成的包和依赖也必须匹配目标架构。MSIX 打包并不免除分发静态链接 libiconv 时的 LGPL 义务。

## GitHub Actions 签名发布

[发布工作流](.github/workflows/release.yml)仅通过 `workflow_dispatch` 手动触发，不会因提交或推送自动发布。它在 GitHub 托管的 Windows x64 runner 上恢复依赖、编译、测试、构建 MSIX，提交 SignPath 签名请求；签名通过后验证签名，再创建标签和 GitHub Release。每次发布需在 SignPath 审批，工作流最长等待约 5.5 小时；不会发布未签名包。

### 首次配置

1. 申请 [SignPath Foundation](https://signpath.org/apply) 并完成项目审核。申请前请核对其[开源项目条件](https://signpath.org/terms)，包括开源许可证、已发布的项目、代码签名策略、团队角色及隐私声明；本项目当前尚未配置这些事项，能否获批由 SignPath 决定。
2. 在 SignPath 组织中配置 GitHub.com Trusted Build System、将其关联到项目，按需要安装 [SignPath GitHub App](https://github.com/apps/signpath)，设置签名策略和审批人。创建 Artifact Configuration，内容使用 [signpath-artifact-configuration.xml](./signpath-artifact-configuration.xml)：GitHub 上传的是 ZIP，内部只签名 MSIX，不对其中第三方 DLL 重新签名。SignPath 可能要求额外的元数据约束，应以审核要求为准。
3. 将 [Package.appxmanifest](./Package.appxmanifest) 中的 `Identity Publisher` 改为 SignPath 所用证书的**完整 Subject**。当前的 `CN=SmartRecoder Development` 是本地开发占位值，不能假定与 SignPath 证书匹配。发布后变更 Publisher 会改变 MSIX 身份，影响升级。
4. 在 GitHub 仓库的 **Settings → Secrets and variables → Actions** 中设置：

   | 类型 | 名称 | 内容 |
   | --- | --- | --- |
   | Secret | `SIGNPATH_API_TOKEN` | 有该项目和签名策略提交权限的 SignPath API token |
   | Variable | `SIGNPATH_ORGANIZATION_ID` | SignPath 组织 ID |
   | Variable | `SIGNPATH_PROJECT_SLUG` | 项目标识 |
   | Variable | `SIGNPATH_SIGNING_POLICY_SLUG` | 签名策略标识 |
   | Variable | `SIGNPATH_ARTIFACT_CONFIGURATION_SLUG` | 上述 Artifact Configuration 标识 |
   | Variable | `SIGNPATH_CERTIFICATE_SUBJECT` | 签名证书的完整 Subject，必须与清单 Publisher 一致 |

   不要将 API token、证书私钥或其他凭据提交到仓库。保护发布分支、限制工作流修改和运行权限，并在 SignPath 配置可信构建和审批策略；可参考 [SignPath 的 GitHub 集成文档](https://docs.signpath.io/trusted-build-systems/github)。

### 手动发布

将清单版本更新并提交，例如 `1.0.0.0`；推送包含工作流的提交后，在 GitHub **Actions → Build, sign and release → Run workflow** 选择要发布的分支，填写与清单版本完全相同的标签，如 `v1.0.0.0`。也可使用 GitHub CLI：

```powershell
gh workflow run release.yml --ref main -f tag=v1.0.0.0
```

工作流文件必须位于默认分支才能手动触发；`--ref` 指定构建的分支。签名审批通过后，工作流为该次运行的提交创建**新标签**并发布签名后的 `SmartRecoder-x64.msix`。如果标签已存在，工作流会失败而不会覆盖它；若创建标签后发布失败，标签会保留，需先检查并手动处理后再重试。构建和签名产物在 Actions 中保留 7 天。
