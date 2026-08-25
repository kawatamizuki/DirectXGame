#include "DebugEditor.h"
#include"Renderer.h"
#include"TimeManager.h"
#include"InputManager.h"
#include <DirectXMath.h>


#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

DebugEditor::DebugEditor()
    : m_context(nullptr)
    , m_selectedObjectIndex(-1)
{
}

DebugEditor::~DebugEditor()
{
}

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
    if (!m_context)
    {
        return;
    }

    ImGui::SetNextWindowSize(
        ImVec2(350, 500),
        ImGuiCond_FirstUseEver
    );

    ImGui::Begin("Debug Editor");

    DrawPerformance();
    DrawObjects();
    DrawInspector();

    ImGui::End();
}

void DebugEditor::DrawPerformance()
{
    if (ImGui::CollapsingHeader("Performance"))
    {
        if (m_context->time)
        {
            ImGui::Text("FPS : %.1f", m_context->time->GetFPS());
            ImGui::Text("DeltaTime : %.4f", m_context->time->GetDeltaTime());
        }

        if (m_context->renderer)
        {
            ImGui::Text(
                "VSync : %s",
                m_context->renderer->IsVSyncEnabled() ? "ON" : "OFF"
            );
        }
    }
}

void DebugEditor::DrawObjects()
{
    if (!m_context->objects)
    {
        return;
    }

    if (ImGui::CollapsingHeader("Objects"))
    {
        int index = 0;

        for (const auto& obj : *m_context->objects)
        {
            std::string label =
                "GameObject " + std::to_string(index);

            bool selected =
                (m_selectedObjectIndex == index);

            if (ImGui::Selectable(label.c_str(), selected))
            {
                m_selectedObjectIndex = index;
            }

            ++index;
        }
    }
}

void DebugEditor::DrawInspector()
{
    if (ImGui::CollapsingHeader("Inspector"))
    {
        if (!m_context->objects ||
            m_selectedObjectIndex < 0 ||
            m_selectedObjectIndex >= static_cast<int>(m_context->objects->size()))
        {
            ImGui::Text("No object selected.");
            return;
        }

        GameObject& selectedObject =
            (*m_context->objects)[m_selectedObjectIndex];

        ImGui::DragFloat3(
            "Position",
            &selectedObject.transform.position.x,
            0.1f
        );

        float rotationDegrees[3] =
        {
            DirectX::XMConvertToDegrees(selectedObject.transform.rotation.x),
            DirectX::XMConvertToDegrees(selectedObject.transform.rotation.y),
            DirectX::XMConvertToDegrees(selectedObject.transform.rotation.z)
        };

        if (ImGui::DragFloat3(
            "Rotation",
            rotationDegrees,
            1.0f,
            -360.0f,
            360.0f))
        {
            selectedObject.transform.rotation.x =
                DirectX::XMConvertToRadians(rotationDegrees[0]);
            selectedObject.transform.rotation.y =
                DirectX::XMConvertToRadians(rotationDegrees[1]);
            selectedObject.transform.rotation.z =
                DirectX::XMConvertToRadians(rotationDegrees[2]);
        }

        ImGui::DragFloat3(
            "Scale",
            &selectedObject.transform.scale.x,
            0.01f,
            0.01f,
            100.0f
        );
    }
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