#include "App.h"

#include <chrono>
#include <thread>

const Atlas::EngineConfig Atlas::EngineInstance::engineConfig = {
    .assetDirectory = "../../../data",
    .shaderDirectory = "shader",
    .validationLayerSeverity = Log::SEVERITY_MEDIUM,
};

void App::LoadContent(AppConfiguration config) {

    this->config = config;

    renderTarget = Atlas::RenderTarget(1920, 1080);
    pathTraceTarget = Atlas::Renderer::PathTracerRenderTarget(1920, 1080);

    auto icon = Atlas::Texture::Texture2D("icon.png");
    window.SetIcon(&icon);

    loadingTexture = Atlas::CreateRef<Atlas::Texture::Texture2D>("loading.png");

    font = Atlas::Font("font/roboto.ttf", 22, 5);

    camera = Atlas::Camera(47.0f, 2.0f, 1.0f, 400.0f,
        glm::vec3(30.0f, 25.0f, 0.0f), glm::vec2(-3.14f / 2.0f, 0.0f));

    scene = Atlas::CreateRef<Atlas::Scene::Scene>(glm::vec3(-2048.0f), glm::vec3(2048.0f));

    mouseHandler = Atlas::Input::MouseHandler(&camera, 1.5f, 6.0f);
    keyboardHandler = Atlas::Input::KeyboardHandler(&camera, 7.0f, 6.0f);

    Atlas::Events::EventManager::KeyboardEventDelegate.Subscribe(
        [this](Atlas::Events::KeyboardEvent event) {
            if (event.keyCode == AE_KEY_ESCAPE) {
                Exit();
            }
            if (event.keyCode == AE_KEY_F11 && event.state == AE_BUTTON_RELEASED) {
                renderUI = !renderUI;
            }
            if (event.keyCode == AE_KEY_LSHIFT && event.state == AE_BUTTON_PRESSED) {
                keyboardHandler.speed = cameraSpeed * 4.0f;
            }
            if (event.keyCode == AE_KEY_LSHIFT && event.state == AE_BUTTON_RELEASED) {
                keyboardHandler.speed = cameraSpeed;
            }
        });
    
    Atlas::PipelineManager::EnableHotReload();

    directionalLight = Atlas::CreateRef<Atlas::Lighting::DirectionalLight>(AE_MOVABLE_LIGHT);
    directionalLight->direction = glm::vec3(0.0f, -1.0f, 0.33f);
    directionalLight->intensity = 100.0f;
    directionalLight->color = glm::vec3(255, 236, 209) / 255.0f;
    glm::mat4 orthoProjection = glm::ortho(-100.0f, 100.0f, -70.0f, 120.0f, -120.0f, 120.0f);
    directionalLight->AddShadow(200.0f, 3.0f, 4096, glm::vec3(0.0f), orthoProjection);
    directionalLight->AddVolumetric(10, 0.28f);

    scene->sky.sun = directionalLight;

    scene->ao = Atlas::CreateRef<Atlas::Lighting::AO>(16);

    if (config.reflection) {
        scene->reflection = Atlas::CreateRef<Atlas::Lighting::Reflection>(1);
        scene->reflection->useShadowMap = true;
    }

    if (config.ddgi) {
        scene->irradianceVolume = std::make_shared<Atlas::Lighting::IrradianceVolume>(
            Atlas::Volume::AABB(glm::vec3(-100.0f), glm::vec3(100.0f)), glm::ivec3(20));
        scene->irradianceVolume->SetRayCount(128, 32);
        scene->irradianceVolume->strength = 1.5f;
    }

    if (config.ssgi) {
        scene->ssgi = Atlas::CreateRef<Atlas::Lighting::SSGI>();
    }

    if (config.fog) {
        scene->fog = Atlas::CreateRef<Atlas::Lighting::Fog>();
        scene->fog->enable = true;
        scene->fog->density = 0.0068f;
        scene->fog->heightFalloff = 0.0284f;
        scene->fog->height = 0.0f;
    }

    if (config.clouds) {
        scene->sky.clouds = Atlas::CreateRef<Atlas::Lighting::VolumetricClouds>();
        scene->sky.clouds->minHeight = 1400.0f;
        scene->sky.clouds->maxHeight = 1700.0f;
        scene->sky.clouds->castShadow = false;
    }

    scene->sky.atmosphere = Atlas::CreateRef<Atlas::Lighting::Atmosphere>();

    if (config.taa) {
        scene->postProcessing.taa = Atlas::PostProcessing::TAA(0.99f);
    }

    if (config.sharpen) {
        scene->postProcessing.sharpen.enable = true;
        scene->postProcessing.sharpen.factor = 0.15f;
    }

    if (config.sss) {
        scene->sss = Atlas::CreateRef<Atlas::Lighting::SSS>();
    }

    if (config.ocean) {
        scene->ocean = Atlas::CreateRef<Atlas::Ocean::Ocean>(9, 4096.0f,
            glm::vec3(0.0f, 5.0f, 0.0f), 512, 86);
    }

    if (config.exampleRenderer) {
        exampleRenderer.Init(graphicsDevice);
    }

    LoadScene();

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    imguiWrapper.Load(&window);

}

