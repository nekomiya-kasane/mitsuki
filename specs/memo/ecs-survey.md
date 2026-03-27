# ECS Architecture Deep Survey

> **Scope**: Entity-Component-System 模式的主流实现、存储策略、调度模型、学术研究综述，及与 miki 设计的对比分析。
> **Date**: 2026-03-19
> **Author**: miki engine team (AI-assisted research)

---

## 1. ECS 核心概念

ECS (Entity-Component-System) 是一种以**数据为中心**的架构模式（Bilas 2002），将程序状态分解为：

- **Entity**: 纯标识符（通常是整数 ID + generation 计数器），不持有任何数据
- **Component**: 纯数据类型（POD struct），通过 entity ID 关联
- **System**: 纯逻辑（函数），通过查询（Query）选择匹配的 entity 集合，对其 component 数据执行变换

与 OOP 的根本区别：OOP 把身份（identity）、数据（data）和行为（behavior）绑定在一个对象中；ECS 把三者完全解耦。

---

## 2. 两大存储策略

### 2.1 Archetype-based (Unity DOTS, Flecs, Bevy)

**核心思想**：具有相同 component 集合的 entity 存储在同一个连续内存块（chunk/table）中。

```
Archetype A = {Position, Velocity}
┌──────────┬──────────┬──────────┐
│ Entity[] │ Position[]│ Velocity[]│  ← SOA layout, contiguous
└──────────┴──────────┴──────────┘

Archetype B = {Position, Velocity, Health}
┌──────────┬──────────┬──────────┬──────────┐
│ Entity[] │ Position[]│ Velocity[]│ Health[] │
└──────────┴──────────┴──────────┴──────────┘
```

**查询执行**：Query `(Position, Velocity)` → 遍历所有包含这两个 component 的 archetype，对每个 archetype 做线性扫描。因为数据在内存中连续，cache hit rate 极高。

**添加/移除 component**：entity 从旧 archetype 移动到新 archetype（memcpy），代价 O(N_components)。这是 archetype 模式的主要开销。

| 实现           | Chunk 大小      | 特殊优化                                                                        |
| -------------- | --------------- | ------------------------------------------------------------------------------- |
| **Unity DOTS** | 16KB 固定 chunk | Burst compiler (LLVM) 向量化；chunk 内 SOA；structural change 延迟到 sync point |
| **Flecs**      | 动态大小 table  | Relationship pair 编码在 entity ID 中；查询缓存（cached queries）               |
| **Bevy**       | 动态大小 table  | Rust ownership 保证线程安全；schedule graph 自动并行                            |

### 2.2 Sparse Set-based (EnTT, miki)

**核心思想**：每种 component 类型有独立的 pool（dense array + sparse lookup table）。Entity 是 pool 的索引键。

```
ComponentPool<Position>:
  dense:  [Pos_A, Pos_B, Pos_C, ...]   ← contiguous for iteration
  sparse: [_, 0, _, 2, 1, _, ...]       ← entity index → dense index

ComponentPool<Velocity>:
  dense:  [Vel_A, Vel_C, ...]
  sparse: [_, 0, _, _, 1, _, ...]
```

**查询执行**：Query `(Position, Velocity)` → 选择最小的 pool 为 pivot，遍历 pivot 的 dense array，对每个 entity 检查其他 pool 的 sparse 表（O(1) 查找）。

**添加/移除 component**：直接在目标 pool 中 append/swap-remove，O(1)。无需移动其他 component 数据。

| 实现     | Sparse 表结构                | 特殊优化                                                            |
| -------- | ---------------------------- | ------------------------------------------------------------------- |
| **EnTT** | Paged sparse array (4K page) | Groups（排序共享 dense 数组实现 O(N) 多组件迭代）；signals/reactive |
| **miki** | Flat vector (uint32_t)       | ComponentRegistry 动态类型注册；smallest-pool pivot query           |

### 2.3 性能对比（学术基准）

Eurographics 2024 论文 "Run-time Performance Comparison of Sparse-set and Archetype ECS" (Staffordshire University) 的关键发现：

