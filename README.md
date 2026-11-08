# CAD-viewer：安卓离线 DWG 看图

本仓库是独立安卓应用。用户可通过系统文件选择器打开本机 DWG，应用只读复制到私有缓存并在本机解析。再次启动时，通过已保存的文件访问权限重新打开最近一张图纸。应用无网络权限。

首版显示模型空间、布局、图层、直线、圆、圆弧、二维多段线、椭圆、单行与多行文字、属性文字、块、标注块、填充和矩形布局视口。支持双指缩放、平移、适配屏幕、图层开关、空间切换与对象信息查看。外部参照、专业对象、测量、编辑和三维显示留待后续版本。

## 构建

安装 JDK 17、Android SDK Platform 36、Android SDK Build Tools、Android NDK `27.3.13750724` 与 CMake `3.31.5`。初始化固定版本的 LibreDWG：

```sh
git submodule update --init third_party/libredwg
./gradlew assembleDebug testDebugUnitTest
./gradlew connectedDebugAndroidTest
```

Windows 使用 `gradlew.bat`。调试 APK 位于 `app/build/outputs/apk/debug/`。原生库构建支持 `arm64-v8a` 与 `x86_64`。

## 验收图纸

仪器测试使用 LibreDWG 上游仓库的六张 `example_*.dwg`，分别覆盖 R2000、R2004、R2007、R2010、R2013 和 R2018。样本位于 `app/src/androidTest/assets/`，来源为子模块 `test/test-data/`。另以子模块的 `Text.dwg`、`HatchG.dwg` 等图纸检查中文文字、标注、块、填充和布局。逐张对照桌面 CAD 的模型空间、图层和布局显示。真机在飞行模式下检查打开、图层切换、重启后最近文件重开和损坏文件提示。

## 许可

本应用按 GNU GPLv3 发布，许可全文见 [LICENSE](LICENSE)。LibreDWG 固定在提交 `2f68d32d456083b645a6808c8a0aa5c94ec1c1f0`，其源码和许可随子模块提供。发布 APK 时应同时提供对应源码、子模块提交信息和 GPLv3 许可。
