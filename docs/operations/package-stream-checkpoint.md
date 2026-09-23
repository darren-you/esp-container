# product.pkg v1 只读流式验包切片

`esp_container_package_verify()` 接受由调用方提供的只读随机访问回调、精确包长、独立信任的 RSA-3072 PKCS#1 `RSAPublicKey` DER 公钥与对应 `signing_key_id`。组件每次最多读取 512 字节；调用方提供 4096 字节 manifest、384 字节签名和 512 字节读取块工作区，设备不把整包复制到 RAM。失败时结果清零。当前 512 KiB Wasm 编译上限只是原 host 原型的临时拒绝边界，不能当作 4 MiB C3 的已验证包槽预算。

解析器只接受 host `product_package.py` 生成的规范无压缩 ustar：固定三成员与顺序，逐字节固定 header，零填充、两个结束块与精确 10 KiB record 长度。manifest 按规范 JSON 的固定键顺序和类型读取，拒绝重复/未知字段、非规范编码、越界整数、非小写十六进制摘要和不匹配的 key ID。签名覆盖 `ESP-CONTAINER-PRODUCT-V1\0` 与 manifest 精确字节，RSA-PSS 固定 SHA-256、MGF1-SHA-256、32 字节 salt；公钥不从包中读取。Wasm 按块计算 SHA-256，核对清单长度和摘要；整个归档也按流计算 SHA-256，供上层绑定请求。

host 测试由主机打包器生成真实临时签名包，以另一个公钥、错误 PSS salt、已签名畸形清单、路径/类型/长度/扩展字段、填充/尾部损坏、截断、不同读取偏移的故障，以及 64 KiB 载荷交叉验证 C 解析器。测试不使用生产密钥，也不写设备。`tests/package_stream_test.py` 通过 CTest 运行。

固定公开 ESP-IDF fork `855937cf9dcee13ee9c423fb0319238cdc8d53fd` 与公开 lwIP `2758df4cd3666b3b2a5b53830148379326425c0d` 的 C3 样例完成编译。独立临时链接/运行探针在仓外生成 10,240 字节测试签名包、临时 RSA-3072 测试公钥和错误 salt 签名，再临时嵌入样例；官方 Espressif QEMU 9.2.2 的二进制 SHA-256 为 `3e38982c1ea3e750edfc8c910a0fd44727fe07d9c666b84d18d2b7985ac58246`。设备端 PSA 路径回报 `package_pss_positive=0 wrong_salt=2`，分别对应验包通过与不可信签名；同次 WAMR 样例回报 `normal=1 instruction_limit=1`。临时镜像大小为 `0x4f620`、SHA-256 为 `76bea69cbe66970eef1d4fa8f1184b5e6271ac03b13bf275127e2b41f9934704`。独立样例不链接验包入口时镜像为 `0x390d0`；这约 90 KiB 的差值只反映该独立链接探针，不能直接外推五组件增量或正式分区余量。测试包、公钥与入口改动均未进入仓库提交。

此 API 本身不安装或启动业务。QEMU 正向验包读取的是临时镜像内的常量包，不是 Flash 包槽。P6-06 仍需在 P6-03/P7-01 确定的真实候选包槽上实现写入后稳定 Flash 回读，并阻止读回与激活之间的内容变化；还需执行 Wasm 静态扫描、ABI/profile/能力/产品授权/配额检查和状态原子提交。没有这些步骤时，不得把验包返回 `OK` 当作安装授权或业务可运行证据。