| 操作                   | Archetype 优势              | Sparse Set 优势                       |
| ---------------------- | --------------------------- | ------------------------------------- |
| **单组件迭代**         | ✅ ~2× faster (SOA prefetch) |                                       |
| **多组件迭代 (2-3)**   | ✅ ~1.5× faster              |                                       |
| **添加 component**     |                             | ✅ ~10-100× faster (无 archetype 迁移) |
| **移除 component**     |                             | ✅ ~10-100× faster                     |
| **随机单 entity 访问** |                             | ✅ ~2× faster (直接 sparse 查找)       |
| **创建/销毁 entity**   |                             | ✅ ~3× faster                          |
| **多组件迭代 (5+)**    | ~相当                       | ~相当 (sparse 查找开销摊销)           |

**结论**：Archetype 适合**迭代密集、结构稳定**的场景（游戏主循环）；Sparse Set 适合**结构频繁变化、随机访问**的场景（编辑器、CAD）。

---

## 3. 主流 ECS 框架深度分析

### 3.1 Flecs (C/C++, Sander Mertens)

**架构**：Archetype + Relationship ECS。Flecs 是唯一将 entity 之间的关系（parent-child, depends-on, is-a）作为一等公民的 ECS。

**Entity ID 布局**：64-bit `[flags:16 | generation:16 | index:32]`

| 特性                    | 设计                                                                                                                                       |
| ----------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| **Archetype table**     | 动态大小，per-type column（SOA）。添加 component → entity 迁移到新 table。                                                                 |
| **Relationship pairs**  | `(Relation, Target)` 编码为 entity ID（bit63=pair flag）。例如 `(ChildOf, parent_entity)` 是一个 component 类型，存储在 archetype 中。     |
| **查询**                | Cached queries（编译时匹配 archetype，运行时仅遍历匹配 table 列表）+ Uncached queries（每次重新匹配）。Filter 支持 All/Any/None/Optional。 |
| **Deferred operations** | System 内部的 entity 操作（create/destroy/add/remove）自动入队（command buffer），system 结束后批量执行。避免迭代中修改结构。              |
| **Prefab 继承**         | `(IsA, Prefab)` relationship 自动继承 prefab 的所有 component。实例只存 override。                                                         |
| **Observer**            | 响应式系统：当 component 被添加/移除/修改时触发回调。                                                                                      |

**适用场景**：复杂层级关系（游戏场景图、UI 树）、需要 prefab 系统的项目。
**不适用**：关系模型增加约 30% 的 archetype 匹配复杂度；对纯数据管道（如 GPU SceneBuffer 同步）过度设计。

### 3.2 EnTT (C++17, Michele Caini / skypjack)

**架构**：Sparse Set。C++ 模板元编程实现零开销抽象。

**Entity ID 布局**：32-bit `[version:12 | entity:20]`（默认），可配置为 64-bit。

| 特性                    | 设计                                                                                                                                                                    |
| ----------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **ComponentPool**       | `basic_sparse_set` + `basic_storage<T>` (typed wrapper)。Dense array contiguous for iteration。Sparse array paged (4KB pages)。                                         |
| **Groups**              | 独创优化：多个 pool 共享 dense 数组排序，使多组件迭代等价于单数组线性扫描。例如 `group<Position, Velocity>` 保证 Position pool 和 Velocity pool 的 dense 数组元素对齐。 |
| **Signals**             | `on_construct`, `on_update`, `on_destroy` 信号。可用于实现 reactive 系统。                                                                                              |
| **No system scheduler** | EnTT 不提供内建的 system 调度器——系统执行顺序由用户代码控制。这是设计决策：保持库的最小化。                                                                             |
| **Registry**            | 核心入口。管理 entity 生命周期 + component pool 注册。`view<T...>` 返回 lazy iterator（最小 pool pivot 策略）。                                                         |

**适用场景**：需要极致灵活性和最小依赖的 C++ 项目；编辑器/工具（频繁添加/移除 component）。
**不适用**：无内建并行调度，大型项目需要自建。

### 3.3 Unity DOTS (C#, Unity Technologies)

**架构**：Archetype + Chunk + Burst + Job System。工业级最成熟的 ECS 实现。

**Entity ID 布局**：64-bit `[version:8 | index:24 | world:8 | flags:24]`

