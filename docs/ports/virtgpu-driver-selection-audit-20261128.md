# NaSeed VirtIO-GPU 3D 上游驱动选型审查（2026-11-28）

> **审查结论（截至本次查询）**：原版 Linux 6.14 VirtIO-GPU 不是唯一驱动；此前未完成跨系统全路径评审便持续编写 Linux 内核适配，违反“先审源代码、验证实际 3D 能力和依赖、再选型”的工程原则。暂停新增兼容层代码，继续保留原生 2D 路径及 PR #155 Draft，不宣称原版 3D 驱动已经集成。
>
> **证据级别区分**：源码核读 / 项目作者自报实际设备验收 / 官方手册功能界定 / 未见真实 3D 证据。不得将作者报告当作 NaSeed 自己的 CI。

## 需求与判定边界

- 目标：复用他人**已验证过的**虚拟 GPU 3D 实现，而不是重写 GPU 3D 命令和伪造驱动；x86-64 / RV64 / AArch64；保持 NaSeed 原来的 VirtIO 2D 正确性。
- 必须区别 **GPU 2D 显示**、**VirtIO VIRGL/Capset 功能探测**、**真正实现 Context / Resource / EXECBUFFER / Fence**、**未经修改的 Alpine Mesa 真正渲染/readback**。
- 用户态兼容性是独立评审项。Linux DRM render node 与 Linux virtgpu ioctl 对未经修改的 Alpine Mesa 兼容；Haiku 的自定义 render node / Fence fd 和 Windows WDDM 不能仅靠 kernel C 文件移植达到同样效果。
- 源码许可证不仅看 port wrapper：若组合使用 Linux DRM/GEM shmem 等 GPL-2.0 源码，整个分发方案需要独立许可证合规评估；未明确前 PR 不合入主分支。

## 已核实候选矩阵

| 候选 | 真正的已公开 3D 验收证据 | 上游驱动是否独立于 Linux DRM | 对 NaSeed 重要依赖/限制 | 当前评估 |
|---|---|---|---|---|
| **Linux v6.14 VirtIO-GPU DRM** | Linux + Mesa VirGL 是成熟栈；但 NaSeed **没有**原版对象编译/链接和 Mesa EXECBUFFER | 不适用：Linux 原生实现 | Linux DRM/GEM shmem/dma_resv/fences/syncobj, VMA, workqueue, virtio, uAPI, 许可证 | 成熟但跨内核移植代价较大；不应以持续编写零散 Linux shim 代替导入真实 DRM 设施 |
| **FreeBSD drm-kmod PR #517** | 作者报告 UTM arm64 上的 sway GLES2、glmark2、EXECBUFFER 压测，amd64 有其他用户核实；**PR 尚未合并** | **否**：保留 Linux v6.14 驱动源码，依托 LinuxKPI + FreeBSD DRM | FreeBSD DRM、LinuxKPI、内核 VirtIO/virtqueue、vm/page APIs；PR 中 `linuxkpi_virtio.c` 本身 484 行不代表全部依赖只有 484 行 | 最好的完整 3D/DRM 验收参考；不能称作比 Linux 更小的独立驱动 |
| **Haiku 原生 VirtIO-GPU + 第三方 3D 补丁** | 2026 年第三方项目 README 报告 UTM AArch64 Venus/Vulkan、WebGL 2/Zink、读回；是作者报告，**不是** NaSeed 验证或 Haiku 正式合并 | **是**：直接扩展 Haiku 自有 VirtIO-GPU C++ 驱动 | 3D 补丁约 **1,695 新增/117 删除，3 文件**，自定义 render node、buffer/blob、Fence、Haiku B_* VM/线程/IOCTL；用户态需 Mesa Venus 26.2.5 的 Haiku 专用补丁，QEMU `blob/hostmem/venus`；仅见其 AArch64 UTM 验收 | **优先做深入可移植性审查的非 Linux 候选**。不能直接替换 Linux DRM ABI，亦没有证实其 VirGL GL 以及三 ISA |
| **Windows/UTM viogpu3d WDDM** | UTM/项目作者报告真实 D3D11 device creation、Windows 3D；`viogpudo` 是另外一套 2D 驱动 | 是，Windows WDDM 驱动 + Mesa Neptune / Venus 用户态 | Windows dxgkrnl/WDDM、WDK、内核对象和 Direct3D UMD；和 Alpine Mesa/Linux DRM 不兼容 | 3D 设计参考，非 NaSeed 即插即用的移植基础 |
| **NetBSD / OpenBSD viogpu** | NetBSD 正式 `viogpu(4)` 手册**明确否定 3D**，用于 wscons console；源自 OpenBSD 7.4 | 是 | 2D console / 虚拟 GPU 基础 | 只作 2D/设备发现参考，不能取代 3D 驱动 |
| **SerenityOS VirtIO GPU** | 官方 graphics 文档列其为显示设备，但此次**没有核实到真实 VirGL 3D + Mesa 渲染证据** | 是 | SerenityOS 自有显示/VMObject APIs | 不按 3D 候选推进；只作为可能的 2D 架构参考 |
| **Redox OS virtio-gpud** | 驱动仓库 README 列 VirtIO GPU device，但此次**没取得有真实 3D 渲染的证据** | 是 | Rust/user space scheme 和 Redox 内核/驱动边界 | 不按 3D 候选推进，除非后续取得原始测试和实现证据 |

## 原始源码和证据

