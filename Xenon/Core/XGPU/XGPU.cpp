// Copyright 2025 Xenon Emulator Project

#include "XGPU.h"
#include "XGPUConfig.h"
#include "XenosRegisters.h"

#include "Base/Config.h"
#include "Base/Version.h"
#include "Base/Logging/Log.h"

Xe::Xenos::XGPU::XGPU(RAM *ram) {
  // Assign RAM Pointer
  ramPtr = ram;

  memset(&xgpuConfigSpace.data, 0xf, sizeof(GENRAL_PCI_DEVICE_CONFIG_SPACE));
  // Setup config space as per dump taken from a Jasper console.
  // Located at config address 0xD0010000.
  u8 i = 0;
  for (u16 idx = 0; idx < 256; idx += 4) {
    memcpy(&xgpuConfigSpace.data[idx], &xgpuConfigMap[i], 4);
    i++;
  }

  xenosState.Regs = new u8[0xFFFFF];
  memset(xenosState.Regs, 0, 0xFFFFF);

  // Set Clocks speeds.
  u32 reg = 0x09000000;
  memcpy(&xenosState.Regs[REG_GPU_CLK], &reg, 4);
  reg = 0x11000c00;
  memcpy(&xenosState.Regs[REG_EDRAM_CLK], &reg, 4);
  reg = 0x1a000001;
  memcpy(&xenosState.Regs[REG_FSB_CLK], &reg, 4);
  reg = 0x19100000;
  memcpy(&xenosState.Regs[REG_MEM_CLK], &reg, 4);

  if (Config::gpuThreadEnabled()) {
    renderThread = std::thread(&XGPU::XenosThread, this);
  }
  else{
      LOG_WARNING(Xenos, "Xenos Render thread disbaled in config.");
  }
}

bool Xe::Xenos::XGPU::Read(u64 readAddress, u64 *data, u8 byteCount) {
  if (isAddressMappedInBAR(static_cast<u32>(readAddress))) {
    
    const u32 regIndex = (readAddress & 0xFFFFF) / 4;

    LOG_TRACE(Xenos, "Read Addr = {:#x}, reg: {:#x}.", readAddress, regIndex);

    XeRegister reg = static_cast<XeRegister>(regIndex);

    memcpy(data, &xenosState.Regs[regIndex * 4], byteCount);
    if (regIndex == 0x00000a07)
      *data = 0x2000000;

    if (regIndex == 0x00001928)
      *data = 0x2000000;

    if (regIndex == 0x00001e54)
      *data = 0;

    return true;
  }

  return false;
}

bool Xe::Xenos::XGPU::Write(u64 writeAddress, u64 data, u8 byteCount) {
  if (isAddressMappedInBAR(static_cast<u32>(writeAddress))) {

    const u32 regIndex = (writeAddress & 0xFFFFF) / 4;

    LOG_TRACE(Xenos, "Write Addr = {:#x}, reg: {:#x}, data = {:#x}.", writeAddress, regIndex,
        _byteswap_ulong(static_cast<u32>(data)));

    XeRegister reg = static_cast<XeRegister>(regIndex);

    memcpy(&xenosState.Regs[regIndex * 4], &data, byteCount);
    return true;
  }

  return false;
}

void Xe::Xenos::XGPU::ConfigRead(u64 readAddress, u64 *data, u8 byteCount) {
  memcpy(data, &xgpuConfigSpace.data[readAddress & 0xFF], byteCount);
  return;
}

void Xe::Xenos::XGPU::ConfigWrite(u64 writeAddress, u64 data, u8 byteCount) {
  memcpy(&xgpuConfigSpace.data[writeAddress & 0xFF], &data, byteCount);
  return;
}

