#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <luajit-2.1/lua.h>
#include <luajit-2.1/lualib.h>
#include <luajit-2.1/lauxlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Add this global variable to keep the messenger alive
VkDebugUtilsMessengerEXT g_debugMessenger = VK_NULL_HANDLE;

// The Bouncer: Silences the ghost spam
static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    // Ignore INFO and VERBOSE spam
    if (messageSeverity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        return VK_FALSE;
    }

    printf("\n[VULKAN LAYER ENFORCER]\nSEVERITY: %d\nMESSAGE: %s\n\n",
           messageSeverity, pCallbackData->pMessage);
    fflush(stdout);

    return VK_FALSE;
}

// ========================================================
// 1. GLOBALS & STATE
// ========================================================
GLFWwindow* g_window = NULL;

int g_quitting = 0;

// Define the exact struct layout that Lua will also use
typedef struct {
    float bass;
    float mid;
    float treble;
    int has_new_data;
} AudioState;

VkInstance g_instance;
VkDevice g_device;
VkQueue g_queue;
uint32_t g_qIndex;
VkSwapchainKHR g_swapchain;
uint32_t g_imageCount;
uint32_t g_width, g_height;

VkPipeline g_gfxPipeline;
VkPipelineLayout g_gfxLayout;
VkPipeline g_compPipeline;
VkPipelineLayout g_compLayout;
VkImage g_depthImage;
VkImageView g_depthView;
VkDescriptorSet g_compSet0;
VkDescriptorSet g_compSet1;

VkImage g_swapchainImages[10];
VkImageView g_swapchainViews[10];
VkBuffer g_buf_swarm_A = VK_NULL_HANDLE;
VkBuffer g_buf_swarm_B = VK_NULL_HANDLE;
VkBuffer g_buf_cage    = VK_NULL_HANDLE;

void* g_mapped_swarm_A = NULL;
void* g_mapped_swarm_B = NULL;
void* g_mapped_cage    = NULL;

VkBuffer g_buf_swarm_cpu_A = VK_NULL_HANDLE;
VkBuffer g_buf_swarm_cpu_B = VK_NULL_HANDLE;
void* g_mapped_swarm_cpu_A = NULL;
void* g_mapped_swarm_cpu_B = NULL;

// PUSH CONSTANTS (Camera)
typedef struct {
    float viewProj[16]; // 64 bytes
} CameraPushConstants;
CameraPushConstants g_cam_pc = {0};

typedef struct {
    float dt;
    float time;
    int state;
    int push;
    int pull;
    uint32_t total_active_count;
    uint32_t offsets[4]; // 0:CpuCore, 1:Static, 2:GpuBoids, 3:GpuMeteors
    uint32_t counts[4];
} ComputePushConstants;
ComputePushConstants g_atlas_pc = {0}; // Global storage for the Lua bridge

// Indirect Draw Command Buffers (Ping-Pong)
VkBuffer g_buf_draw_cmd_A, g_buf_draw_cmd_B;
void *g_mapped_draw_cmd_A, *g_mapped_draw_cmd_B;

// --- ADD THESE FOR THE SPATIAL HASH ---
VkBuffer g_buf_grid_A = VK_NULL_HANDLE;
VkBuffer g_buf_grid_B = VK_NULL_HANDLE;

// ========================================================
// HYBRID ENGINE STATE (Managed by Lua)
// ========================================================
int g_force_draw_buffer = -1; // -1 = GPU Ping-Pong, 0 = Force SwarmA, 1 = Force SwarmB
uint32_t g_draw_count = 100000;
uint32_t g_vertex_count = 24; // Default to Tetrahedron

// Compute Shader Push Constants
float g_comp_dt = 0.016f;
float g_comp_time = 0.0f;
int g_comp_state = 1;
int g_comp_push = 0;
int g_comp_pull = 0;
float g_comp_bass = 0.0f, g_comp_mid = 0.0f, g_comp_treble = 0.0f;

// CROSS-PLATFORM SOCKETS
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif

SOCKET g_udp_socket = INVALID_SOCKET;
struct sockaddr_in g_peer_addr = {0};

