# 五分钟代码导读

这是对已实现代码的短导读，不是新课程或个人掌握验收。

1. **队列解决什么问题？** 两个 host producer 不直接访问 backend。`Runtime::submit` 用同一把 mutex 保护 FIFO 和接受序号。成功 push 才产生 ticket；跨线程函数开始调用的时间不是执行顺序。`src/demo.cpp` 的两个 producer 各用不同的 1 KiB scratch，避免把 fence 误当成 write/read 事务锁。
2. **谁执行 I/O？** `Runtime::run` 是唯一 owner。它拿锁取一个请求，放锁，然后调用 backend。读写期间 producer 仍可排队，最多 Q 个 queued 加一个 executing；单次最多 4096 bytes。未被接受的调用参数、caller 持有的 future/result、backend 和第三方内存不在这个界限中。
3. **数据活多久？** `write(offset, Bytes)` 接受一个 byte vector，移动到请求；owner 完成写入后释放它。`read` 的独立 vector 通过 promise/future 移给 caller，可以比 Runtime 活得更久。这里的 `std::byte` 与所有长度单位都是字节。
4. **fence 保证什么？** owner 顺序执行，并调用 `Backend::fence`。因此 fence 成功意味着更早接受的请求到达 backend 定义的完成点。CPU copy 同步完成；固定 UMD simulator 的 barrier hook 是空实现。host 顺序成立不能证明真芯片的 memory-barrier 语义。
5. **关闭和错误怎么收敛？** `close_and_drain` 在 mutex 下禁止新接受，唤醒等待者，再释放 mutex 才 join。普通 backend exception 设置终端错误，当前与所有 pending promise 只由各自 request 完成一次。之后的有效提交抛出该错误。后台若永远不返回，析构无法强行取消它；ttsim fatal exit 由外部 runner 检测。
6. **三层关系是什么？** TT-Metal 提供设备程序与更高层执行设施，本项目不依赖完整 Metal。TT-UMD 提供芯片访问 API 和窗口管理；ttsim 提供模拟器 ABI。我们的 FIFO 位于 host，并通过真实 UMD adapter 调用后两层。
7. **window allocator 分配什么？** `SimulationTlbAllocator` 给出有限 TLB window index，按能容纳请求的最小可用 size class 分配，耗尽后向更大 class 回退。mutex 保护可变的分配集合。Blackhole 的 4 GiB window 是地址映射窗口，不是申请 4 GiB RAM。allocator 的原算法属于上游；本项目新增调用示例与解释。

口述待验收：在代码中指出接受的线性化点；画出 close 与正在等待 producer 的交错；解释为什么相同 scratch 上的 write/fence/read 不是跨 producer 事务；修改一个测试的预测再运行。工程测试通过不自动表示这些个人检查通过。