| 特性                      | 设计                                                                                                              |
| ------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| **Archetype Chunk**       | 16KB 固定大小 chunk，每个 chunk 存储同一 archetype 的 N 个 entity。Chunk 内 SOA layout。                          |
| **Structural changes**    | 延迟到 sync point（`EntityCommandBuffer`）。Sync point 内所有 structural change 批量执行。                        |
| **Burst Compiler**        | 基于 LLVM 的 AOT/JIT 编译器，将 C# Job 代码编译为高度优化的本机代码（自动向量化 SIMD，去除 GC 边界检查）。        |
| **Job System**            | `IJobChunk` 逐 chunk 并行执行。自动依赖分析：读写同一 component 类型的 job 串行化；读不同 component 的 job 并行。 |
| **Enableable components** | 不移除 component 而是标记为 disabled（避免 archetype 迁移开销）。                                                 |
| **Shared components**     | 值相同的 component 在 chunk 间共享（例如 material ID），用于 chunk 级别分组。                                     |
| **Managed components**    | 允许引用类型（class）作为 component，但会绕过 Burst 优化。                                                        |

**适用场景**：大规模游戏（>100K 同质 entity），需要 SIMD/多核并行。
**不适用**：C# 生态锁定；structural change 延迟模型对交互式编辑器不友好。

### 3.4 Bevy ECS (Rust, Cart Anderson)

**架构**：Archetype + Schedule Graph。Rust 所有权模型天然保证线程安全。

**Entity ID 布局**：64-bit `[generation:32 | index:32]`

| 特性                  | 设计                                                                                                                |
| --------------------- | ------------------------------------------------------------------------------------------------------------------- |
| **Table storage**     | 类似 Flecs 的动态 archetype table（SOA column）。可选 `SparseSet` storage hint（per-component 选择）。              |
| **Schedule**          | System 声明 `Query<&Position, &mut Velocity>` → 编译时推导读/写依赖。Schedule graph 自动拓扑排序 + 并行 fork-join。 |
| **Exclusive systems** | 需要 `&mut World` 的 system 独占执行（sync point）。其他 system 并行执行。                                          |
| **Change detection**  | Per-component tick 计数器。`Changed<T>` filter 只迭代自上次 system 执行以来被修改的 component。极大减少无效迭代。   |
| **Resources**         | 全局单例数据（`Res<T>`, `ResMut<T>`），与 component 分开管理。                                                      |
| **Events**            | 类型安全的事件队列。`EventReader<T>` / `EventWriter<T>`。双缓冲：当前帧读上一帧写的事件。                           |

**适用场景**：Rust 项目；需要自动并行调度；中大型游戏。
**不适用**：Rust 编译时间长；生态尚不如 Unity 成熟。

### 3.5 Our Machinery / The Machinery (已停运)

**架构**：Bitsquid/Stingray 血统。"The Truth" 数据模型 + 细粒度 component change tracking。

| 特性               | 设计                                                                                                       |
| ------------------ | ---------------------------------------------------------------------------------------------------------- |
| **The Truth**      | 中央数据模型，所有编辑器状态存储在此。支持 undo/redo（操作日志）、branching（类似 git）、multi-user 协作。 |
| **Component 存储** | 类似 sparse set，但每个 component pool 支持 change notification。                                          |
| **System 调度**    | 基于 fiber 的 task graph。任务声明依赖（component access），runtime 自动并行。                             |

**影响**：虽然已停运，但 "The Truth" 数据模型对 CAD 引擎有极高参考价值——它的 undo/branching 模型与 CAD 的 parametric history + configuration management 需求高度吻合。

### 3.6 其他值得关注的实现

| 框架         | 语言       | 存储                          | 特色                                  |
| ------------ | ---------- | ----------------------------- | ------------------------------------- |
| **ECSY**     | JavaScript | Sparse Map                    | Web 前端 ECS                          |
| **Shipyard** | Rust       | Sparse Set                    | EnTT 的 Rust 移植思路                 |
| **Entitas**  | C#         | Reactive (event-driven)       | Component 变化触发 system，非每帧轮询 |
| **specs**    | Rust       | Sparse Set + Archetype hybrid | 已弃用（被 Bevy 取代）                |
| **hecs**     | Rust       | Archetype                     | 最小化 archetype ECS，无 schedule     |
| **legion**   | Rust       | Archetype                     | 被 Bevy 借鉴后独立维护                |
| **Arch**     | C#         | Archetype                     | 高性能 .NET archetype ECS             |

---

## 4. 调度与并行模型

### 4.1 系统调度策略对比

