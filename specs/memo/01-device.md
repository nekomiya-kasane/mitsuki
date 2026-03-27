Device 在前沿渲染器中的职责
核心定位
Device = GPU 逻辑资源的工厂 + 生命周期管理者。它不直接参与渲染命令录制，而是负责「创建一切 GPU 资源」并「管理它们的生命周期」。

职责矩阵
职责分类	具体内容	UE5 (FRHIDevice)	Filament (Driver)	wgpu	miki (IDevice)
资源创建	Buffer, Texture, Sampler, RenderPass, Framebuffer	✅	✅	✅	✅
Pipeline 创建	Graphics/Compute/RayTracing PSO	✅	✅	✅	via IPipelineFactory
Shader 编译/缓存	ShaderModule, PipelineCache	✅	✅	✅	🔜
Command 分配	CommandBuffer/CommandEncoder 创建	✅	✅	✅	✅
Queue 提交	Submit command buffers to GPU queue	✅	✅	✅	✅
同步原语	Fence, Semaphore, Event 创建	✅	✅	✅	🔜
内存管理	Allocator 策略 (VMA/D3D12MA)	✅	✅	内部	✅ (VMA)
Capability 查询	特性/限制/格式支持	✅	✅	✅	✅
Swap chain	创建 + Present	✅	✅	Surface	🔜
资源销毁 + 延迟回收	Deferred deletion queue	✅	✅	✅	🔜
Debug 标注	SetObjectName, 调试标签	✅	✅	✅	🔜
热重载	Pipeline/Shader 热重载	部分	❌	❌	🔜
Device 不负责的事
不归 Device	归谁
命令录制 (Draw/Dispatch/Copy)	CommandBuffer
渲染图编排 (Pass 依赖/资源转换)	RenderGraph / FrameGraph
场景管理 / Culling	Scene / Visibility 层
窗口/输入	Platform / Window 层
高层材质 / 光照	Material / Lighting 系统
关键设计模式
Factory Pattern — Device 是几乎所有 GPU 对象的唯一工厂入口。资源创建必须通过 Device，不允许裸调 vkCreateBuffer。
Deferred Deletion — GPU 资源不能立即销毁（可能仍在 flight 中）。主流做法是 Device 内置一个 per-frame deletion queue，在 fence 确认 GPU 完成后才真正释放。
Handle-based Indirection — 返回轻量 handle（而非裸指针），Device 内部维护 handle → native object 的映射池（你的 VulkanHandlePool 就是这个）。
Multi-queue Abstraction — 前沿引擎的 Device 暴露 Graphics/Compute/Transfer queue 的抽象，允许异步计算和异步传输。