1. [NaSeed PR #155](https://github.com/yituanxing/na-seed/pull/155) / [现有 Linux 适配审查记录](linux-virtgpu-v6.14.md)：原版 3D 源码导入但 **未编译链接**，三架构 QEMU 只验 GET_CAPSET_INFO，不能当 GPU 3D 成功。
2. [FreeBSD DRM-KMOD PR #517](https://github.com/freebsd/drm-kmod/pull/517)：作者完整的 3D 验收记录和未合并状态；实际 [LinuxKPI VirtIO 队列桥](https://github.com/borovikovd/drm-kmod/blob/virtio-gpu-v2/drivers/virtio/linuxkpi_virtio.c) 与 [FreeBSD 兼容模块](https://github.com/borovikovd/drm-kmod/blob/virtio-gpu-v2/drivers/gpu/drm/virtio/virtgpu_freebsd.c) 已核读。该方案还从 Linux 移植 DRM GEM shmem helper，不能只复制 virtqueue 文件。
3. [Haiku 原生 2D 驱动](https://github.com/haiku/haiku/blob/master/src/add-ons/kernel/drivers/graphics/virtio/virtio_gpu.cpp) 已核读（主要 2D 路径）；[第三方 3D 补丁源码](https://github.com/bluevisor/haiku-arm64-chromium/blob/main/gpu/haiku-virtio_gpu-3d.diff) 已核读结构与 3D ioctl/命令涉及的代码，[真实项目测试记录](https://github.com/bluevisor/haiku-arm64-chromium/blob/main/gpu/README.md) 明确是 **Venus** 而不是 NaSeed 目前验收的简单 VirGL capset，且用户态 Mesa 也有修改。
4. [UTM Windows WDDM 虚拟 GPU](https://github.com/utmapp/viogpu-wddm)：独立 `viogpu3d` 3D 与 `viogpudo` 2D 路径，用户态 Neptune/Venus，依赖 Windows WDDM/WDK。
5. [NetBSD viogpu 官方手册](https://man.netbsd.org/viogpu.4)：明确“does not provide access to the 3D functionality”。[SerenityOS 官方 graphics 文档](https://github.com/SerenityOS/serenity/blob/master/Documentation/Kernel/GraphicsSubsystem.md) 与 [Redox 驱动目录](https://github.com/redox-os/drivers) 目前仅作 2D 证据。
6. [QEMU VirtIO-GPU 后端文档](https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html) 和 [Mesa VirGL 文档](https://docs.mesa3d.org/drivers/virgl.html)：VirGL 和 Venus 并非同一宿主特性要求或用户态通路，不能用 capset probe 替代真实渲染。

## 根因分析：为何原版 Linux 3D 迟迟没有集成

现有 PR 的 `ports/linux-virtgpu` 代码尽管能通过三架构构建、DMA ownership/队列测试和 QEMU VirGL 功能探测，但**都是 NaSeed 适配层对象，而不是已链接的 Linux 原版 `virtgpu_vq.o`**。2026-11-28 已新增直接编译原版 `virtgpu_vq.c` 的 CI 诊断，第一次就因缺 `linux/dma-mapping.h` 失败。后续还必须解决 Linux scatterlist/kmem_cache、DRM/GEM shmem、dma_resv、dma_fence/syncobj、workqueue 和原版 ioctl。这不是一个“仅补两三个接口”的问题。

错误决策：之前虽看过 FreeBSD 例子，却没有先完成“3D 能力、核心代码作者身份、上游依赖总量、Mesa 用户态 ABI、跨架构证据、许可证”六项对比，就继续写 Linux 兼容层。结果代码量增长，核心原版驱动并未启动。不能再以 2D/CAPSET/队列 CI 的成功率作为 3D 移植的推进指标。

## 选型与执行约束

- **停止继续堆 Linux shim**，保留当前通过的基础代码和 PR #155 Draft，先审完候选；不能修改 NaSeed 原有 2D 驱动去配合未经验证的 3D 路线。
- **优先进一步审 Haiku 3D 补丁**：逐模块拆出真正的 Context/Resource/EXECBUFFER/Fence、请求所有权与用户态 ioctl；评估可保留多少原始代码、需要替换多少 Haiku VM/同步接口，并核实是否能支持 NaSeed 的三架构及 QEMU 标准 VirGL（不要把仅有的 Venus AArch64 测试当作已验证）。
- **并行审 FreeBSD 完整 3D 方案**：重点不是照抄 LinuxKPI 队列函数，而是盘点 DRM shmem/dma_resv/fence/syncobj 总依赖，确定是否可以尽可能导入原始已经验证的组件，避免重新手写一套 Linux API。
- **选择门槛**：候选至少有公开**真实 3D**运行证据；梳理所有 kernel/user ABI 和关键依赖；确定测试能从单 ISA 扩到 x86-64、RV64、AArch64；确认许可证；最后才能单独建立最小 PoC 分支。若要求“未经修改的 Alpine Mesa 直接使用”，则不能用 Haiku 私有 ioctl 冒充 Linux DRM render node。
- **必须先产生可运行证据**：原版/原生驱动对象真实编译链接 → 真实 QEMU Context/Resource/EXECBUFFER/Fence → 原版 Mesa 渲染和像素回读 → 3 ISA 复验。未完成前，不许宣称“GPU 已经在跑程序”。

### 审查时点结论

以**直接运行未修改的 Alpine Mesa VirGL**为硬要求，Linux/FreeBSD 同源的 DRM uAPI 与成熟 Mesa 对接最明确，但其整个 Linux DRM 内核依赖仍很重。以**优先获得任何一条真实 3D 路径**为目标，Haiku 是值得优先深入审查的非 Linux 原生候选，但它依赖 Venus + 修改后的 Mesa，且三架构移植尚未证明。**此时不宜因为已经写了 Linux 适配层，就排除 Haiku 或仓促断言 Linux 最优。**