| 策略                    | 框架          | 描述                                  | 优点                      | 缺点                          |
| ----------------------- | ------------- | ------------------------------------- | ------------------------- | ----------------------------- |
| **手动顺序**            | EnTT          | 用户代码控制 system 执行顺序          | 完全可控，零抽象开销      | 无自动并行；大型项目维护困难  |
| **声明式 DAG**          | Bevy          | System 声明读/写，框架构建 DAG        | 自动并行，模块化          | DAG 构建有运行时开销          |
| **Job Graph**           | Unity DOTS    | IJobChunk 逐 chunk 并行               | 极致数据并行              | 学习曲线陡峭；sync point 限制 |
| **Pipeline Stage**      | Flecs         | 预定义阶段（OnUpdate, OnValidate 等） | 直观，阶段间隐式同步      | 阶段内并行受限                |
| **Fiber/Task**          | Our Machinery | fiber-based task graph                | 细粒度任务并行            | 实现复杂                      |
| **ComponentAccessDecl** | miki          | 手动声明读/写 component 集合          | 简单，Kahn 排序保证正确性 | 手动声明可能遗漏              |

### 4.2 学术研究：Core ECS 形式化 (UCSC, 2025)

Redmond et al. "Exploring the Theory and Practice of Concurrency in the Entity-Component-System Pattern" (arXiv 2508.15264, 2025) 是**首个 ECS 模式的形式化模型**。

**Core ECS 核心定义**：
- 状态 σ = Entity → Component → Value 的全函数
- System = (Query, Mutation) pair
- 一轮执行 = 对所有 system 的 query 求值 → 对匹配 entity 应用 mutation
- **确定性条件**：如果两个 system 的 write 集不相交，则它们可以安全并行执行（无论调度顺序，结果确定性相同）

**关键发现**：
1. 所有主流 ECS 框架都**未充分利用**可用的并行性——即使 system 的 write 集不相交，框架仍可能串行化它们
2. System 内部的 entity 迭代顺序是非确定性的主要来源——如果 system 对多个 entity 的写入存在数据竞争，迭代顺序影响最终结果
3. **Deferred command buffer**（Flecs/Unity DOTS 的做法）是保证确定性的充分条件：system 内部的操作延迟到 system 结束后批量执行，消除了迭代顺序依赖

### 4.3 Change Detection 策略

| 策略                      | 框架       | 机制                                   | 粒度                      |
| ------------------------- | ---------- | -------------------------------------- | ------------------------- |
| **脏标记 (dirty flag)**   | miki       | ECS change detection 标记脏 entity     | per-entity                |
| **Per-component tick**    | Bevy       | 每个 component 存储 last_changed tick  | per-component per-entity  |
| **Version counter**       | Unity DOTS | chunk 级别 version 计数器              | per-chunk (~100 entities) |
| **Observer signal**       | Flecs/EnTT | on_construct/on_update/on_destroy 回调 | per-operation             |
| **Reactive subscription** | Entitas    | system 订阅 component 变化事件         | per-system per-component  |

---

## 5. 高级主题

### 5.1 关系型 ECS (Relational ECS)

**Flecs 独创**。将 entity 之间的关系（parent-child, attached-to, depends-on）编码为 component 类型。

```cpp
// Flecs: ChildOf 关系
entity.add(flecs::ChildOf, parent);
// 内部：entity 的 archetype 包含 (ChildOf, parent) 这个 "component"
// 查询所有 parent 的子节点：
world.query<Position>().with(flecs::ChildOf, parent).each(...);
```

**优势**：
- 层级遍历变成 archetype 匹配（cache-friendly）
- 级联删除自动（删除 parent → 删除所有 ChildOf 该 parent 的 entity）
- Prefab 继承自然（IsA relationship）

**局限**：
- 每种 (Relation, Target) 组合创建一个新 archetype → archetype 爆炸（N 个 parent = N 个 archetype）
- 不适合表达复杂图结构（B-Rep 半边数据结构、约束图）

### 5.2 Reactive / Event-Driven ECS

**传统 ECS**：每帧轮询所有匹配 entity → 即使没有变化也执行 system。

**Reactive ECS**（Entitas, Flecs Observer, EnTT signals）：system 仅在 component 变化时触发。

| 策略               | 触发时机                    | 适用场景                         |
| ------------------ | --------------------------- | -------------------------------- |
| **Polling (传统)** | 每帧                        | 物理模拟、渲染（每帧都需要执行） |
| **Reactive**       | component add/remove/modify | UI 更新、事件处理、稀疏变化      |
| **Hybrid**         | 轮询 + 变化过滤             | Bevy `Changed<T>` filter         |