void App::UnloadContent() {

    UnloadScene();
    imguiWrapper.Unload();

}

void App::Update(float deltaTime) {

    if (sceneReload) {
        UnloadScene();
        LoadScene();
        sceneReload = false;
    }

    const ImGuiIO& io = ImGui::GetIO();

    imguiWrapper.Update(&window, deltaTime);

    if (io.WantCaptureMouse) {
        mouseHandler.lock = true;
    }
    else {
        mouseHandler.lock = false;
    }

    mouseHandler.Update(&camera, deltaTime);
    keyboardHandler.Update(&camera, deltaTime);

    if (rotateCamera) {
        camera.rotation.y += rotateCameraSpeed * cos(Atlas::Clock::Get());
        mouseHandler.Reset(&camera);
    }

    if(moveCamera) {
        camera.location += camera.right * moveCameraSpeed * cos(Atlas::Clock::Get());
        mouseHandler.Reset(&camera);
    }

    camera.UpdateView();
    camera.UpdateProjection();

    scene->Update(&camera, deltaTime);

    CheckLoadScene();

    if (frameCount > FRAME_DATA_COUNT + 1) {
        Exit();
    }

}

void App::Render(float deltaTime) {

    static bool pathTrace = false;

#ifndef AE_HEADLESS
    /*
    auto windowFlags = window.GetFlags();
    if (windowFlags & AE_WINDOW_HIDDEN || windowFlags & AE_WINDOW_MINIMIZED || !(windowFlags & AE_WINDOW_SHOWN)) {
        return;
    }
    */
#endif

    if (!loadingComplete) {
        DisplayLoadingScreen(deltaTime);
        return;
    }

    frameCount++;

    if (config.resize && frameCount == 2) {
        SetResolution(2560, 1440);
    }
    if (config.recreateSwapchain && frameCount == 2) {
        Atlas::Graphics::GraphicsDevice::DefaultDevice->CreateSwapChain();
    }
    if (config.minimizeWindow && frameCount == 2) {
        window.Minimize();
    }
    if (config.minimizeWindow && frameCount == 3) {
        window.Maximize();
    }

    if (config.exampleRenderer) {
        exampleRenderer.Render(&camera);
    }
    else if (pathTrace) {
        viewport.Set(0, 0, pathTraceTarget.GetWidth(), pathTraceTarget.GetHeight());
        mainRenderer->PathTraceScene(&viewport, &pathTraceTarget, &camera, scene.get());
    }
    else {
        mainRenderer->RenderScene(&viewport, &renderTarget, &camera, scene.get());
    }

    ImGui::NewFrame();
    ImGui::ShowDemoWindow();
    ImGui::Render();

    imguiWrapper.Render();

}

void App::DisplayLoadingScreen(float deltaTime) {

    auto commandList = graphicsDevice->GetCommandList();

    commandList->BeginCommands();
    graphicsDevice->swapChain->colorClearValue.color = {0.0f, 0.0f, 0.0f, 1.0f};
    commandList->BeginRenderPass(graphicsDevice->swapChain, true);

    auto windowSize = window.GetDrawableSize();

    float width = float(loadingTexture->width);
    float height = float(loadingTexture->height);

    float x = windowSize.x / 2 - width / 2;
    float y = windowSize.y / 2 - height / 2;

    static float rotation = 0.0f;

    rotation += deltaTime * abs(sin(Atlas::Clock::Get())) * 10.0f;

    mainRenderer->textureRenderer.RenderTexture2D(commandList, &viewport,
        loadingTexture.get(), x, y, width, height, rotation);

    float textWidth, textHeight;
    font.ComputeDimensions("Loading...", 2.0f, &textWidth, &textHeight);

    x = windowSize.x / 2 - textWidth / 2;
    y = windowSize.y / 2 - textHeight / 2 + float(loadingTexture->height) + 20.0f;

    viewport.Set(0, 0, windowSize.x, windowSize.y);
    mainRenderer->textRenderer.Render(commandList, &viewport, &font,
        "Loading...", x, y, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 2.0f);

    commandList->EndRenderPass();
    commandList->EndCommands();

    graphicsDevice->SubmitCommandList(commandList);

}