bool Xe::Xenos::XGPU::isAddressMappedInBAR(u32 address) {
  u32 bar0 = xgpuConfigSpace.configSpaceHeader.BAR0;
  u32 bar1 = xgpuConfigSpace.configSpaceHeader.BAR1;
  u32 bar2 = xgpuConfigSpace.configSpaceHeader.BAR2;
  u32 bar3 = xgpuConfigSpace.configSpaceHeader.BAR3;
  u32 bar4 = xgpuConfigSpace.configSpaceHeader.BAR4;
  u32 bar5 = xgpuConfigSpace.configSpaceHeader.BAR5;

  if (address >= bar0 && address <= bar0 + XGPU_DEVICE_SIZE ||
      address >= bar1 && address <= bar1 + XGPU_DEVICE_SIZE ||
      address >= bar2 && address <= bar2 + XGPU_DEVICE_SIZE ||
      address >= bar3 && address <= bar3 + XGPU_DEVICE_SIZE ||
      address >= bar4 && address <= bar4 + XGPU_DEVICE_SIZE ||
      address >= bar5 && address <= bar5 + XGPU_DEVICE_SIZE) {
    return true;
  }

  return false;
}

constexpr const char* vertexShaderSource = R"(
#version 430 core
layout (location = 0) in vec2 i_pos;
layout (location = 1) in vec2 i_texture_coord;
out vec2 o_texture_coord;
void main() {
  gl_Position = vec4(i_pos, 0.0, 1.0);
  o_texture_coord = i_texture_coord;
}
)";
constexpr const char* fragmentShaderSource = R"(
#version 430 core
in vec2 o_texture_coord;
out vec4 o_color;
uniform usampler2D u_texture;

void main() {
  uint pixel = texture(u_texture, o_texture_coord).r; // Read packed 32-bit BGRA value
  float b = float((pixel >> 0)  & 0xFF) / 255.0;
  float g = float((pixel >> 8)  & 0xFF) / 255.0;
  float r = float((pixel >> 16) & 0xFF) / 255.0;
  float a = float((pixel >> 24) & 0xFF) / 255.0;
  o_color = vec4(r, g, b, a);
}
)";
// Vali: We may have to do some cursed hackry to dynamically change resolution. Whether that's recompiling the shader with placeholder text and string replacement, or passing some layout data
constexpr const char* computeShaderSource = R"(
#version 430 core

layout (local_size_x = 16, local_size_y = 16) in;
layout (r32ui, binding = 0) uniform writeonly uimage2D o_texture;
layout (std430, binding = 1) buffer pixel_buffer {
  uint pixel_data[];
};

uniform bool useXeFb;
uniform int resWidth;
uniform int resHeight;

int xeFbConvert(int resWidth, int addr)
{
    int y = addr / (resWidth * 4);
    int x = (addr % (resWidth * 4)) / 4;

    // Adjust X & Y based on console tiling format
    int tileX = (x >> 5) * 32;  // Tile-aligned X
    int tileY = (y >> 5) * resWidth; // Tile-aligned Y

    // Local offsets inside tile
    int localX = x & 3;
    int localY = y & 1;

    // Compute offset like console_pset32
    uint offset = (tileY + tileX) 
                + (localX) 
                + (localY << 2) 
                + (((x & 31) >> 2) << 3) 
                + (((y & 31) >> 1) << 6);

    offset ^= ((y & 8) << 2); // Fix interleaving

    return int(offset);
}

#define XE_PIXEL_TO_STD_ADDR(x, y) ((y * resWidth + x)) * 4
#define XE_PIXEL_TO_XE_ADDR(x, y) xeFbConvert(resWidth, XE_PIXEL_TO_STD_ADDR(x, y))

void main() {
  ivec2 texel_pos = ivec2(gl_GlobalInvocationID.xy);
  if (texel_pos.x >= resWidth || texel_pos.y >= resHeight)
    return;
  int flippedX = resWidth - texel_pos.x - 1;
  int flippedY = resHeight - texel_pos.y - 1;

  int index = useXeFb ? XE_PIXEL_TO_XE_ADDR(texel_pos.x, texel_pos.y) 
                      : XE_PIXEL_TO_STD_ADDR(texel_pos.x, texel_pos.y);

  if (index < 0 || index >= (resWidth * resHeight)) return;
  
  uint packedColor = pixel_data[index / 4]; 
  uint debugColor = ((texel_pos.x % 16) < 8) ? 0xFFFFFFFF : 0xFF000000;
  imageStore(o_texture, texel_pos, uvec4(packedColor, 0, 0, 0));
}
)";

