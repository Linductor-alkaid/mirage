# 长会话性能修复 Native Release 与 DEB（2026-10-23）

> 状态：Completed（Linux amd64 构建、测试与包校验）；安装实测待验收
> 负责人：Mirage 维护者
> 工作项：M6-48 / M6-50 / M6-51 / M6-52 交付跟进
> 依据：DEC-037、DEC-068、DEC-073、DEC-108

按维护者要求更新 native-release 与 DEB。源版本 0e7ce89，EUI pin 19acab6；
沿用 0.1.0 amd64 和现有 CPack 安装清单。Release 预设启用 PP-OCR、固定视觉资源、
原生 UI、pet preview 和 PipeWire；DEB 沿用既有四产品进程清单和视觉资源。
最终交付路径 out/packages/mirage_0.1.0_amd64.deb。

完整 Release 构建完成后运行测试，并由同一构建树生成 DEB；解包比较产品
ELF、UI/视觉资源及依赖和控制脚本，校验后替换交付文件并生成相邻 SHA-256。
原始日志和中间产物位于 out/native-package-20261023/。安装/升级/卸载、在线模型、
用户实际会话及 Windows 安装包未在本轮验证，Mirage 维护者取得对应环境后补跑。


## 实际命令与包校验

```bash
cmake --preset native-release -DMIRAGE_ENABLE_PACKAGING=ON
cmake --build --preset native-release -j4
cmake --build --preset native-release --target mirage-native-product -j4
LP_NUM_THREADS=2 ctest --preset native-release -j2
cpack --config build/native-release/CPackConfig.cmake -B /home/linductor/mirage/out/native-package-20261023
python3 out/native-package-20261023/verify_package.py
```

Ubuntu 24.04.5 LTS / x86_64 / GCC 13.3.0，完整构建和产品聚合目标成功。
包大小 58,562,748 字节，SHA-256：
`56bf9be447347dec640cd932a533fff699577db5163d7e921552617c42f7943c`。

解包后四个产品 ELF 与本次 Release 对应程序 SHA-256 一致，ldd 无缺失依赖；
18 项 UI 资源（含 CJK/图标字体、厂商资源与许可证）及 12 项视觉资源与构建源
逐字节匹配。五个 OCR 文件的大小与摘要还与 manifest/resources.lock.json 一致。
postinst/prerm 通过 sh -n，desktop-file-validate 通过，包内 CLI --version 返回
mirage 0.1.0。


## 交付与验证结果

完整 native-release CTest **120/120 通过**，0 skip，151.32s。
校验后原子替换 out/packages/mirage_0.1.0_amd64.deb，并更新相邻 .sha256；
在该目录执行 sha256sum -c mirage_0.1.0_amd64.deb.sha256 通过。原交付包备份至
out/native-package-20261023/previous-b30b6b4c11ae.deb。

构建、测试、CPack 和解包日志保留在 out/native-package-20261023/；
程序/资源摘要与依赖校验结果见[summary.json](data/native-package-refresh-20261023/summary.json)。
本轮未更改产品代码、版本或安装清单，未执行系统安装或重启正在运行的产品。