bool App::LoadScene() {

    bool successful = false;
    loadingComplete = false;

    using namespace Atlas::Loader;

    meshes.reserve(3);
    glm::mat4 transform = glm::scale(glm::mat4(1.0f), glm::vec3(.05f));
    auto sponzaMesh = Atlas::ResourceManager<Atlas::Mesh::Mesh>::GetOrLoadResourceWithLoaderAsync(
        "sponza/sponza.obj", ModelLoader::LoadMesh, false, transform, 2048
    );
    meshes.push_back(sponzaMesh);

    transform = glm::scale(glm::mat4(1.0f), glm::vec3(1.f));
    auto wallMesh = Atlas::ResourceManager<Atlas::Mesh::Mesh>::GetOrLoadResourceWithLoaderAsync(
        "metallicwall.gltf", ModelLoader::LoadMesh, Atlas::Mesh::MeshMobility::Movable,
        false, transform, 2048
    );
    meshes.push_back(wallMesh);

    transform = glm::scale(glm::mat4(1.0f), glm::vec3(1.f));
    auto sphereMesh = Atlas::ResourceManager<Atlas::Mesh::Mesh>::GetOrLoadResourceWithLoaderAsync(
        "chromesphere.gltf", ModelLoader::LoadMesh, Atlas::Mesh::MeshMobility::Movable,
        false, transform, 2048
    );
    meshes.push_back(sphereMesh);

    auto meshCount = 0;
    for (const auto& mesh : meshes) {
        if (meshCount == 10) {
            meshCount++;
            continue;
        }
        actors.push_back(Atlas::Actor::MovableMeshActor{ mesh, glm::translate(glm::mat4(1.0f),
            glm::vec3(0.0f)) });

        meshCount++;
    }

    for (auto& actor : actors) {
        scene->Add(&actor);
    }

    camera.Update();
    scene->Update(&camera, 1.0f);

    // Reset input handlers
    keyboardHandler.Reset(&camera);
    mouseHandler.Reset(&camera);

    Atlas::Clock::ResetAverage();

    return successful;

}

void App::UnloadScene() {

    for (auto& actor : actors) scene->Remove(&actor);

    actors.clear();
    meshes.clear();

    actors.shrink_to_fit();
    meshes.shrink_to_fit();

    scene->ClearRTStructures();

    graphicsDevice->WaitForIdle();
    graphicsDevice->ForceMemoryCleanup();

}

void App::CheckLoadScene() {

    if (!scene->IsFullyLoaded() || loadingComplete)
        return;

    static std::future<void> future;

    auto buildRTStructure = [&]() {
        auto sceneMeshes = scene->GetMeshes();

        for (auto& mesh : sceneMeshes) {
            mesh->BuildBVH();
        }
    };

    if (!future.valid()) {
        future = std::async(std::launch::async, buildRTStructure);
        return;
    }
    else {
        if (future.wait_for(std::chrono::microseconds(0)) != std::future_status::ready) {
            return;
        }
        future.get();
    }

    auto sceneAABB = Atlas::Volume::AABB(glm::vec3(std::numeric_limits<float>::max()),
        glm::vec3(-std::numeric_limits<float>::max()));

    auto sceneActors = scene->GetMeshActors();
    for (const auto& actor : sceneActors) {
        sceneAABB.Grow(actor->aabb);
    }

    for (const auto& mesh : meshes) {
        mesh->invertUVs = true;
        mesh->cullBackFaces = true;
    }

    Atlas::Clock::ResetAverage();

    loadingComplete = true;

}

void App::SetResolution(int32_t width, int32_t height) {

    renderTarget.Resize(width, height);
    pathTraceTarget.Resize(width, height);

}

Atlas::EngineInstance* GetEngineInstance() {

    return new App();

}