// ========================================================
// 2. INPUT CALLBACKS (Triggered by OS)
// ========================================================
static double g_last_mouse_x = 0.0;
static double g_last_mouse_y = 0.0;
static int g_first_mouse = 1;

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    lua_State* L = (lua_State*)glfwGetWindowUserPointer(window);

    if (g_first_mouse) {
        g_last_mouse_x = xpos;
        g_last_mouse_y = ypos;
        g_first_mouse = 0;
    }

    double dx = xpos - g_last_mouse_x;
    double dy = ypos - g_last_mouse_y;
    g_last_mouse_x = xpos;
    g_last_mouse_y = ypos;

    lua_getglobal(L, "love_mousemoved");
    if (lua_isfunction(L, -1)) {
        lua_pushnumber(L, xpos);
        lua_pushnumber(L, ypos);
        lua_pushnumber(L, dx);
        lua_pushnumber(L, dy);
        if (lua_pcall(L, 4, 0, 0) != LUA_OK) {
            printf("[LUA ERROR] %s\n", lua_tostring(L, -1));
        }
    } else {
        lua_pop(L, 1);
    }
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    if (width == 0 || height == 0) return;

    lua_State* L = (lua_State*)glfwGetWindowUserPointer(window);
    if (!L) return;

    lua_getglobal(L, "love_resize_trigger");
    if (lua_isfunction(L, -1)) {
        lua_pushinteger(L, width);
        lua_pushinteger(L, height);
        if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
            printf("[LUA ERROR in resize]: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS || action == GLFW_RELEASE) {
        lua_State* L = (lua_State*)glfwGetWindowUserPointer(window);
        if (!L) return;

        const char* func_name = (action == GLFW_PRESS) ? "love_keypressed" : "love_keyreleased";

        lua_getglobal(L, func_name);
        if (lua_isfunction(L, -1)) {
            lua_pushinteger(L, key);
            if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                printf("[LUA ERROR in keyboard]: %s\n", lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        } else {
            lua_pop(L, 1);
        }
    }
}

static int l_signal_quit(lua_State* L) {
    g_quitting = 1;
    return 0;
}
static int l_quit_engine(lua_State* L) {
    glfwSetWindowShouldClose(g_window, GLFW_TRUE);
    return 0;
}

// ========================================================
// 3. LUA FFI BRIDGE FUNCTIONS
// ========================================================
static int l_inject_validation_layers(lua_State* L) {
    VkInstance instance = (VkInstance)(uintptr_t)strtoull(lua_tostring(L, 1), NULL, 10);

    VkDebugUtilsMessengerCreateInfoEXT createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = vulkan_debug_callback;
    createInfo.pUserData = NULL;

    PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

    if (func != NULL) {
        func(instance, &createInfo, NULL, &g_debugMessenger);
        printf("[BOOT] Validation Layer Enforcer injected successfully.\n");
    } else {
        printf("[BOOT] Failed to load vkCreateDebugUtilsMessengerEXT!\n");
    }

    return 0;
}

static int l_set_core_handles(lua_State* L) {
    g_device     = (VkDevice)(uintptr_t)strtoull(lua_tostring(L, 1), NULL, 10);
    g_queue      = (VkQueue)(uintptr_t)strtoull(lua_tostring(L, 2), NULL, 10);
    g_qIndex     = (uint32_t)lua_tointeger(L, 3);
    g_swapchain  = (VkSwapchainKHR)(uintptr_t)strtoull(lua_tostring(L, 4), NULL, 10);
    g_imageCount = (uint32_t)lua_tointeger(L, 5);
    g_width      = (uint32_t)lua_tointeger(L, 6);
    g_height     = (uint32_t)lua_tointeger(L, 7);

    printf("[C BRIDGE] Rebuilt VkDevice: %p\n", (void*)g_device); fflush(stdout);
    return 0;
}

static int l_set_pipeline_handles(lua_State* L) {
    g_gfxPipeline  = (VkPipeline)strtoull(lua_tostring(L, 1), NULL, 10);
    g_gfxLayout    = (VkPipelineLayout)strtoull(lua_tostring(L, 2), NULL, 10);
    g_compPipeline = (VkPipeline)strtoull(lua_tostring(L, 3), NULL, 10);
    g_compLayout   = (VkPipelineLayout)strtoull(lua_tostring(L, 4), NULL, 10);
    g_depthImage   = (VkImage)strtoull(lua_tostring(L, 5), NULL, 10);
    g_depthView    = (VkImageView)strtoull(lua_tostring(L, 6), NULL, 10);
    g_compSet0     = (VkDescriptorSet)strtoull(lua_tostring(L, 7), NULL, 10);
    g_compSet1     = (VkDescriptorSet)strtoull(lua_tostring(L, 8), NULL, 10);
    return 0;
}

static int l_set_swapchain_asset(lua_State* L) {
    uint32_t index = (uint32_t)lua_tointeger(L, 1);
    if (index < 10) {
        g_swapchainImages[index] = (VkImage)strtoull(lua_tostring(L, 2), NULL, 10);
        g_swapchainViews[index]  = (VkImageView)strtoull(lua_tostring(L, 3), NULL, 10);
    }
    return 0;
}

static int l_submit_buffers(lua_State* L) {
    if (lua_gettop(L) < 14) { // CHANGED TO 14
        printf("[FATAL] l_submit_buffers expected 14 string arguments!\n");
        return 0;
    }

    // Swarm Data Buffers
    g_buf_swarm_cpu_A = (VkBuffer)strtoull(lua_tostring(L, 1), NULL, 10);
    g_buf_swarm_cpu_B = (VkBuffer)strtoull(lua_tostring(L, 2), NULL, 10);
    g_buf_swarm_A     = (VkBuffer)strtoull(lua_tostring(L, 3), NULL, 10);
    g_buf_swarm_B     = (VkBuffer)strtoull(lua_tostring(L, 4), NULL, 10);

    g_mapped_swarm_cpu_A = (void*)strtoull(lua_tostring(L, 5), NULL, 10);
    g_mapped_swarm_cpu_B = (void*)strtoull(lua_tostring(L, 6), NULL, 10);
    g_mapped_swarm_A     = (void*)strtoull(lua_tostring(L, 7), NULL, 10);
    g_mapped_swarm_B     = (void*)strtoull(lua_tostring(L, 8), NULL, 10);

    // THE NEW PING-PONG INDIRECT BUFFERS
    g_buf_draw_cmd_A    = (VkBuffer)strtoull(lua_tostring(L, 9), NULL, 10);
    g_buf_draw_cmd_B    = (VkBuffer)strtoull(lua_tostring(L, 10), NULL, 10);
    g_mapped_draw_cmd_A = (void*)strtoull(lua_tostring(L, 11), NULL, 10);
    g_mapped_draw_cmd_B = (void*)strtoull(lua_tostring(L, 12), NULL, 10);

    // THE TEMPORAL GRIDS (Arguments 13 & 14)
    g_buf_grid_A = (VkBuffer)strtoull(lua_tostring(L, 13), NULL, 10);
    g_buf_grid_B = (VkBuffer)strtoull(lua_tostring(L, 14), NULL, 10);

    printf("[C BRIDGE] All 14 GPU Buffers (including Temporal Grids) locked.\n");
    return 0;
}

static int l_set_vertex_count(lua_State* L) {
    g_vertex_count = (uint32_t)luaL_checkinteger(L, 1);
    return 0;
}

static int l_setCameraMatrix(lua_State* L) {
    for (int i = 0; i < 16; i++) {
        g_cam_pc.viewProj[i] = (float)lua_tonumber(L, i + 1);
    }
    return 0;
}

static int l_isKeyDown(lua_State* L) {
    int key = luaL_checkinteger(L, 1);
    lua_pushboolean(L, glfwGetKey(g_window, key) == GLFW_PRESS);
    return 1;
}

static int l_isMouseDown(lua_State* L) {
    int button = luaL_checkinteger(L, 1);
    int state = glfwGetMouseButton(g_window, button - 1);
    lua_pushboolean(L, state == GLFW_PRESS);
    return 1;
}

static int l_setRelativeMode(lua_State* L) {
    int enable = lua_toboolean(L, 1);
    glfwSetInputMode(g_window, GLFW_CURSOR, enable ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (enable) {
        double x, y;
        glfwGetCursorPos(g_window, &x, &y);
        g_last_mouse_x = x;
        g_last_mouse_y = y;
        g_first_mouse = 0;
    }
    return 0;
}

static int l_get_glfw_extensions(lua_State* L) {
    uint32_t count = 0;
    const char** exts = glfwGetRequiredInstanceExtensions(&count);
    lua_pushlightuserdata(L, (void*)exts);
    lua_pushinteger(L, count);
    return 2;
}

static int l_create_surface(lua_State* L) {
    uintptr_t addr = (uintptr_t)luaL_checknumber(L, 1);
    VkInstance instance = (VkInstance)addr;
    VkSurfaceKHR surface;
    VkResult res = glfwCreateWindowSurface(instance, g_window, NULL, &surface);

    if (res != VK_SUCCESS) {
        printf("FATAL: GLFW Failed to create Vulkan Surface! Error code: %d\n", res);
        exit(-1);
    }

    lua_pushnumber(L, (lua_Number)(uintptr_t)surface);
    return 1;
}

static int l_get_window_size(lua_State* L) {
    int width, height;
    glfwGetFramebufferSize(g_window, &width, &height);
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);
    return 2;
}

static int l_set_fullscreen(lua_State* L) {
    int enable = lua_toboolean(L, 1);
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    if (enable) {
        glfwSetWindowMonitor(g_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        int win_w = 1280;
        int win_h = 720;
        int pos_x = (mode->width - win_w) / 2;
        int pos_y = (mode->height - win_h) / 2;
        glfwSetWindowMonitor(g_window, NULL, pos_x, pos_y, win_w, win_h, GLFW_DONT_CARE);
    }
    return 0;
}

static int l_net_host(lua_State* L) {
    int port = luaL_checkinteger(L, 1);
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
#endif
    g_udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    u_long mode = 1; ioctlsocket(g_udp_socket, FIONBIO, &mode);
#else
    fcntl(g_udp_socket, F_SETFL, O_NONBLOCK);
#endif
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(g_udp_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("[NETWORK] Failed to bind to port %d\n", port);
        lua_pushboolean(L, 0);
        return 1;
    }
    printf("[NETWORK] Hosting UDP on port %d\n", port);
    lua_pushboolean(L, 1);
    return 1;
}

static int l_net_join(lua_State* L) {
    const char* ip = luaL_checkstring(L, 1);
    int port = luaL_checkinteger(L, 2);
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
#endif
    g_udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    u_long mode = 1; ioctlsocket(g_udp_socket, FIONBIO, &mode);
#else
    fcntl(g_udp_socket, F_SETFL, O_NONBLOCK);
#endif
    g_peer_addr.sin_family = AF_INET;
    g_peer_addr.sin_port = htons(port);
    g_peer_addr.sin_addr.s_addr = inet_addr(ip);

    printf("[NETWORK] Ready to blast UDP to %s:%d\n", ip, port);
    lua_pushboolean(L, 1);
    return 1;
}

static int l_net_poll(lua_State* L) {
    if (g_udp_socket == INVALID_SOCKET) return 0;
    AudioState* state = (AudioState*)lua_topointer(L, 1);
    if (!state) return 0;

    char buffer[1024];
    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);
    int bytes = 0;
    int latest_bytes = -1;
    char latest_buffer[1024];

    while ((bytes = recvfrom(g_udp_socket, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&sender, &sender_len)) > 0) {
        latest_bytes = bytes;
        memcpy(latest_buffer, buffer, bytes);
        if (g_peer_addr.sin_port == 0) {
            g_peer_addr = sender;
        }
    }

    if (latest_bytes > 0) {
        latest_buffer[latest_bytes] = '\0';
        if (sscanf(latest_buffer, "%f,%f,%f", &state->bass, &state->mid, &state->treble) == 3) {
            state->has_new_data = 1;
        }
    }
    return 0;
}

static int l_net_send(lua_State* L) {
    if (g_udp_socket == INVALID_SOCKET || g_peer_addr.sin_port == 0) return 0;
    const char* msg = luaL_checkstring(L, 1);
    sendto(g_udp_socket, msg, strlen(msg), 0, (struct sockaddr*)&g_peer_addr, sizeof(g_peer_addr));
    return 0;
}

static int l_debug_particle(lua_State* L) {
    int index = luaL_checkinteger(L, 1);
    if (!g_mapped_swarm_B) {
        lua_pushnil(L);
        return 1;
    }
    float* swarm = (float*)g_mapped_swarm_B;
    int offset = index * 4;
    lua_pushnumber(L, swarm[offset + 0]);
    lua_pushnumber(L, swarm[offset + 1]);
    lua_pushnumber(L, swarm[offset + 2]);
    return 3;
}

static int l_set_compute_push_constants(lua_State* L) {
    g_atlas_pc.dt                 = (float)luaL_checknumber(L, 1);
    g_atlas_pc.time               = (float)luaL_checknumber(L, 2);
    g_atlas_pc.state              = (int)luaL_checkinteger(L, 3);
    g_atlas_pc.push               = (int)luaL_checkinteger(L, 4);
    g_atlas_pc.pull               = (int)luaL_checkinteger(L, 5);
    
    g_atlas_pc.total_active_count = (uint32_t)luaL_checkinteger(L, 6);
    
    g_atlas_pc.offsets[0]         = (uint32_t)luaL_checkinteger(L, 7);
    g_atlas_pc.counts[0]          = (uint32_t)luaL_checkinteger(L, 8);
    g_atlas_pc.offsets[1]         = (uint32_t)luaL_checkinteger(L, 9);
    g_atlas_pc.counts[1]          = (uint32_t)luaL_checkinteger(L, 10);
    g_atlas_pc.offsets[2]         = (uint32_t)luaL_checkinteger(L, 11);
    g_atlas_pc.counts[2]          = (uint32_t)luaL_checkinteger(L, 12);
    g_atlas_pc.offsets[3]         = (uint32_t)luaL_checkinteger(L, 13);
    g_atlas_pc.counts[3]          = (uint32_t)luaL_checkinteger(L, 14);
    return 0;
}

static int l_set_active_buffer(lua_State* L) {
    g_force_draw_buffer = (int)luaL_checkinteger(L, 1);
    return 0;
}

static int l_set_draw_count(lua_State* L) {
    g_draw_count = (uint32_t)luaL_checkinteger(L, 1);
    return 0;
}

// ========================================================
// 4. MAIN ENTRY POINT
// ========================================================
int main() {
    printf("[BOOT] Starting Naked Bootloader...\n");

    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

    g_window = glfwCreateWindow(mode->width, mode->height, "VibeEngine - Cooking Dish", monitor, NULL);

    if (!g_window) {
        printf("[FATAL] Window creation failed!\n");
        glfwTerminate();
        return -1;
    }

    g_last_mouse_x = mode->width / 2.0;
    g_last_mouse_y = mode->height / 2.0;
    g_first_mouse = 1;

    glfwSetCursorPosCallback(g_window, cursor_position_callback);
    glfwSetFramebufferSizeCallback(g_window, framebuffer_size_callback);
    glfwSetKeyCallback(g_window, key_callback);

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    glfwSetWindowUserPointer(g_window, L);

    lua_newtable(L);

    lua_pushcfunction(L, l_quit_engine); lua_setfield(L, -2, "quit");
    lua_pushcfunction(L, l_signal_quit); lua_setfield(L, -2, "signal_quit");
    lua_pushcfunction(L, l_get_glfw_extensions); lua_setfield(L, -2, "get_glfw_extensions");
    lua_pushcfunction(L, l_create_surface);      lua_setfield(L, -2, "create_surface");
    lua_pushcfunction(L, l_get_window_size);     lua_setfield(L, -2, "getWindowSize");
    lua_pushcfunction(L, l_set_fullscreen);      lua_setfield(L, -2, "setFullscreen");
    lua_pushcfunction(L, l_submit_buffers);      lua_setfield(L, -2, "submit_buffers");
    lua_pushcfunction(L, l_set_core_handles);    lua_setfield(L, -2, "set_core_handles");
    lua_pushcfunction(L, l_set_pipeline_handles);lua_setfield(L, -2, "set_pipeline_handles");
    lua_pushcfunction(L, l_set_swapchain_asset); lua_setfield(L, -2, "set_swapchain_asset");

    lua_pushcfunction(L, l_net_host);            lua_setfield(L, -2, "net_host");
    lua_pushcfunction(L, l_net_join);            lua_setfield(L, -2, "net_join");
    lua_pushcfunction(L, l_net_poll);            lua_setfield(L, -2, "net_poll");
    lua_pushcfunction(L, l_net_send);            lua_setfield(L, -2, "net_send");

    lua_pushcfunction(L, l_setCameraMatrix);     lua_setfield(L, -2, "setCameraMatrix");
    lua_pushcfunction(L, l_isKeyDown);           lua_setfield(L, -2, "isKeyDown");
    lua_pushcfunction(L, l_isMouseDown);         lua_setfield(L, -2, "isMouseDown");
    lua_pushcfunction(L, l_setRelativeMode);     lua_setfield(L, -2, "setRelativeMode");

    lua_pushcfunction(L, l_debug_particle); lua_setfield(L, -2, "debug_particle");

    lua_pushcfunction(L, l_set_compute_push_constants); lua_setfield(L, -2, "set_compute_push_constants");
    lua_pushcfunction(L, l_set_active_buffer);          lua_setfield(L, -2, "set_active_buffer");
    lua_pushcfunction(L, l_set_draw_count);             lua_setfield(L, -2, "set_draw_count");
    lua_pushcfunction(L, l_set_vertex_count);           lua_setfield(L, -2, "set_vertex_count");

    lua_pushcfunction(L, l_inject_validation_layers); lua_setfield(L, -2, "inject_validation_layers");

    lua_setglobal(L, "C_Bridge");

    if (luaL_dofile(L, "main.lua") != LUA_OK) {
        printf("[LUA FATAL]: %s\n", lua_tostring(L, -1));
        return -1;
    }

    lua_getglobal(L, "love_load");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            printf("[LUA FATAL ERROR]: %s\n", lua_tostring(L, -1));
            exit(-1);
        }
    } else {
        lua_pop(L, 1);
    }

    printf("[BOOT] Entering Main Loop...\n");
    fflush(stdout);

    // ========================================================
    // THE NAKED DISPATCHER
    // ========================================================
    printf("[C DEBUG] 0. Extracting raw AMD driver functions...\n"); fflush(stdout);

    #define LOAD_VK(func) PFN_##func pfn_##func = (PFN_##func)vkGetDeviceProcAddr(g_device, #func); \
                          if (!pfn_##func) { printf("[FATAL] Missing %s\n", #func); fflush(stdout); return -1; }

    LOAD_VK(vkCreateCommandPool);
    LOAD_VK(vkAllocateCommandBuffers);
    LOAD_VK(vkCreateSemaphore);
    LOAD_VK(vkCreateFence);
    LOAD_VK(vkWaitForFences);
    LOAD_VK(vkResetFences);
    LOAD_VK(vkAcquireNextImageKHR);
    LOAD_VK(vkResetCommandBuffer);
    LOAD_VK(vkBeginCommandBuffer);
    LOAD_VK(vkCmdBindPipeline);
    LOAD_VK(vkCmdBindDescriptorSets);
    LOAD_VK(vkCmdPushConstants);
    LOAD_VK(vkCmdDispatch);
    LOAD_VK(vkCmdPipelineBarrier);
    LOAD_VK(vkCmdSetViewport);
    LOAD_VK(vkCmdSetScissor);
    LOAD_VK(vkCmdBindVertexBuffers);
    LOAD_VK(vkCmdDraw);
    LOAD_VK(vkCmdDrawIndirect);
    LOAD_VK(vkCmdFillBuffer); // <--- ADDED
    LOAD_VK(vkEndCommandBuffer);
    LOAD_VK(vkQueueSubmit);
    LOAD_VK(vkQueuePresentKHR);
    LOAD_VK(vkDestroyCommandPool);
    LOAD_VK(vkDestroySemaphore);
    LOAD_VK(vkDestroyFence);
    LOAD_VK(vkDeviceWaitIdle);

    PFN_vkCmdBeginRenderingKHR pfn_vkCmdBeginRendering = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(g_device, "vkCmdBeginRendering");
    if (!pfn_vkCmdBeginRendering) pfn_vkCmdBeginRendering = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(g_device, "vkCmdBeginRenderingKHR");

    PFN_vkCmdEndRenderingKHR pfn_vkCmdEndRendering = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(g_device, "vkCmdEndRendering");
    if (!pfn_vkCmdEndRendering) pfn_vkCmdEndRendering = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(g_device, "vkCmdEndRenderingKHR");

    if (!pfn_vkCmdBeginRendering || !pfn_vkCmdEndRendering) {
        printf("[FATAL] Dynamic Rendering functions not found on device!\n"); fflush(stdout);
        return -1;
    }

    #define MAX_FRAMES_IN_FLIGHT 2

    printf("[C DEBUG] 1. Creating Command Pool...\n"); fflush(stdout);
    VkCommandPoolCreateInfo poolInfo = {0};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = g_qIndex;

    VkCommandPool commandPool;
    pfn_vkCreateCommandPool(g_device, &poolInfo, NULL, &commandPool);

    printf("[C DEBUG] 2. Allocating Command Buffers...\n"); fflush(stdout);
    VkCommandBufferAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT];
    pfn_vkAllocateCommandBuffers(g_device, &allocInfo, commandBuffers);

    printf("[C DEBUG] 3. Creating Sync Objects...\n"); fflush(stdout);

    #define MAX_SWAPCHAIN_IMAGES 10

    VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT];
    VkFence inFlightFences[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore renderFinishedSemaphores[MAX_SWAPCHAIN_IMAGES];

    VkSemaphoreCreateInfo semInfo = {0};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {0};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        pfn_vkCreateSemaphore(g_device, &semInfo, NULL, &imageAvailableSemaphores[i]);
        pfn_vkCreateFence(g_device, &fenceInfo, NULL, &inFlightFences[i]);
    }

    for (int i = 0; i < MAX_SWAPCHAIN_IMAGES; i++) {
        pfn_vkCreateSemaphore(g_device, &semInfo, NULL, &renderFinishedSemaphores[i]);
    }

    uint32_t frameIndex = 0;
    double startTime = glfwGetTime();

    printf("[C DEBUG] 4. Launching the Render Loop!\n"); fflush(stdout);

    double last_time = glfwGetTime();
    while (!glfwWindowShouldClose(g_window)&& !g_quitting) {
        glfwPollEvents();

        uint32_t currentFrame = frameIndex % MAX_FRAMES_IN_FLIGHT;

        pfn_vkWaitForFences(g_device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

        double current_time = glfwGetTime();
        double dt = current_time - last_time;
        last_time = current_time;

        int can_render = 0;
        lua_getglobal(L, "love_update");
        if (lua_isfunction(L, -1)) {
            lua_pushnumber(L, dt);
            lua_pushinteger(L, currentFrame);
            if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
                printf("[LUA FATAL ERROR in love_update]: %s\n", lua_tostring(L, -1));
                break;
            }
            can_render = lua_toboolean(L, -1);
            lua_pop(L, 1);
        }

        if (!can_render) {
            continue;
        }

        uint32_t imageIndex;
        VkResult res = pfn_vkAcquireNextImageKHR(g_device, g_swapchain, UINT64_MAX,
                                  imageAvailableSemaphores[currentFrame],
                                  VK_NULL_HANDLE, &imageIndex);

        if (res == VK_ERROR_OUT_OF_DATE_KHR) {
            lua_getglobal(L, "love_resize_trigger");
            if (lua_isfunction(L, -1)) {
                int w, h; glfwGetFramebufferSize(g_window, &w, &h);
                lua_pushinteger(L, w);
                lua_pushinteger(L, h);
                lua_pcall(L, 2, 0, 0);
            } else { lua_pop(L, 1); }
            continue;
        } else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
            continue;
        }

        pfn_vkResetFences(g_device, 1, &inFlightFences[currentFrame]);

        VkCommandBuffer cmd = commandBuffers[currentFrame];
        pfn_vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo beginInfo = {0};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        pfn_vkBeginCommandBuffer(cmd, &beginInfo);

        void* current_mapped_cmd = (currentFrame == 0) ? g_mapped_draw_cmd_A : g_mapped_draw_cmd_B;
        VkBuffer current_buf_cmd = (currentFrame == 0) ? g_buf_draw_cmd_A : g_buf_draw_cmd_B;

        if (current_mapped_cmd) {
            uint32_t* draw_data = (uint32_t*)current_mapped_cmd;
            draw_data[0] = g_vertex_count;
            draw_data[1] = 0;
            draw_data[2] = 0;
            draw_data[3] = 0;
        }

        if (g_force_draw_buffer == -1) {
            // ========================================================
            // THE TEMPORAL GRID CLEAR (Execute before Compute!)
            // ========================================================
            VkBuffer future_grid = (frameIndex % 2 == 0) ? g_buf_grid_A : g_buf_grid_B;

            if (future_grid != VK_NULL_HANDLE) {
                // Blast the future grid to 0 using raw DMA transfer
                pfn_vkCmdFillBuffer(cmd, future_grid, 0, VK_WHOLE_SIZE, 0);

                // We MUST barrier the fill before the Compute Shader fires!
                VkMemoryBarrier fillBarrier = {0};
                fillBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

                pfn_vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 1, &fillBarrier, 0, NULL, 0, NULL);
            }

            // ========================================================
            // PASS A: COMPUTE SHADER (Simulation / Decoration)
            // ========================================================
            pfn_vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_compPipeline);

            VkDescriptorSet currentSet = (frameIndex % 2 == 0) ? g_compSet0 : g_compSet1;
            pfn_vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_compLayout, 0, 1, &currentSet, 0, NULL);

            pfn_vkCmdPushConstants(cmd, g_compLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &g_atlas_pc);

            uint32_t groupCount = (g_atlas_pc.total_active_count + 255) / 256;
            if (groupCount == 0) groupCount = 1;
            pfn_vkCmdDispatch(cmd, groupCount, 1, 1);

            VkMemoryBarrier memBarrier = {0};
            memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            memBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

            pfn_vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                0, 1, &memBarrier, 0, NULL, 0, NULL);
        }

        // ----------------------------------------------------
        // PASS B: GRAPHICS SHADER (DYNAMIC RENDERING)
        // ----------------------------------------------------
        VkImageMemoryBarrier imgBarrier = {0};
        imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imgBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imgBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imgBarrier.image = g_swapchainImages[imageIndex];
        imgBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imgBarrier.subresourceRange.levelCount = 1;
        imgBarrier.subresourceRange.layerCount = 1;
        imgBarrier.srcAccessMask = 0;
        imgBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkImageMemoryBarrier depthBarrier = {0};
        depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.image = g_depthImage;
        depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthBarrier.subresourceRange.levelCount = 1;
        depthBarrier.subresourceRange.layerCount = 1;
        depthBarrier.srcAccessMask = 0;
        depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkImageMemoryBarrier barriers[] = {imgBarrier, depthBarrier};

        pfn_vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                             0, 0, NULL, 0, NULL, 2, barriers);

        VkRenderingAttachmentInfoKHR colorAttachment = {0};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
        colorAttachment.imageView = g_swapchainViews[imageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkClearValue clearColor = {{{0.01f, 0.01f, 0.02f, 1.0f}}};
        colorAttachment.clearValue = clearColor;

        VkRenderingAttachmentInfoKHR depthAttachment = {0};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
        depthAttachment.imageView = g_depthView;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        VkClearValue depthClear = {0};
        depthClear.depthStencil.depth = 0.0f;
        depthAttachment.clearValue = depthClear;

        VkRenderingInfoKHR renderInfo = {0};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
        renderInfo.renderArea.extent.width = g_width;
        renderInfo.renderArea.extent.height = g_height;
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttachment;
        renderInfo.pDepthAttachment = &depthAttachment;

        pfn_vkCmdBeginRendering(cmd, &renderInfo);
        pfn_vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_gfxPipeline);

        VkViewport viewport = {0.0f, 0.0f, (float)g_width, (float)g_height, 0.0f, 1.0f};
        pfn_vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = {{0, 0}, {g_width, g_height}};
        pfn_vkCmdSetScissor(cmd, 0, 1, &scissor);

        pfn_vkCmdPushConstants(cmd, g_gfxLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(CameraPushConstants), &g_cam_pc);
        VkDeviceSize offsets[] = {0};

        // CURRENT (INCORRECT): Rendering the same buffer that Compute is writing to!
        VkBuffer unifiedBuffer = (frameIndex % 2 == 0) ? g_buf_swarm_A : g_buf_swarm_B;
        // CORRECT: Render the buffer that Compute IS NOT currently touching.
        // Even frame (0): Compute writes to A, so we render B.
        // Odd frame (1): Compute writes to B, so we render A.
        // VkBuffer unifiedBuffer = (frameIndex % 2 == 0) ? g_buf_swarm_B : g_buf_swarm_A;

        pfn_vkCmdBindVertexBuffers(cmd, 0, 1, &unifiedBuffer, offsets);

        pfn_vkCmdDrawIndirect(cmd, current_buf_cmd, 0, 1, 16);

        pfn_vkCmdEndRendering(cmd);

        imgBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imgBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        imgBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        imgBarrier.dstAccessMask = 0;

        pfn_vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, NULL, 0, NULL, 1, &imgBarrier);

        pfn_vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo = {0};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailableSemaphores[currentFrame];

        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinishedSemaphores[imageIndex];

        pfn_vkQueueSubmit(g_queue, 1, &submitInfo, inFlightFences[currentFrame]);

        VkPresentInfoKHR presentInfo = {0};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphores[imageIndex];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &g_swapchain;
        presentInfo.pImageIndices = &imageIndex;

        pfn_vkQueuePresentKHR(g_queue, &presentInfo);

        frameIndex++;
    }

    pfn_vkDeviceWaitIdle(g_device);

    printf("[TEARDOWN] Cleaning up C-side Sync Objects and Command Pool...\n");

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        pfn_vkDestroySemaphore(g_device, imageAvailableSemaphores[i], NULL);
        pfn_vkDestroyFence(g_device, inFlightFences[i], NULL);
    }

    for (int i = 0; i < MAX_SWAPCHAIN_IMAGES; i++) {
        if (renderFinishedSemaphores[i] != VK_NULL_HANDLE) {
            pfn_vkDestroySemaphore(g_device, renderFinishedSemaphores[i], NULL);
        }
    }

    pfn_vkDestroyCommandPool(g_device, commandPool, NULL);

    lua_getglobal(L, "love_quit");
    if (lua_isfunction(L, -1)) {
        lua_pcall(L, 0, 0, 0);
    }

    glfwDestroyWindow(g_window);
    glfwTerminate();
    lua_close(L);
    return 0;
}
