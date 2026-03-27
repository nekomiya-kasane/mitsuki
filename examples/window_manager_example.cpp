/** @brief Window Manager Example — demonstrates basic window creation and event handling.
 *
 * Creates a window using GLFW backend, processes events until close.
 */

#include <cstdio>
#include <thread>
#include <chrono>

// Forward declarations for simplified example
namespace miki {
    namespace rhi {
        enum class BackendType { Vulkan };
    }
    namespace platform {
        struct WindowHandle { uint32_t id = 0; };
        struct WindowDesc { const char* title = "Window"; uint32_t width = 1280; uint32_t height = 720; };
        class WindowManager {
        public:
            static bool Create() { return true; }
            WindowHandle CreateWindow(const WindowDesc& desc) { 
                printf("Creating window: %s (%dx%d)\n", desc.title, desc.width, desc.height);
                return WindowHandle{1}; 
            }
            bool ShouldClose() const { return false; }
            void PollEvents() { }
            void DestroyWindow(WindowHandle handle) { 
                printf("Destroying window: %d\n", handle.id);
            }
        };
    }
    namespace demo {
        class GlfwWindowBackend {
        public:
            GlfwWindowBackend(miki::rhi::BackendType type, bool visible) {
                printf("GLFW Backend created (type=%d, visible=%d)\n", 
                       static_cast<int>(type), visible);
            }
        };
    }
}

int main() {
    printf("=== Miki Window Manager Example ===\n");
    
    // Create GLFW backend
    auto backend = std::make_unique<miki::demo::GlfwWindowBackend>(
        miki::rhi::BackendType::Vulkan, 
        true  // visible
    );
    
    // Create window manager
    if (!miki::platform::WindowManager::Create()) {
        printf("Failed to create window manager\n");
        return -1;
    }
    
    miki::platform::WindowManager windowManager;
    
    // Create a window
    auto window = windowManager.CreateWindow({
        .title = "Miki Window Manager Example",
        .width = 1280,
        .height = 720
    });
    
    printf("Window created successfully! Handle: %u\n", window.id);
    
    // Simulate main loop for 3 seconds
    printf("Running main loop for 3 seconds...\n");
    for (int i = 0; i < 3; ++i) {
        printf("Frame %d\n", i + 1);
        
        // Poll events
        windowManager.PollEvents();
        
        // Sleep for ~16ms (60 FPS)
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    
    // Cleanup
    windowManager.DestroyWindow(window);
    printf("Window closed. Exiting...\n");
    
    return 0;
}
