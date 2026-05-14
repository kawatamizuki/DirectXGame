#include "DebugEditor.h"
#include"Renderer.h"
#include"TimeManager.h"
#include"InputManager.h"


#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"


bool DebugEditor::Initialize(HWND hwnd, GameContext* context)
{
    if (!hwnd || !context || !context->renderer)
    {
        return false;
    }
  
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX11_Init(
        context->renderer->GetDevice(),
        context->renderer->GetContext()
    );

    m_context = context;


    return true;
}

void DebugEditor::BeginFrame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void DebugEditor::Draw()
{
    ImGui::SetNextWindowSize(ImVec2(260, 120), ImGuiCond_FirstUseEver);
    ImGui::Begin("Debug Editor");

    if (m_context && m_context->time)
    {
        ImGui::Text(
            "FPS : %.1f",
            m_context->time->GetFPS()
        );

        if (m_context->renderer)
        {
            bool vsync = m_context->renderer->IsVSyncEnabled();

            ImGui::Text(
                "VSync : %s",
                vsync ? "ON" : "OFF"
            );
        }
    }
    ImGui::Text("DeltaTime : %.4f", m_context->time->GetDeltaTime());

    ImGui::End();
}

void DebugEditor::EndFrame()
{
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void DebugEditor::Finalize()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}