### 5.3 GPU ECS / GPU SceneBuffer

将 ECS 数据直接映射到 GPU SSBO 是现代渲染引擎的趋势。

| 引擎           | GPU 数据模型                             | 同步策略                              |
| -------------- | ---------------------------------------- | ------------------------------------- |
| **UE5**        | `FPrimitiveSceneInfo` → GPU Scene Buffer | 增量更新，GPUScene 是 flat array      |
| **Unity DOTS** | Chunk 数据直接 memcpy 到 GPU             | Burst job 负责转换+上传               |
| **Filament**   | Flat array, per-entity upload            | Dirty flag + bulk upload              |
| **miki**       | `GpuInstance[128B]` SSBO via BDA         | ECS ForEachAlive → 脏 entity 增量上传 |

### 5.4 ECS 与 CAD/CAE 的适配性分析

| CAD 需求                                   | ECS 适配度 | 分析                                                                   |
| ------------------------------------------ | ---------- | ---------------------------------------------------------------------- |
| **大量同质实例** (标准件库)                | ★★★★★      | 10K 螺栓实例 → 单 archetype，极致迭代性能                              |
| **异构零件** (每个零件不同 component 组合) | ★★★☆☆      | archetype 爆炸；sparse set 更合适                                      |
| **频繁编辑** (add/remove features)         | ★★★★☆      | sparse set O(1) add/remove；archetype 需要迁移                         |
| **装配体层级**                             | ★★★☆☆      | 关系型 ECS (Flecs) 可以表达 parent-child；但不如专用 segment tree 高效 |
| **B-Rep 拓扑**                             | ★☆☆☆☆      | 半边数据结构太复杂，不适合 ECS 表达                                    |
| **Undo/Redo**                              | ★★☆☆☆      | ECS 无原生 undo 支持；Our Machinery "The Truth" 模型更合适             |
| **GPU 场景同步**                           | ★★★★★      | ForEachAlive → dirty upload 完美契合                                   |
| **选择/高亮/幽灵**                         | ★★★★★      | selectionMask 作为 component，O(1) 修改                                |

---

## 6. 与 miki 设计的对比

### 6.1 miki ECS 设计选择

| 决策                | miki 选择                        | 替代方案                  | 理由                                                                   |
| ------------------- | -------------------------------- | ------------------------- | ---------------------------------------------------------------------- |
| **存储**            | Sparse Set                       | Archetype                 | CAD 零件异构（每个零件不同 component 组合）；编辑操作频繁 add/remove   |
| **Entity ID**       | 32-bit [gen:8 \| idx:24]         | 64-bit                    | GPU 端 `GpuInstance.entityId` 需要紧凑（4B × 10M = 40MB vs 8B = 80MB） |
| **Generation wrap** | Wrap / Retire 双策略             | 固定 wrap                 | GenerationWrapPolicy::Retire 消除长时间运行的 dangling handle 风险     |
| **查询**            | Smallest-pool pivot              | Archetype 匹配            | Sparse set 的标准做法；对少量 component 查询效率高                     |
| **调度**            | Kahn DAG + jthread               | Bevy-style schedule graph | 手动 ComponentAccessDecl 更简单；30 人团队可控                         |
| **变化检测**        | ECS dirty flag                   | Per-component tick        | ForEachAlive + dirty upload 是 GPU 同步的最小化方案                    |
| **关系**            | 无（专用 CadScene segment tree） | Flecs relationship        | B-Rep/装配体层级太复杂，不适合 ECS pair 建模                           |
| **Undo**            | 无（Phase 8 CadScene 层）        | ECS-level undo            | CAD undo 是 parametric history，不是 component 快照                    |

### 6.2 miki 设计的优势

1. **GPU 数据管道最短路径**：Entity → ComponentPool\<Transform\> → ForEachAlive → GpuInstance[] SSBO。无 archetype 迁移开销。
2. **32-bit Entity 节省 GPU 带宽**：10M instances × 4B = 40MB（vs 8B = 80MB）。
3. **编辑友好**：Sparse set O(1) add/remove component，不触发 archetype 迁移。
4. **CAD 专用结构正交**：TopoGraph（B-Rep）、CadScene（装配体树）、MaterialRegistry（材质管理）各自独立于 ECS。ECS 只负责 GPU 数据管道。

