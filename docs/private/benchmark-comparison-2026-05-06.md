# 基准测试对比记录（2026-05-06）

## 测试范围

- 只使用 Release 二进制
- 响应内容统一为简单固定响应：`HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!`
- `benchmark2` 当前版本为普通 `async_receive_some` 读循环
- `benchmark_asio` 当前版本已开启 `TCP_NODELAY`
- 本文档汇总当天所有关键 benchmark 结果，不再拆分到其他 Markdown 文档中

## 配置与实现变更时间线

1. 为便于公平对比，两个 benchmark 都统一切换到简单固定响应 `Hello, World!`。
2. `benchmark2` 当前使用普通 `async_receive_some` 读循环。
3. `benchmark_asio` 当前在 `session()` 中显式开启 `TCP_NODELAY`。
4. 在当前配置对齐之后，进行了单点复跑和完整梯度 sweep。

## 阶段结论

1. 当前版本下，`benchmark2` 和 `benchmark_asio` 的配置已经比之前更接近，可以直接做横向对比。
2. `benchmark_asio` 在当前复测里没有出现 socket 读错误，高连接数下稳定性更好。
3. `benchmark2` 在某些连接档位吞吐并不落后，甚至更高；但高连接数尾部行为仍然不如 `benchmark_asio` 稳定。
4. 梯度测试中两边表现会随连接数变化发生交叉，说明问题更偏向高压下的行为一致性，而不是单纯吞吐不足。
5. 单次结果仍有波动，因此结论要同时参考单点复跑和梯度 sweep。

## benchmark2：普通读版本结果

这组结果对应 `benchmark2` 改成普通 `async_receive_some` 之后的复跑。

| 用例 | 每秒请求数 | 传输速率 | socket 读错误 |
| --- | ---: | ---: | ---: |
| t16 c512 d10s | 1632074.58 | 80.94MB/s | 0 |
| t16 c10000 d10s | 602438.64 | 29.88MB/s | 2 |

结论：

1. 改成普通读之后，结构上更接近 `benchmark_asio`。
2. 但这次采样里，`c10000` 下吞吐更低，且仍然出现了 2 次读错误。
3. 这说明当前差异不只是读接口形式本身，还可能和 accept / 调度 / awaiter 路径有关。

## benchmark_asio：TCP_NODELAY 复测结果

这组结果对应 `benchmark_asio` 在 `session()` 中显式开启 `TCP_NODELAY` 之后的复跑。

| 用例 | 每秒请求数 | 传输速率 | socket 读错误 |
| --- | ---: | ---: | ---: |
| t16 c512 d10s | 1429720.00 | 70.90MB/s | 0 |
| t16 c10000 d10s | 670651.74 | 33.26MB/s | 0 |

结论：

1. `TCP_NODELAY` 不是此前差异的主因。
2. 在同样开启 `TCP_NODELAY` 的前提下，`benchmark_asio` 在 `c10000` 下仍保持 0 次读错误。

## 最新梯度测试

参数：

- `wrk -t16 -d10s`
- 连接数：`512 / 1024 / 2048 / 4096 / 8192 / 10000`
- `benchmark2`：普通 `async_receive_some`
- `benchmark_asio`：已开启 `TCP_NODELAY`

| 连接数 | benchmark2 每秒请求数 | benchmark2 传输速率 | benchmark2 错误 | benchmark_asio 每秒请求数 | benchmark_asio 传输速率 | benchmark_asio 错误 |
| ---: | ---: | ---: | --- | ---: | ---: | --- |
| 512 | 1455083.79 | 72.16MB/s | 无 | 1651540.59 | 81.90MB/s | 无 |
| 1024 | 1672182.54 | 82.93MB/s | 无 | 1652375.49 | 81.94MB/s | 无 |
| 2048 | 1518494.88 | 75.30MB/s | 无 | 1176299.47 | 58.33MB/s | 超时 1719 |
| 4096 | 994186.60 | 49.30MB/s | 无 | 1050421.78 | 52.09MB/s | 无 |
| 8192 | 632911.88 | 31.39MB/s | 无 | 695048.24 | 34.47MB/s | 无 |
| 10000 | 736217.28 | 36.51MB/s | 无 | 554614.16 | 27.50MB/s | 无 |

这轮梯度测试的观察：

1. `benchmark2` 在 `c1024`、`c2048`、`c10000` 三档更强。
2. `benchmark_asio` 在 `c512`、`c4096`、`c8192` 三档更强。
3. 最大异常点是 `benchmark_asio` 在 `c2048` 出现超时 1719，导致吞吐明显下降。
4. 这轮梯度测试中，两边都没有报告 socket 读错误。
5. 这说明高连接压测存在明显波动，不能只看某一轮单点结果。

## 当前判断

1. 现在保留下来的结果都基于当前可比配置：`benchmark2` 普通读版本，对比 `benchmark_asio + TCP_NODELAY`。
2. 从当前结果看，`benchmark2` 并不是在所有档位都落后；它在某些连接区间甚至更强。
3. `benchmark_asio` 在高连接数下的连接稳定性仍更好，至少当前复测里没有出现 socket 读错误。
4. 因此当前问题更像是高压下的尾部稳定性和行为一致性，而不是纯吞吐不足。

## 建议的后续方向

1. 固定 `c2048 / c4096 / c10000` 三档，各做 3 轮重复测试，确认梯度测试里的波动是否可复现。
2. 优先对比 accept / 调度模型，而不是继续只盯读接口，因为现在读接口已经更接近了，但行为仍未完全一致。
3. 如果要继续定位连接稳定性问题，优先检查 `receive_awaiter`、socket 关闭时序，以及 `IOContext` 调度路径在高连接数下的差异。