void compileShader(GLuint shader, const char* source) {
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int success;
    char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
      glGetShaderInfoLog(shader, 512, NULL, infoLog);
      LOG_ERROR(System, "Failed to initialize SDL video subsystem: {}", infoLog);
    }
}

GLuint createShaderProgram(const char* vertex, const char* fragment) {
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    compileShader(vertexShader, vertex);
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    compileShader(fragmentShader, fragment);
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return program;
}

static inline int xeFbConvert(const int resWidth, const int addr) {
  const int y = addr / (resWidth * 4);
  const int x = addr % (resWidth * 4) / 4;
  const u64 offset =
      ((((y & ~31) * resWidth) + (x & ~31) * 32) +
       (((x & 3) + ((y & 1) << 2) + ((x & 28) << 1) + ((y & 30) << 5)) ^
        ((y & 8) << 2))) *
      4;
  return offset;
}

#define XE_PIXEL_TO_STD_ADDR(x, y) (y * resWidth + x) * 4
#define XE_PIXEL_TO_XE_ADDR(x, y)                                              \
  xeFbConvert(resWidth, XE_PIXEL_TO_STD_ADDR(x, y))

void ConvertFramebufferCPU(std::vector<uint32_t>& outputBuffer, uint8_t* xeFramebuffer, int resWidth, int resHeight) {
  int stdPixPos = 0;
  int xePixPos = 0;
  for (int x = 0; x < resWidth; x++) {
      for (int y = 0; y < resHeight; y++) {
          if (x < 5 && y < 5) { 
            printf("X: %d, Y: %d -> Std: %d, XE: %d\n", x, y, stdPixPos, xePixPos); 
          }
          int flippedY = resHeight - y - 1;
          
          stdPixPos = XE_PIXEL_TO_STD_ADDR(x, flippedY);
          xePixPos = XE_PIXEL_TO_XE_ADDR(x, y);
          // Ensure within bounds
          if (xePixPos + 3 >= resWidth * resHeight * 4) continue;
          // Copy pixel data
          uint8_t b = xeFramebuffer[xePixPos];      // Blue
          uint8_t g = xeFramebuffer[xePixPos + 1];  // Green
          uint8_t r = xeFramebuffer[xePixPos + 2];  // Red
          uint8_t a = xeFramebuffer[xePixPos + 3];  // Alpha
          // Store in OpenGL format (RGBA)
          outputBuffer[stdPixPos / 4] = COLOR(b, g, r, a);
      }
  }
}

