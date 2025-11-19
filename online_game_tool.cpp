#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <map>
#include <algorithm>
#include <cstring>
#include <boost/asio.hpp>
#include <memory>
#include "tcp_server.h"
#include "tcp/tcp_client.h"
#include "steam/steam_networking_manager.h"

using boost::asio::ip::tcp;

// New variables for multiple connections and TCP clients
std::vector<HSteamNetConnection> connections;
std::map<HSteamNetConnection, std::shared_ptr<TCPClient>> clientMap;
std::mutex clientMutex;
std::mutex connectionsMutex;  // Add mutex for connections
int localPort = 0;
std::unique_ptr<TCPServer> server;

int main() {
    boost::asio::io_context io_context;

    // Initialize Steam Networking Manager
    SteamNetworkingManager steamManager;
    if (!steamManager.initialize()) {
        std::cerr << "Failed to initialize Steam Networking Manager" << std::endl;
        return 1;
    }

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        steamManager.shutdown();
        return -1;
    }

    // Create window
    GLFWwindow* window = glfwCreateWindow(1280, 720, "在线游戏工具", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        SteamAPI_Shutdown();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    // Load Chinese font
    io.Fonts->AddFontFromFileTTF("font.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    ImGui::StyleColorsDark();

    // Initialize ImGui backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Set message handler dependencies
    steamManager.setMessageHandlerDependencies(io_context, clientMap, clientMutex, server, localPort);
    steamManager.startMessageHandler();

    // Steam Networking variables
    bool isHost = false;
    bool isClient = false;
    char joinBuffer[256] = "";
    char filterBuffer[256] = "";

    // Lambda to render invite friends UI
    auto renderInviteFriends = [&]() {
        ImGui::InputText("过滤朋友", filterBuffer, IM_ARRAYSIZE(filterBuffer));
        ImGui::Text("朋友:");
        for (const auto& friendPair : steamManager.getFriendsList()) {
            std::string nameStr = friendPair.second;
            std::string filterStr(filterBuffer);
            // Convert to lowercase for case-insensitive search
            std::transform(nameStr.begin(), nameStr.end(), nameStr.begin(), ::tolower);
            std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);
            if (filterStr.empty() || nameStr.find(filterStr) != std::string::npos) {
                ImGui::PushID(friendPair.first.ConvertToUint64());
                if (ImGui::Button(("邀请 " + friendPair.second).c_str())) {
                    // Send invite via Steam with lobby ID as connect string
                    std::string connectStr = std::to_string(steamManager.getCurrentLobby().ConvertToUint64());
                    // Safety check for SteamFriends
                    if (SteamFriends()) {
                        SteamFriends()->InviteUserToGame(friendPair.first, connectStr.c_str());
                        std::cout << "Sent invite to " << friendPair.second << " with connect string: " << connectStr << std::endl;
                    } else {
                        std::cerr << "SteamFriends() is null! Cannot send invite." << std::endl;
                    }
                }
                ImGui::PopID();
            }
        }
    };

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        // Poll events
        glfwPollEvents();

        // Run Steam callbacks
        SteamAPI_RunCallbacks();

        // Update Steam networking info
        steamManager.update();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Create a window for online game tool
        ImGui::Begin("在线游戏工具");
        if (server) {
            ImGui::Text("TCP服务器监听端口8888");
            ImGui::Text("已连接客户端: %d", server->getClientCount());
        }
        ImGui::Separator();

        if (!steamManager.isHost() && !steamManager.isConnected()) {
            if (ImGui::Button("主持游戏房间")) {
                steamManager.startHosting();
            }
            if (ImGui::Button("搜索游戏房间")) {
                steamManager.searchLobbies();
            }
            ImGui::InputText("主机Steam ID", joinBuffer, IM_ARRAYSIZE(joinBuffer));
            if (ImGui::Button("加入游戏房间")) {
                uint64 hostID = std::stoull(joinBuffer);
                if (steamManager.joinHost(hostID)) {
                    // Start TCP Server
                    server = std::make_unique<TCPServer>(8888, &steamManager);
                    if (!server->start()) {
                        std::cerr << "Failed to start TCP server" << std::endl;
                    }
                }
            }
            // Display available lobbies
            if (!steamManager.getLobbies().empty()) {
                ImGui::Text("可用房间:");
                for (const auto& lobbyID : steamManager.getLobbies()) {
                    std::string lobbyName = "房间 " + std::to_string(lobbyID.ConvertToUint64());
                    if (ImGui::Button(lobbyName.c_str())) {
                        steamManager.joinLobby(lobbyID);
                    }
                }
            }
        }
        if (steamManager.isHost()) {
            ImGui::Text("正在主持游戏房间。邀请朋友！");
            ImGui::Separator();
            ImGui::InputInt("本地端口", &localPort);
            ImGui::Separator();
            renderInviteFriends();
        }
        if (steamManager.isConnected() && !steamManager.isHost()) {
            ImGui::Text("已连接到游戏房间。邀请朋友！");
            ImGui::Separator();
            renderInviteFriends();
        }

        ImGui::End();

        // Room status window - only show when hosting or joined
        if (steamManager.isHost() || steamManager.isClient()) {
            ImGui::Begin("房间状态");
            if (server) {
                ImGui::Text("房间内玩家: %d", server->getClientCount() + 1); // +1 for host
            }
            {
                std::lock_guard<std::mutex> lock(clientMutex);
                std::lock_guard<std::mutex> lockConn(connectionsMutex);
                ImGui::Text("连接的好友: %d", (int)steamManager.getConnections().size());
                ImGui::Text("活跃的TCP客户端: %d", (int)clientMap.size());
            }
            ImGui::Separator();
            ImGui::Text("用户列表:");
            if (ImGui::BeginTable("UserTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("名称");
                ImGui::TableSetupColumn("延迟 (ms)");
                ImGui::TableSetupColumn("连接类型");
                ImGui::TableHeadersRow();
                {
                    std::lock_guard<std::mutex> lock(clientMutex);
                    for (const auto& pair : steamManager.getUserMap()) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", pair.second.name.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", pair.second.ping);
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", pair.second.isRelay ? "中继" : "直连");
                    }
                }
                ImGui::EndTable();
            }
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Swap buffers
        glfwSwapBuffers(window);
    }

    // Stop message handler
    steamManager.stopMessageHandler();

    // Cleanup
    // Cleanup TCP Clients
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        for (auto& pair : clientMap) {
            pair.second->disconnect();
        }
        clientMap.clear();
    }
    if (server) {
        server->stop();
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    steamManager.shutdown();

    return 0;
}