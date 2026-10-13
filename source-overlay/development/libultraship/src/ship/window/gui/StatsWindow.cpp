#include "ship/window/gui/StatsWindow.h"
#include <imgui.h>
#include "spdlog/spdlog.h"

namespace Ship {
StatsWindow::~StatsWindow() {
    SPDLOG_TRACE("destruct stats window");
}

void StatsWindow::InitElement() {
}

void StatsWindow::DrawElement() {
    const float framerate = ImGui::GetIO().Framerate;
    const float deltatime = ImGui::GetIO().DeltaTime;
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

#if defined(_WIN32)
    ImGui::Text("Plataforma: Windows");
#elif defined(__IOS__)
    ImGui::Text("Plataforma: iOS");
#elif defined(__APPLE__)
    ImGui::Text("Plataforma: macOS");
#elif defined(__linux__)
    ImGui::Text("Plataforma: Linux");
#else
    ImGui::Text("Plataforma: desconocida");
#endif
    ImGui::Text("Estado: %.3f ms/cuadro (%.1f FPS)", deltatime * 1000.0f, framerate);
    ImGui::PopStyleColor();
}

void StatsWindow::UpdateElement() {
}
} // namespace Ship
