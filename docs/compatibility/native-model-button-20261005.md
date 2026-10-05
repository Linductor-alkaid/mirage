# 输入栏模型按钮：Linux验收

> 日期：2026-10-05；工作项：M6-13；负责人：Mirage维护者。
> 依据：DEC-035/DEC-037既有会话工具栏；本轮只修正私有UI布局。

模型按钮原来按剩余列宽拉伸，800px阅读列中达到458px。现按当前字体的实际字宽加
EUI公开ButtonBuilder的图标、间距及内边距计算，宽96–180px并受剩余空间约束。
长名称按同一字体度量省略，完整ID保留于模型设置；模型标识、切换流程和服务不变。
没有新增并发路径、依赖或第三方修改。

## 执行与证据

- native-debug/native-release构建mirage-native和native_conversation_view_test通过。
- Debug native_chat_model_test通过；原生渲染测试70 checks / 0 failures。
- Release、ASAN的native_chat_model_test/native_conversation_view_test均2/2通过；无ASAN诊断。
- CheckFormat、BoundaryCheck（49 headers / 0 violations）、git diff --check通过。
- 凭据内容扫描无命中；设计sidecar结构及全部非字符串token保持原值。

复跑：

```sh
cmake --build --preset native-debug --target mirage-native native_conversation_view_test -j4
cd build/native-debug/apps/native
../../tests/native_conversation_view_test /tmp/mirage-model-button-20261005
```

自有Xvfb/EUI Runtime/GL framebuffer渲染，不操作用户桌面、不连接模型。
测试使用合成短名GLM-4.6和synthetic/provider/very-long-model-name-20261005；
1180×800/860×620各明暗主题，验证与思考按钮不重叠、模型列表和管理入口仍可打开。
8张新截图保留于[model-button-20261005](../../.impeccable/review/model-button-20261005/)。
单轮批量视觉检查：短名完整、长名省略，图标和文字清晰；按钮收紧、相邻控制位置稳定。

## Inline finish review

按Impeccable降级角色在当前任务内复核，非独立agent评审。

disposition: ship

- persistence：pass；新构建与合成截图覆盖正常/最小明暗窗口。
- fidelity：模型尺寸为用户要求的adaptation；其余工具、顺序、主题、字号为match。
- ceiling：本次局部尺寸修正达到目标，未扩展视觉方向。
- material_fixes：无。
- keep：保留真实模型ID、工具栏顺序、思考/发送右对齐。

## 文档同步

当前任务内执行Impeccable documenter角色；按实际实现更新根目录及apps/native的
DESIGN.md和.impeccable/design.json，并同步前端设计与M6计划。

- palette：沿用既有明暗中性色。
- type ramp：Noto Sans SC与13EM工具栏标签保留。
- named rule：模型按钮以字宽收紧，宽96–180px。
- named rule：超长标签省略，完整ID由管理模型查看。
- named rule：思考与发送维持右对齐。

未将其他预存视觉或文档偏差纳入本次修正。Windows尚未验证：当前环境为Linux；
负责人为维护者，补跑条件为Windows native preset构建及正常/最小明暗窗口交互。
本次未执行UBSAN/TSAN、后端全量回归和在线模型调用；没有相关逻辑变更。