### 6.3 miki 设计的潜在改进方向

| 改进                               | 来源              | 价值                                                            | 阶段                                |
| ---------------------------------- | ----------------- | --------------------------------------------------------------- | ----------------------------------- |
| **Change detection per-component** | Bevy `Changed<T>` | 减少 GPU 上传量（只上传变化的 component，而非整个 GpuInstance） | Phase 6a                            |
| **EnTT Groups**                    | EnTT              | 多组件迭代 O(N) 无 sparse 查找                                  | 低优先级（miki 的多组件查询场景少） |
| **Deferred command buffer**        | Flecs/Unity DOTS  | 保证 system 内部操作的确定性                                    | Phase 8（CadScene 编辑命令）        |
| **Observer 信号**                  | Flecs/EnTT        | 响应式 UI 更新（仅当 component 变化时刷新 ImGui 面板）          | Phase 9                             |

---

## 7. 前沿研究方向

### 7.1 确定性并行 ECS (Core ECS, UCSC 2025)

Redmond et al. 的形式化模型证明了 ECS 模式天然适合确定性并行编程。关键洞察：**如果 system 的 write 集不相交，则无论调度顺序，结果确定性相同**。这为 ECS 框架提供了理论基础来自动识别可并行的 system 对。

当前所有主流框架都依赖运行时的粗粒度锁（archetype 级别或 component 类型级别），而非利用 write 集分析来最大化并行。

### 7.2 GPU-native ECS

传统 ECS 在 CPU 上运行，GPU 端是 flat buffer（GpuInstance SSBO）。新兴研究探索**在 GPU compute shader 中直接运行 ECS system**：

- **GPU Scene Submission** (UE5 Nanite): GPU compute 直接操作 SceneBuffer，零 CPU 参与
- **GPU ECS** (experimental): entity 操作（create/destroy/add/remove）在 GPU 上执行，通过 atomic 和 indirect dispatch 实现

miki 的 Phase 6a GPU Scene Submission 已朝这个方向设计：SceneBuffer 是 GPU-resident SSBO，compute shader 直接读写。

### 7.3 Streaming / Out-of-Core ECS

当 entity 数量超过内存容量时（10B+ 实体），ECS 需要 streaming 支持：

- **Entity paging**: entity 按空间区域分页，仅常驻可见页
- **Component LOD**: 远处 entity 只存储低频 component（Position），近处存储全部
- **与 Virtual Geometry 集成**: miki Phase 6b 的 ClusterDAG streaming 与 ECS 的 entity paging 正交但互补

### 7.4 ECS + Machine Learning

- **训练数据生成**: ECS 的 SOA layout 天然适合作为 ML 训练数据（batch = archetype chunk）
- **Neural system**: 用 MLP 替代传统 system 逻辑（例如 AI NPC 行为）
- **GNN on entity graph**: 如果 ECS 有关系（Flecs），entity graph 可以直接作为 GNN 输入

---

## 8. 参考文献

### 学术论文
1. Redmond et al., "Exploring the Theory and Practice of Concurrency in the Entity-Component-System Pattern", arXiv 2508.15264, 2025.
2. Staffordshire University, "Run-time Performance Comparison of Sparse-set and Archetype ECS", Eurographics 2024.
3. Bilas, "A Data-Driven Game Object System", GDC 2002 (ECS 模式的起源).
4. Caini (skypjack), "ECS Back and Forth" blog series, 2019-2022 (EnTT 设计理论).
5. Mertens, "Building an ECS" blog series, 2019-2024 (Flecs 设计理论).

### 框架文档
6. Flecs: https://www.flecs.dev/flecs/
7. EnTT: https://github.com/skypjack/entt
8. Unity DOTS: https://unity.com/ecs
9. Bevy ECS: https://docs.rs/bevy_ecs/
10. Our Machinery (archived): https://ourmachinery.com/

### miki 相关
11. `include/miki/scene/Entity.h` — Entity 32-bit, EntityManager, GenerationWrapPolicy
12. `include/miki/scene/ComponentStorage.h` — ComponentPool<T>, Sparse Set
13. `include/miki/scene/QueryEngine.h` — Query<All,Any,None>
14. `include/miki/scene/SystemScheduler.h` — Kahn DAG, ComponentAccessDecl
15. `specs/rendering-pipeline-architecture.md` §5.5 — GpuInstance layout
