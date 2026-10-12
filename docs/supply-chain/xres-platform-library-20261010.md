# XRes 可选系统平台库核对

> 状态：Verified（本机 SDK/动态库与私有 Xvfb；目标发行包另验）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 工作项：M7-09 / [DEC-065](../decisions/DEC-065-x11-drag-helper-ownership.md)

X11 连接资源归属复用系统 libXRes 的公开 XResQueryExtension / XResQueryClients。
它为 Linux Platform Backend 的私有可选依赖，不替代 Mira/Mirador 或 kairo，不
加入 dependencies.lock.json，不复制第三方源码/ABI 声明进仓库。核心 pin 未改。
MIRAGE_ENABLE_X11_DRAG_HELPERS 默认 ON；FindX11 找到头文件与库才编译接入，
缺开发包或显式 OFF 保留严格拒绝。运行期无扩展也拒绝辅助图层，不自动安装。

本机已有 libxres1:amd64 2:1.2.1-1build1，缺开发头文件。本轮从 Ubuntu noble
软件源下载同版本 libxres-dev（8492 字节），只解包到 /tmp/mirage-xres-sdk，
未安装系统软件。下载包 SHA256：
`7e7346f017ad871cd2e3a0e9c8742e943e09198867f042cc455465c39240e2ce`。
SDK 使用包内原始 XRes.h；通过 CMake 缓存指定 X11_XRes_INCLUDE_PATH 与
现有 /lib/x86_64-linux-gnu/libXRes.so.1，不使用手写声明、dlopen 或静态库复制。
四配置复用此 SDK；临时目录移除后需系统开发包或重新指向合法 SDK 才能重建。

包内 copyright 为 XFree86 的宽松许可，保留版权/许可及禁止无授权以其名称
宣传的条款。系统动态依赖无需在仓库引入代码许可证副本。现有 DEB 配置启用
CPACK_DEBIAN_PACKAGE_SHLIBDEPS，动态链接的 XRes 随 dpkg-shlibdeps 检测；
本机 Release service 的 readelf 确认 DT_NEEDED=libXRes.so.1；独立
dpkg-shlibdeps -O 检测到 libxres1 (>= 2:1.2.1)。该检查直接读取构建二进制，
工具提示未放入 package 安装目录；不是完整 DEB 安装/发行版兼容性验证。

平台行为与编译关闭/运行期缺扩展结果见
[原生拖放验收](../compatibility/x11-native-dnd-20261010.md)。
