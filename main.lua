local ffi = require("ffi")
local DebugProxy = require("debug_proxy")
local vk_core = require("vulkan_core")
local camera_math = require("camera")

Engine = {
    Resize = { is_resizing = false, timer = 0.0, cooldown = 0.25, new_width = 0, new_height = 0 },
    vk_context = nil, vk_swapchain = nil, vk_graphics = nil, vk_compute = nil, vk_descriptors = nil,
    Time = 0.0,
    -- Removed DrawCount!
    SwarmState = 0,
    GravityBlend = 1.0,
    MetalBlend = 0.0,
    ParadoxBlend = 0.0,
    SpacePressedLast = false
}

-- ========================================================
-- PILLAR 2: THE AVX2 MATH LIBRARY (VibeMath)
-- ========================================================
VibeMath = ffi.load(jit.os == "Windows" and "vibemath" or "./libvibemath.so")
Config = {
    physics_mode = "HYBRID"
}

-- ========================================================
-- PILLAR 3: THE C-BRIDGE (Injected by main.c)
-- ========================================================
love = {
    keyboard = {
        isDown = function(key)
            local keymap = { w = 87, a = 65, s = 83, d = 68, q = 81, e = 69, space = 32 }
            if not keymap[key] then return false end
            return C_Bridge.isKeyDown(keymap[key])
        end
    },
    mouse = {
        getRelativeMode = function() return true end,
        isDown = function(button) return C_Bridge.isMouseDown(button) end
    }
}

-- ========================================================
-- MODULE INFECTION & SETUP
-- ========================================================
local EngineModules = {}
local modules_to_load = {"memory", "descriptors", "swapchain", "graphics_pipeline", "compute_pipeline"}
for _, mod_name in ipairs(modules_to_load) do
    EngineModules[mod_name] = DebugProxy.Infect(mod_name, require(mod_name))
end

local memory = EngineModules.memory
local descriptors = EngineModules.descriptors
local swapchain = EngineModules.swapchain
local graphics_pipeline = EngineModules.graphics_pipeline
local compute_pipeline = EngineModules.compute_pipeline

local cam_state = camera_math.create_state()
local success, vk = pcall(ffi.load, "vulkan-1")
if not success then success, vk = pcall(ffi.load, "vulkan") end

local function ptr2str(ptr)
    if ptr == nil then return "0" end
    local cdata_num = ffi.cast("uint64_t", ffi.cast("uintptr_t", ptr))
    return string.match(tostring(cdata_num), "%d+")
end

-- ========================================================
-- 4. THE REBUILD ORCHESTRATOR
-- ========================================================
local function ExecuteVulkanRebuild(width, height, is_boot)
    if not is_boot then
        print("\n[REBUILD] Halting GPU and Destroying old Swapchain/Pipelines...")
        vk.vkDeviceWaitIdle(Engine.vk_context.device)
        graphics_pipeline.Destroy(vk, Engine.vk_context, Engine.vk_graphics)
        compute_pipeline.Destroy(vk, Engine.vk_context, Engine.vk_compute)
        swapchain.Destroy(vk, Engine.vk_context, Engine.vk_swapchain)
    end

    print("[REBUILD] Building Swapchain and Pipelines...")
    Engine.vk_swapchain = swapchain.Init(vk, Engine.vk_context, width, height)
    Engine.vk_graphics = graphics_pipeline.Init(vk, Engine.vk_context, width, height)
    Engine.vk_compute = compute_pipeline.Init(vk, Engine.vk_context.device, Engine.vk_descriptors.pipelineLayout)

    C_Bridge.set_core_handles(
        ptr2str(Engine.vk_context.device), ptr2str(Engine.vk_context.queue), Engine.vk_context.qIndex,
        ptr2str(Engine.vk_swapchain.handle), Engine.vk_swapchain.imageCount, width, height
    )

    C_Bridge.set_pipeline_handles(
        ptr2str(Engine.vk_graphics.pipeline), ptr2str(Engine.vk_graphics.pipelineLayout),
        ptr2str(Engine.vk_compute.pipeline), ptr2str(Engine.vk_descriptors.pipelineLayout),
        ptr2str(Engine.vk_graphics.depthImage), ptr2str(Engine.vk_graphics.depthImageView),
        ptr2str(Engine.vk_descriptors.set0), ptr2str(Engine.vk_descriptors.set1)
    )

    for i = 0, Engine.vk_swapchain.imageCount - 1 do
        C_Bridge.set_swapchain_asset(i, ptr2str(Engine.vk_swapchain.images[i]), ptr2str(Engine.vk_swapchain.imageViews[i]))
    end
end

