# DEC-042：原生服务商编辑与ZCode对照

> 状态：Accepted
> 日期：2026-10-06
> 依据：维护者本轮明确要求；M6-21

ZCode 29628c9的SectionLayout/Navigation/ProviderCardSections作为本轮视觉及信息结构依据：224px服务导航（窄窗口56px）、32px导航/输入、服务标题与操作、Base URL、API格式、API Key、模型列表和添加模型。Mirage保持原生EUI与Mira公开模型网关；不引入TS壳。

目录ModelSettings增加可选provider_id/provider_name元数据，旧文件缺失时按display_name视为单模型服务。新服务使用稳定产品ID，同服务可有多模型；仍限制总目录12条。模型display_name保持独立、短且稳定的目录唯一身份（不拼接供应商模型ID，符合ModelLayer256字节上限），UI分别显示服务名和模型ID。共享连接字段和凭据在服务范围内一致更新。

没有保存配置时展示本地“未命名服务”，不伪造已保存模型。草稿与服务返回的目录分离，保存成功ACK后才更新左侧和live_model；失败保留所有编辑与原应用状态。无模型服务允许保存为停用，添加模型后可启用。服务配置刷新/取消后重建编辑副本。

JSON models显式空数组表示清空目录，缺失表示保持旧目录；本地配置记录这一存在性以保证旧客户端兼容。删除当前服务时停用其模型，不自动开启另一服务。服务目录更新与配置保存事务共同提交。

本轮以已交付Chat Completions/Responses为协议范围。ZCode中的账户套餐/OAuth/Anthropic等依赖能力不伪造；完整协议与连接测试需独立契约和验收。对照结果必须记录差异，不宣称未取证的像素级一致。
