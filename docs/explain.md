# 代码导读

这个 runtime 把多个线程的读写请求交给同一个 backend worker。CPU backend
用字节数组存数据；UMD backend 则访问 ttsim 中一个 Wormhole core 的 L1 scratch
区域。两条路径共用队列、future 和关闭逻辑。

## 请求怎样进入队列

[`Runtime::submit`](../src/runtime.cpp) 先检查单次长度与地址范围，再用 mutex
保护 FIFO。成功入队后才分配 sequence，因此执行顺序由接受顺序决定；两个线程
开始调用函数的先后时间不能决定谁先执行。队列满时，producer 在 condition
variable 上等待，worker 取走请求或 runtime 关闭时将它唤醒。

范围检查先判断 `offset > extent`，再判断 `count > extent - offset`，避免
`offset + count` 的整数溢出。单次最多传输 4096 bytes，区域末尾的零字节操作合法。

## 谁执行 I/O，数据活多久

`Runtime::run` 是唯一调用 backend I/O 的线程。它持锁取走一个请求，释放锁后
执行 I/O，让 producer 在读写期间继续排队。容量 Q 表示最多 Q 个 queued 请求
加一个 executing 请求；caller 尚未提交的参数、已得到的结果以及 backend 自己的
内存不计入这个上限。

`write(offset, Bytes)` 将 vector 移入请求，写完后释放。如果传入 vector 预留的
capacity 超过 4096，入队前只复制实际内容并释放原分配，避免一字节请求携带巨大
预留区。请求对象和 allocator 自身的开销不在 payload 上限内。`read` 将独立 vector
通过 promise/future 交给 caller，结果可以比 Runtime 活得更久。长度和 offset
的单位都是字节，payload 用 `std::byte` 表示。

## fence 和并发访问

worker 顺序执行请求，并在 fence 时调用 `Backend::fence`。CPU copy 同步完成，
所以 CPU fence 无需额外操作。固定版本 UMD simulator 的 `l1_membar` 是空实现；
adapter 仍调用该 hook，但模拟器读回不能验证真芯片的 barrier 语义。

fence 不会将多个调用合并成事务。例如，同一区域上若依次接受 `write A`、
`write B`、A 的 fence、A 的 read，最后的 read 就会读到 B。
[`transfer_demo`](../src/demo.cpp) 给两个 producer 各分配独立的 1 KiB 区域，
因此合法的交错不会覆盖对方数据。

## 关闭和错误

`close_and_drain` 在 mutex 下关闭 admission，唤醒等待者，再释放 mutex 后 join。
这样 worker 可以继续拿锁并处理已接受的请求。`call_once` 使同时发生的 close
调用只执行一次 join。

backend 抛出普通 C++ exception 时，runtime 关闭 admission，将相同异常写入
当前和所有 pending 请求的 promise。之前成功的 future 保持成功。之后的有效
提交会抛出终端异常。backend 如果永远不返回，runtime 无法强行取消它；ttsim
进程崩溃或超时由 [`run_umd.py`](../scripts/run_umd.py) 从进程外记录。

## UMD 与窗口分配

[`UmdBackend`](../src/umd_backend.cpp) 创建一个 Cluster，从 descriptor 中选择
TENSIX core，并确认从 `0x1000` 开始的 8 KiB scratch 放得进 L1。demo 先直接
读写，再把同一个 backend 移交 Runtime；worker join 后才销毁 backend。
TT-Metal 是更高层的设备编程与执行设施，本项目只构建 TT-UMD 集成。

上游 `SimulationTlbAllocator` 分配的是有限的 TLB window index。它按窗口大小
寻找能容纳请求的最小可用池，耗尽后尝试更大的池；mutex 保护找空槽与标记，释放
时清除标记供后续复用。Blackhole 的 4 GiB window 是地址映射窗口，并未申请
4 GiB RAM。原算法和锁逻辑来自 Tenstorrent；本项目的
[`allocator_examples`](../examples/allocator_examples.cpp) 调用真实上游实现，
检查释放复用、跨 size class 回退以及非零 BAR4 基址的地址计算。