void Xe::Xenos::XGPU::XenosThread() {
  // TODO(bitsh1ft3r):
  // Change resolution/window size according to current AVPACK, that is
  // according to corresponding registers inside Xenos.

  // TODO(Xphalnos):
  // Find a way to change the internal resolution without crashing the display.
  // Window Resolution.
  const u32 resWidth = 1280;
  const u32 resHeight = 720;

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    LOG_ERROR(System, "Failed to initialize SDL video subsystem: {:#x}", SDL_GetError());
  }

  //	Set the title.
  std::string TITLE = "Xenon " + std::string(Base::VERSION);

  //	SDL3 window properties.
  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING,
                        std::string(TITLE).c_str());
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER,
                        SDL_WINDOWPOS_CENTERED);
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER,
                        SDL_WINDOWPOS_CENTERED);
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,
                        Config::windowWidth());
  SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER,
                        Config::windowHeight());
  //	Only putting this back when a Vulkan implementation is done.
  //	SDL_SetNumberProperty(props, "flags", SDL_WINDOW_VULKAN);
  SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
  SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);
  mainWindow = SDL_CreateWindowWithProperties(props);
  SDL_DestroyProperties(props);

  SDL_SetWindowMinimumSize(mainWindow, 640, 480);

  // Set OpenGL SDL Properties
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
  // Set RGBA size (R8G8B8A8)
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
  // Set OpenGL version to 4.3 (earliest with CS)
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  // We aren't using compatibility profile
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  // Create OpenGL handle for SDL
  context = SDL_GL_CreateContext(mainWindow);
  if (!context) {
    LOG_ERROR(System, "Failed to create OpenGL context: {:#x}", SDL_GetError());
  }

  // Init GLAD
  if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
    LOG_ERROR(System, "Failed to initialize OpenGL Loader");
  }

  // Init shader handles
  GLuint computeShader = glCreateShader(GL_COMPUTE_SHADER);
  compileShader(computeShader, computeShaderSource);
  shaderProgram = glCreateProgram();
  glAttachShader(shaderProgram, computeShader);
  glLinkProgram(shaderProgram);
  glDeleteShader(computeShader);
  renderShaderProgram = createShaderProgram(vertexShaderSource, fragmentShaderSource);

  // Init GL texture
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, resWidth, resHeight); 
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

  // Init pixel buffer 
  int pitch = resWidth * resHeight;
  std::vector<uint32_t> pixels(pitch, COLOR(0, 0, 0, 255)); // Init with black
	glGenBuffers(1, &pixelBuffer);
  glUniform1i(glGetUniformLocation(shaderProgram, "useXeFb"), GL_FALSE);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, pixelBuffer);
	glBufferData(GL_SHADER_STORAGE_BUFFER, pixels.size() * sizeof(uint32_t), pixels.data(), GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, pixelBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  // Init the fullscreen quad
  constexpr float quadVertices[]{
    // Positions   Texture coords
    -1.0f, -1.0f,  0.0f, 0.0f,
    1.0f , -1.0f,  1.0f, 0.0f,
    1.0f ,  1.0f,  1.0f, 1.0f,
    -1.0f,  1.0f,  0.0f, 1.0f
  };
  // Bind the VAO and VBO for our quad
  glGenVertexArrays(1, &quadVAO);
  glGenBuffers(1, &quadVBO);
  glBindVertexArray(quadVAO);
  glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
  // Set shader attributes
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
  glEnableVertexAttribArray(1);
  // Set clear color
  glClearColor(0.7f, 0.7f, 0.7f, 1.f);
  glViewport(0, 0, resWidth, resHeight);
  glDisable(GL_BLEND);
  //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);

  // Framebuffer pointer from main memory.
  u8 *fbPointer = ramPtr->getPointerToAddress(XE_FB_BASE);
  // Rendering Mode.
  bool rendering = true;
  // VSYNC Mode.
  bool VSYNC = true;
  // Set VSYNC mode to default.
  SDL_GL_SetSwapInterval((int)VSYNC);
  // Fullscreen Mode.
  SDL_SetWindowFullscreen(mainWindow, Config::fullscreenMode());

  while (rendering) {
    // Process events.
    while (SDL_PollEvent(&windowEvent)) {
      switch (windowEvent.type) {
      case SDL_EVENT_QUIT:
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(mainWindow);
        if (Config::quitOnWindowClosure()) {
          SDL_Quit();
          exit(0);
        } 
        rendering = false;       
        break;
      case SDL_EVENT_KEY_DOWN:
        if (windowEvent.key.key == SDLK_F5) {
          SDL_GL_SetSwapInterval((int)!VSYNC);
          LOG_INFO(Xenos, "RenderWindow: Setting Vsync to: {0:#b}", VSYNC);
          VSYNC = !VSYNC;
        }
        if (windowEvent.key.key == SDLK_F11) {
          SDL_WindowFlags flag = SDL_GetWindowFlags(mainWindow);
          bool fullscreenMode = flag & SDL_WINDOW_FULLSCREEN;
          SDL_SetWindowFullscreen(mainWindow, !fullscreenMode);
        }
        break;
      default:
        break;
      }
    }

    //ConvertFramebufferCPU(pixels, fbPointer, resWidth, resHeight);

    // Upload corrected buffer
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, pixelBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, resWidth * resHeight * sizeof(uint32_t), fbPointer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Use the compute shader
    glUseProgram(shaderProgram);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, pixelBuffer);
    glUniform1i(glGetUniformLocation(shaderProgram, "useXeFb"), GL_TRUE);
    glUniform1i(glGetUniformLocation(shaderProgram, "resWidth"), resWidth);
    glUniform1i(glGetUniformLocation(shaderProgram, "resHeight"), resHeight);
    glDispatchCompute(resWidth / 16, resHeight / 16, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // Render the texture
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(renderShaderProgram);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    SDL_GL_SwapWindow(mainWindow);
  }
}