-- ========================================================
-- 5. STANDARD BOOT SEQUENCE
-- ========================================================
function love_load()
    print("[LUA] Booting VibeEngine...")
    Engine.vk_context = vk_core.init()

    local use_avx2 = (Config.physics_mode == "CPU_AVX2" or Config.physics_mode == "HYBRID")
    memory.Init(vk, Engine.vk_context, use_avx2)

    -- Setup Descriptors with the Quad-Buffer layout + Indirect + TEMPORAL GRIDS
    Engine.vk_descriptors = descriptors.Init(
        vk, Engine.vk_context.device,
        memory.Buffers["SwarmCPU_A"],
        memory.Buffers["SwarmCPU_B"],
        memory.Buffers["SwarmPing"],
        memory.Buffers["SwarmPong"],
        memory.Buffers["DrawCmd_A"],
        memory.Buffers["DrawCmd_B"],
        memory.Buffers["Grid_A"], -- <--- PATCHED IN
        memory.Buffers["Grid_B"]  -- <--- PATCHED IN
    )

    local win_width, win_height = C_Bridge.getWindowSize()
    ExecuteVulkanRebuild(win_width, win_height, true)

    -- CRITICAL: Keep 14 parameters so main.c doesn't crash
    C_Bridge.submit_buffers(
        ptr2str(memory.Buffers["SwarmCPU_A"]), ptr2str(memory.Buffers["SwarmCPU_B"]),
        ptr2str(memory.Buffers["SwarmPing"]), ptr2str(memory.Buffers["SwarmPong"]),
        ptr2str(memory.Mapped["SwarmCPU_A"]), ptr2str(memory.Mapped["SwarmCPU_B"]),
        ptr2str(memory.Mapped["SwarmPing"]), ptr2str(memory.Mapped["SwarmPong"]),
        ptr2str(memory.Buffers["DrawCmd_A"]), ptr2str(memory.Buffers["DrawCmd_B"]),
        ptr2str(memory.Mapped["DrawCmd_A"]), ptr2str(memory.Mapped["DrawCmd_B"]),
        ptr2str(memory.Buffers["Grid_A"]), ptr2str(memory.Buffers["Grid_B"]) -- <--- PATCHED IN
    )

    -- LEGACY BIND: We pad with nil to avoid FFI signature panics based on your current vibemath.c
    VibeMath.vmath_bind_vulkan_buffers(memory.Mapped["SwarmCPU_A"], nil, nil)
    VibeMath.vmath_bind_engine(memory.RenderStruct, nil, nil)

    -- We seed the entire active memory atlas!
    VibeMath.vmath_seed_swarm(memory.TotalActive)
    print("[INIT] VRAM Seeded with " .. tostring(memory.TotalActive) .. " Particles.")

    if use_avx2 then
        VibeMath.vmath_init_thread_pool()
        print("[INIT] AVX2 Thread Pool Online.")
    end

    C_Bridge.setRelativeMode(true)
    print("[INIT] Mouse Captured. Delta-reporting active.")
end

-- ========================================================
-- 6. OS EVENTS & UPDATE LOOP
-- ========================================================
function love_resize_trigger(w, h)
    Engine.Resize.is_resizing = true
    Engine.Resize.timer = 0.0
    Engine.Resize.new_width = w
    Engine.Resize.new_height = h
end

