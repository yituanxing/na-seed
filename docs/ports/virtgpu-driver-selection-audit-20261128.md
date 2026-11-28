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
| **Haiku 原生 VirtIO-GPU + 第三方 3D 补丁** | 作者报告 UTM AArch64 Venus/Vulkan、WebGL 2、Zink/GL 与像素读回；非 NaSeed 验证或正式 Haiku 合并 | **是**：Haiku C++ 原生驱动 | 补丁 **+1,695/-117，3 文件**；有 CONTEXT_INIT、RESOURCE_CREATE_BLOB、SUBMIT_3D、Fence，但未见 **RESOURCE_CREATE_3D / TRANSFER_*_HOST_3D** 的 VirGL 经典资源通道；还需 Haiku VM/线程/hostmem BAR、自定义 render ioctl + **修改 Mesa Venus**（其补丁新增 `vn_renderer_haiku.c` 约 851 行），实测 AArch64 UTM | **Venus 专用成熟性参考**，不是当前 VirGL/OpenGL + 原版 Alpine Mesa 的直接候选；除非决定切换整条图形栈 |
| **rcore-os/virtio-drivers (Rust no_std)** | 源码包含 x86-64 QEMU 条件运行的 **15 步 VirGL 回归**，实际实现 CTX/3D Resource/Attach Backing/Transfer/SUBMIT_3D(Fence)；测试没有在 CLEAR 后再做逐像素断言，不能当作已验证的 glmark2 | **是**，独立 Rust 设备驱动，不使用 Linux DRM | `src/device/gpu/mod.rs` 约 **1,268 行**，`examples/x86_64/src/virgl.rs` 约 **312 行**；依赖整个 no_std `virtio-drivers`、Hal、Transport、VirtQueue，Rust cross-compile/FFI；不提供 Linux render node/Mesa API；MIT 许可 | **VirGL 协议/真实设备链路首选参考和候选最小 PoC**。不是可直接运行原版 Alpine Mesa 的替代 DRM |
| **TGOSKits / StarryOS：Rust DRM + VirtIO-GPU + 原版 Mesa** | 上游文档有 **2026-09 x86-64 QEMU/KVM Weston、Mesa `GL_RENDERER: virgl`、glmark2 默认场景集（Score 43）及 VNC 3D 动画人工证据**；特别记录 `refract` 场景 `GL_FRAMEBUFFER_UNSUPPORTED`；x86-64/AArch64/RV64/LoongArch64 有 DRM 通用系统测试记录，**不是四架构 3D 已验收** | **是**：自有 Rust `virtio-gpu` + `kdrm-core`/DRM Linux 语义移植，不是原样 Linux C 6.14 GPU 模块 | `components/kdrm-core` (MIT, `no_std`，约 60+ DRM 模块) + `drm-kapi` (MIT, `no_std`，约 50 内核机制模块) + `os/arceos/modules/axdrm`（依赖 ax-gpu、ax-task/ax-mm/ax-hal、RDIF）+ Starry `card0`/文件与 VMA；跨 C/Rust 工具链和 ABI、provider、GPL/MIT 原算法来源合规审查 | **本轮最强的新发现：完整真实应用级 3D 证据 + Linux DRM UAPI 的非 Linux C 实现**。优先深入评估 `kdrm-core`/`drm-kapi` 可否原样编译链接到 NaSeed C 内核，而不是照抄 ArceOS `axdrm` |
| **go-virtio/gpu (Go)** | 项目作者通过真实 virglrenderer `vtest` + **llvmpipe CPU 软件渲染器**验证 CLEAR、三角形、带纹理三角形的像素回读；独立 QEMU guest 的渲染/Alpine 支持仍未证实 | **是**，不使用 Linux DRM | pure-Go 及其 runtime/common transport，`gpu3d.go` 约 **392 行**仅含 fixed CLEAR 路径，其他源文件提供三角形；手工构造固定 TGSI/virgl 命令，不是完整 Mesa 驱动；BSD-3-Clause | 很好的**可比对字节序列与像素读回**测试语料；不是复用到 NaSeed C 内核的可直接链接驱动 |
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

## 深审结果：可复用源码与不可跳过的内核/用户态边界

本节按实际**源码函数与验收程序**审查，避免只看 README。

### Haiku 原生 3D 实现：有完成链路，缺标准 VirGL 的三类对象调用

