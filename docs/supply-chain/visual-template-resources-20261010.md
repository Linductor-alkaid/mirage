# 几何候选模板资源格式与来源

> 状态：Active（Linux M7-07 本地资源边界）
> 日期：2026-10-10
> 负责人：Mirage 维护者
> 依据：[DEC-061](../decisions/DEC-061-geometric-template-resources.md)

模板在既有审核过的 OCR manifest schema 1 中显式提供；geometry_marks 必须 true。
服务不下载、不发现模板，也不从用户屏幕自动登记身份。每项声明原始 BGRA8 像素文件，
按 B/G/R/A 字节排列、紧密行布局；宽高各 8..256，bytes=width×height×4。
最多 16 项、每项最多 256 KiB、合计最多 4 MiB。ID 唯一且只有 1..64 个 ASCII
字母、数字、点、下划线或连字符。其他格式、字段、摘要/大小差异均拒绝启动。

在已有完整模型清单中增加如下字段；摘要必须替换成文件实际 SHA-256：

```json
{
  "geometry_marks": true,
  "templates": [
    {
      "id": "submit-icon",
      "format": "bgra8",
      "path": "templates/submit-icon.bgra",
      "sha256": "<actual SHA-256>",
      "bytes": 20736,
      "width": 72,
      "height": 72
    }
  ]
}
```

路径沿既有模型清单规则解析，相对路径基于清单目录；可以显式使用绝对路径。读取后的
校验像素由 session 配置拥有，之后不再次打开原文件，因此文件变化不会悄悄改变
运行中模板。整个 manifest canonical digest（包括模板摘要和配置）构成 revision，
修改后重启服务；模板内容摘要证明一致性，资源来源和使用权由维护者审核。

[验收脚本](../../tests/manual/visual_template_acceptance.py)使用项目自行绘制的公开
线条/交叉图标，没有第三方图标包：在公共 Canvas 图像中离线生成 72×72 patch，
写为原始 BGRA8，并记录摘要。模板不是从被操作桌面实时登记。脚本和归档只交付
生成方法及公共样本证据；用户模板、原始屏幕与外部 PP-OCR 权重不入仓。

PP-OCR/ncnn 的版本、资源来源和尚未闭合的权重分发边界沿
[既有资源审计](real-ocr-resources-20261009.md)。本增量没有新增依赖、权重、默认
下载或发布资源。模板匹配不保证跨主题、尺度或自由轮廓定位；无足够几何候选、弱
匹配或歧义时 ID 为空，不能作为可执行引用或业务成功证明。