-- CRITICAL: Keep currentFrame in signature to prevent parity drift!
function love_update(dt, currentFrame)
    if Engine.Resize.is_resizing then
        Engine.Resize.timer = Engine.Resize.timer + dt
        if Engine.Resize.timer >= Engine.Resize.cooldown then
            ExecuteVulkanRebuild(Engine.Resize.new_width, Engine.Resize.new_height, false)
            Engine.Resize.is_resizing = false
        end
        return false
    end

    dt = math.min(dt, 0.033)
    Engine.Time = Engine.Time + dt

    -- ====================================================
    -- MANUAL SWARM CHOREOGRAPHY (Restored)
    -- ====================================================
    local space_down = love.keyboard.isDown("space")
    if space_down and not Engine.SpacePressedLast then
        Engine.SwarmState = Engine.SwarmState + 1
        if Engine.SwarmState > 6 then Engine.SwarmState = 0 end
        print("[STATE] Swarm Matrix Shifted to State: " .. Engine.SwarmState)
    end
    Engine.SpacePressedLast = space_down

    -- Smooth Morphing Blends
    if Engine.SwarmState == 0 then Engine.GravityBlend = math.min(1.0, Engine.GravityBlend + dt * 2.0)
    else Engine.GravityBlend = math.max(0.0, Engine.GravityBlend - dt * 2.0) end

    if Engine.SwarmState == 5 then Engine.MetalBlend = math.min(1.0, Engine.MetalBlend + dt * 0.5)
    else Engine.MetalBlend = math.max(0.0, Engine.MetalBlend - dt * 2.0) end

    if Engine.SwarmState == 6 then Engine.ParadoxBlend = math.min(1.0, Engine.ParadoxBlend + dt * 0.5)
    else Engine.ParadoxBlend = math.max(0.0, Engine.ParadoxBlend - dt * 2.0) end

    local push_active = love.mouse.isDown(1) and 1 or 0
    local pull_active = love.mouse.isDown(2) and 1 or 0

    local mem = memory.RenderStruct
    mem.Swarm_State = Engine.SwarmState
    mem.Swarm_GravityBlend = Engine.GravityBlend
    mem.Swarm_MetalBlend = Engine.MetalBlend
    mem.Swarm_ParadoxBlend = Engine.ParadoxBlend

    -- ====================================================
    -- HARDWARE SYNCHRONIZED TRAFFIC COP
    -- ====================================================
    -- 1. Use the hardware-locked frame from C!
    local active_cpu_idx = currentFrame
    local target_cpu_mapped = (active_cpu_idx == 0) and memory.Mapped["SwarmCPU_A"] or memory.Mapped["SwarmCPU_B"]

    -- 2. Legacy Bind (Ignoring Heterogenous Feedback buffers)
    VibeMath.vmath_bind_vulkan_buffers(target_cpu_mapped, nil, nil)

    -- 1. The CPU only processes its assigned slice count!
    VibeMath.vmath_step_swarm(memory.Atlas.CpuCore.count, Engine.Time, dt, Engine.SwarmState, push_active, pull_active)

    if Config.physics_mode == "HYBRID" then
        -- 2. Push the entire Memory Atlas to the GPU! (14 Arguments)
        C_Bridge.set_compute_push_constants(
            dt, Engine.Time, Engine.SwarmState, push_active, pull_active,
            memory.TotalActive,
            memory.Atlas.CpuCore.offset, memory.Atlas.CpuCore.count,
            memory.Atlas.Static.offset, memory.Atlas.Static.count,
            memory.Atlas.GpuBoids.offset, memory.Atlas.GpuBoids.count,
            memory.Atlas.GpuMeteors.offset, memory.Atlas.GpuMeteors.count
        )

        C_Bridge.set_active_buffer(-1)
    elseif Config.physics_mode == "CPU_AVX2" then
        C_Bridge.set_active_buffer(2)
    end

    camera_math.apply_movement(cam_state, dt)
    camera_math.build_matrix(cam_state, Engine.vk_swapchain.extent.width, Engine.vk_swapchain.extent.height)
    C_Bridge.setCameraMatrix(unpack(cam_state.mat))

    -- 3. Update the Rasterizer's global vertex bounds to match the Atlas
    C_Bridge.set_draw_count(memory.TotalActive)
    -- C_Bridge.set_vertex_count(3) -- Set to 3 for triangles if performance lags!
    C_Bridge.set_vertex_count(1)
    return true
end

function love_mousemoved(x, y, dx, dy)
    camera_math.apply_look(cam_state, dx, dy)
end

function love_keypressed(key)
    if key == 256 then
        print("[LUA] Escape detected. Signaling C to stop the loop...")
        C_Bridge.signal_quit()
    elseif key == 72 then
        if Config.physics_mode == "HYBRID" then
            Config.physics_mode = "CPU_AVX2"
            print("[ENGINE] Mode Swapped: PURE CPU (Raw Smale's Paradox)")
        else
            Config.physics_mode = "HYBRID"
            print("[ENGINE] Mode Swapped: HYBRID (CPU Paradox + GPU Turbulence)")
        end
    elseif key == 71 then -- 'G' Key
        -- Spawn 50,000 Newborn GPU Boids dynamically!
        memory.Atlas.GpuBoids.count = memory.Atlas.GpuBoids.count + 50000

        -- Recalculate TotalActive (Simplistic rebuild of the atlas total)
        memory.TotalActive = memory.Atlas.CpuCore.count + memory.Atlas.Static.count +
                             memory.Atlas.GpuBoids.count + memory.Atlas.GpuMeteors.count

        print("[SPAWN] Newborn GPU Boids added! Total Active: " .. memory.TotalActive)
    end
end

function love_quit()
    print("\n[SHUTDOWN] Initiating Safe Teardown...")
    local device = Engine.vk_context.device
    vk.vkDeviceWaitIdle(device)

    graphics_pipeline.Destroy(vk, Engine.vk_context, Engine.vk_graphics)
    compute_pipeline.Destroy(vk, Engine.vk_context, Engine.vk_compute)
    swapchain.Destroy(vk, Engine.vk_context, Engine.vk_swapchain)

    if descriptors.Destroy then
        descriptors.Destroy(vk, device, Engine.vk_descriptors)
    end

    memory.Destroy(vk, Engine.vk_context)
    vk_core.Destroy(vk, Engine.vk_context)
end