- 驱动基础：[Haiku `virtio_gpu.cpp`](https://github.com/haiku/haiku/blob/master/src/add-ons/kernel/drivers/graphics/virtio/virtio_gpu.cpp)，约 985 行，主职责 2D 和显示；3D 由 [第三方约 2,103 行 diff](https://github.com/bluevisor/haiku-arm64-chromium/blob/main/gpu/haiku-virtio_gpu-3d.diff) 补充。
- 真实协议：`VIRTIO_GPU_CMD_SUBMIT_3D`、`RESOURCE_CREATE_BLOB`、`RESOURCE_MAP_BLOB`、`CTX_CREATE/DESTROY`、`CTX_ATTACH_RESOURCE` 与自有 fence id 的 `poll`/wait。异步 request 队列在完成线程中消费，直到 fence response 才通知用户，成功案例不是软件 framebuffer 伪装。
- **已核验的缺项**：所审 diff 内不存在 `VIRTIO_GPU_CMD_RESOURCE_CREATE_3D`、`TRANSFER_TO_HOST_3D`、`TRANSFER_FROM_HOST_3D` 的调用。它实际服务 Venus/Blob，而非完整 VirGL 经典上下文；不能只因为有 `SUBMIT_3D` 就叫“可以直接跑 virgl Gallium”。
- 用户态还需要 [Mesa 26.2.5 Haiku Venus patch](https://github.com/bluevisor/haiku-arm64-chromium/blob/main/gpu/mesa-26.2.5-haiku-venus.diff)，约 1,089 行 diff，关键新文件 `vn_renderer_haiku.c` **851 行**；Haiku `MAP` 与 `FENCE_BIND` 替代 Linux DRM `mmap/sync_file`。所以 kernel 3D 补丁行数**绝不代表整条适配成本**。
- kernel 依赖分布：`get_memory_map_etc`/phys entries 组织 DMA、Haiku B_SYSTEM_TEAM 内核地址归属、`ConditionVariable` + completion thread、`vm_map_physical_memory`/B_SHARED_AREA、host-visible PCI SHM BAR、render file 的 Context/Handle/Blob 容器、select/poll。C++ 数据结构也要求 Haiku C++ runtime。NaSeed 目前没有这些“一比一兼容设施”。
- **审查判决**：保留其异步 Fence/Blob 生命周期设计作参考，但不原样导入到 NaSeed 的标准 VirGL → 原版 Alpine Mesa 路线；否则需要再移植 Venus/Mesa 用户态，并不能更快实现原目标。

### rcore-os 独立 Rust VirtIO-GPU 实现：目前最接近真正可重用的轻量 VirGL *协议驱动*

- [GPU 原始实现](https://github.com/rcore-os/virtio-drivers/blob/master/src/device/gpu/mod.rs)（约 1,268 行）包含 `get_capset_info/get_capset`、`ctx_create/destroy`、`resource_create_3d`、`resource_attach_backing`、`transfer_{to,from}_host_3d`、`submit_3d`、`resource_create_blob`。由 `virtio-drivers` 的 `Hal`/`VirtQueue` 完成 DMA/Transport，GPU 层不重造完整队列。MIT。
- [x86-64 测试原码](https://github.com/rcore-os/virtio-drivers/blob/master/examples/x86_64/src/virgl.rs)（约 312 行）**15 个步骤**：VIRGL 协商、capset、Context、3D resource + guest backing、传输、向绑定 surface 提交 CLEAR 和 fenced `SUBMIT_3D`、可选 Blob、detach/unref/destroy。README 的 `make qemu gl=on` 是项目提供的真实 GL QEMU 测试入口。
- **证据边界**：测试在 CLEAR 前已经做 `TRANSFER_FROM_HOST_3D`，**并未在 CLEAR 后再次读回并断言预期像素**；“15 steps passed”验证的是控制/提交/资源生命期，不是最终像素正确性，更不是运行了 Mesa/glmark2。这里只核读了上游代码，NaSeed 还没有运行该上游 Rust test。
- **移植边界**：Rust `no_std + alloc`、HAL trait、`virtio-drivers::Transport` 与 ring 所有权；NaSeed 现有 C split ring 并不自动满足 Rust trait。不能为了采用它就翻译成新 C 3D 代码——那又会回到自写驱动。若作为 PoC，应**保持原 Rust 源码**，以明确的 Rust build + FFI/平台 HAL 方案集成；这种语言边界需要独立验证和许可证审查。
- **审查判决**：**首选最小 VirGL 真实设备协议验证的上游代码**，但不能自动生成 Linux DRM render node 或使 Alpine 原版 Mesa 能打开它。不得把 Rust PoC 完成宣称为完整 3D 软件兼容。

### go-virtio/GPU：很好的真实渲染 oracle，不是目标内核驱动

- [纯 Go 协议实现](https://github.com/go-virtio/gpu) 和 [`go-virtio/validate` 原始测试](https://github.com/go-virtio/validate/blob/main/README.md)：有真实 `SUBMIT_3D` VirGL 命令字节流、CLEAR/triangle/textured triangle 和 framebuffer 回读；使用真实 virglrenderer `vtest`，不是伪造响应。
- **但验收是软件 `llvmpipe`** 的 host renderer，作者也明确区分另一条 **2D framebuffer + CPU 软件立方体** 验收，因此不能称之为“实际物理 GPU 硬件加速”或“Linux Mesa 应用已运行”。Go 代码还要 Go runtime、设备抽象，不满足 NaSeed C 内核直接移植。
- **审查判决**：用其通过的 protocol bytes/readback 作为独立交叉比对用例，**不把手写固定 TGSI stream 当成可供真实应用使用的 Mesa 替代品**。

### FreeBSD：真正跑过 glmark2，但全量依赖不小

- [FreeBSD DRM-KMOD PR #517](https://github.com/freebsd/drm-kmod/pull/517) 当前仍为未合并 PR，审查到 **48 个变更文件**，其中包含 Linux 6.14 的 virtgpu 组件、GPL-2.0 的 843 行 `drm_gem_shmem_helper.c`，`drm_gem.c`/DRM core、`dma_fence`、`dma_resv` 相关头、LinuxKPI VirtIO、DMA/folio/pagemap、设备/irq 销毁以及 KMS 等。LinuxKPI 队列桥仅 484 行，绝不是总依赖体积。
- 作者报告实际运行 sway GLES2、glmark2、`EXECBUFFER` 压力测试，且通过 arm64/aarch64 和 amd64 测试；但这些均在 FreeBSD 内核/LinuxKPI 现成设施上完成，**不是**在 NaSeed 当前核心上可直接复现的工具包。
- **审查判决**：**原版 Alpine Mesa 对接首选的验证过的完整设计参考**。NaSeed 需要的是把成熟 DRM/GEM/Fence 核心作为一个受审查的集成单元，而不是再一个个追加缺失的 Linux headers。

### 推荐验收路线（避免再陷入无尽外围兼容）

| 验收关口 | 直接可用的来源 | 合格证据 | 禁止的替代“成功” |
|---|---|---|---|
| A：真实 VirGL 命令链 | rcore 上游 Rust 15 步测试 + go-virtio 的像素回读 oracle | 原始上游代码在目标 QEMU/VirGL 设备实际成功；最好要求渲染**后的**读回校验 | Capset probe、2D scanout、mock queue SUCCESS |
| B：NaSeed 原版驱动集成 | Linux v6.14 源码，FreeBSD #517 证明的完整依赖拓扑 | **原始 virtgpu 源文件**被编译链接并作为 GPU owner 启动，真实 Context / Resource / Fence | 仅编译 ports/linux-virtgpu 下的自写兼容层 |
| C：应用级 GPU 渲染 | 未修改 Alpine Mesa/virgl、`glmark2` 或明确的 GLES 测试 | `/dev/dri/renderD128`、真实 `EXECBUFFER`、GPU 完成、读回校验，三 ISA 逐步复制 | 运行作者修改过的 Venus-only 用户态然后称为原版 VirGL |
| D：性能真实性 | 主机 virglrenderer renderer / QEMU host GPU 状态 | 记录实际 renderer，区分 GPU 设备加速 vs `llvmpipe` 软件渲染 | 仅看到 VirtIO-GPU 名字就写“物理 GPU 加速” |

### TGOSKits / StarryOS：同目标、真 glmark2、完整共享 DRM，但不是一个小 GPU 驱动

- [TGOSKits GPU 栈原始记录](https://github.com/rcore-os/tgoskits/blob/dev/docs/docs/runtime/gpu-stack.md)：将驱动与 Linux DRM ABI 分成 `rdif-gpu` / `ax-gpu`（设备 owner）、`ax-drm`（桥）、`kdrm-core`（GEM/PRIME/KMS/atomic/syncobj/ioctl）、`drm-kapi`（内核锁、页、等待、时钟、DMA/fence）、Starry `card0`（进程 fd、poll、VMA）。它不是只复制 Linux `virtgpu_vq.c` 后空缺其余设施。
- [共享 DRM 原始设计与限制](https://github.com/rcore-os/tgoskits/blob/dev/docs/design/shared-drm.md)：从 Linux **7.2.3** 的 DRM 内部行为移植为 Rust（项目文档明确说是**Rust 移植**，不是原始 Linux C 代码），`*_upstream.rs` 对应原函数，关键公开边界是 `drm-kapi` provider；后端使用既有 VirtIO 而不重建另一队列。文件归属 / DMA backing / fence callback / 丢失设备后才释放的条件均写清。**必须独立核实代码许可与实际源码来源，不能见 Cargo.toml 写 MIT 就直接认定所有改编算法没有分发约束。**
- 实际核读 [`components/kdrm-core/src/lib.rs`](https://github.com/rcore-os/tgoskits/blob/dev/components/kdrm-core/src/lib.rs)，该 crate 自称 `#![no_std]`，约 **60+** 个 DRM/C-type/Upstream 模块，主要依赖 `drm-kapi`/spin；[kdrm-core Cargo.toml](https://github.com/rcore-os/tgoskits/blob/dev/components/kdrm-core/Cargo.toml) 标 **MIT**。[`drm-kapi/src/lib.rs`](https://github.com/rcore-os/tgoskits/blob/dev/components/drm-kapi/src/lib.rs) 拆出工作队列、dma_fence、dma_resv、wait、VM、scatterlist、shmem、sync_file 等大量 Linux 行为；[`drm_gem_shmem_helper_upstream.rs`](https://github.com/rcore-os/tgoskits/blob/dev/components/kdrm-core/src/drm_gem_shmem_helper_upstream.rs) 约 **1,560 行**、[`dma_fence.rs`](https://github.com/rcore-os/tgoskits/blob/dev/components/drm-kapi/src/dma_fence.rs) 约 **1,144 行**，这并非“两个小 crate”。
- **不能原样移植 ArceOS 整合层**：[axdrm Cargo.toml](https://github.com/rcore-os/tgoskits/blob/dev/os/arceos/modules/axdrm/Cargo.toml) 依赖 `ax-gpu`、`ax-task`、`ax-mm`、`ax-hal`、`rdif-gpu`/display 等具体 Rust ArceOS 设施。NaSeed 是 C 内核，若想**不改写上游 DRM Rust 源码**，先需最小可链接 Rust `no_std` 库（静态库/FFI）、NaSeed 自身的 alloc/panic、锁、页、用户 fd/VMA/信号的 provider，再注册真实设备；目前**没有**在 NaSeed 完成这项链接验证。
- [真实 3D 用例 README](https://github.com/rcore-os/tgoskits/blob/dev/apps/starry/virgl-test/README.md)：Weston DRM + GL，`/dev/dri/card0` 和 `renderD128`，`GL_RENDERER: virgl`，x86-64 QEMU/KVM 默认 `glmark2-es2-wayland` 场景通过且可视动画；**个别 `refract` 场景失败**、人工验收需手工结束虚机，**没有自动 PASS 标记**。这比只做 capset probe 的 NaSeed 证据强，但并非本次重新运行或所有功能/所有架构全面通过。上游通用 DRM x86/RV64/AArch64 测试不等于这三架构已有 3D 渲染测试。
- **选型影响**：TGOSKits 是 NaSeed 所求的“完整 Linux ABI + 成熟 3D 渲染路径”**非 Linux C 内核**实例，是与 Linux v6.14/FreeBSD #517 并列的真实候选。不能因为已经写了 Linux 适配层就排除；同样不能因为 Rust 是 MIT 就忽略巨大的跨语言 runtime 桥。下一决定性试验应是**原样的 kdrm-core/drm-kapi 能否单独以 C ABI 静态链接进 NaSeed 的 x86-64 最小内核**，并跑真正 provider 初始化；若不能，退回 Linux C + 完整 DRM 依赖导入，绝不把源文件翻译成另一套自写 DRM。

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

**经本轮逐文件核读后的具体选择：**

1. **完整应用级路线的最终候选并列评估两套成熟源代码**：**(A) Linux v6.14 原始 DRM VirtIO-GPU + FreeBSD #517 已验证的依赖拓扑**；**(B) TGOSKits `kdrm-core`/`drm-kapi` + 已运行真实 glmark2 的 VirtIO GPU/DRM 栈**。两者都明确服务于 Linux DRM render-node ioctl 和原版 Mesa。**先做一轮真实编译/链接可行性关口**：若 Rust kdrm-core 无法在 NaSeed C 内核以原样来源加最小 FFI/provider 成功链接/注册对象，则选择完整 Linux C 版本，不继续堆散落 Linux shim；若 Rust 方式可行，则比较许可证、实际 kernel/VFS 依赖和跨 ISA 成本后选型。
2. **最小真实 VirGL 命令链和像素验收，独立借鉴 rcore-os/virtio-drivers（MIT, Rust no_std）与 go-virtio/validate（BSD-3, real vtest+llvmpipe pixel oracle）**。无需假装其 15 步测试能运行原版 Mesa，也不要翻译为一套自写 C GPU 3D 实现。若验证 Rust 源码跨语言嵌入，会在独立 PoC 分支处理，不污染 PR #155。
3. **Haiku 第三方 3D 实现保留为 Venus/Blob/Fence 的经验证设计参考，不作为当前 VirGL 的主线候选**。缺少经典 ResourceCreate3D/Transfer 调用、需要 patch Mesa Vulkan backend 和 Haiku VM/线程栈，选择它会改变目标而非节约工作量。

**停止编写无审查依据的单点兼容接口。审查材料和真实 3D 上游协议源代码，优先于“有多少 CI 显示绿色”。**
