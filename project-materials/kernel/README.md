# 内核源码和构建配置

完整内核源码位于本仓库根目录。已同步的 reviewed #24 源码提交为
`aa18b6dd02a0706ba8c3d79fda1a113db2492294`。

实际构建配置：
`arch/arm64/configs/tb371fc_hyperos_reviewed24.config`。
源码对应说明：根目录 `TB371FC-REVIEWED-24.md`。

已构建并在现有设备上验收的版本标识为 `4.19.325-perf++ #24`。
Image SHA256：
`337d2d42aa1f3c79ac48263e906ffcf92c1feb9dbbde1cc0b9be375642df8bd7`
boot SHA256：
`a0d3a11eb761dbe63d0175dd4ea597a3d26881c8a3ef264ca2b3fe42d886a5a9`

后续编译的版本号、时间戳和二进制哈希可能不同，以上哈希用于标识已验收产物。
本次资料发布没有触发新的内核构建或设备刷写。
