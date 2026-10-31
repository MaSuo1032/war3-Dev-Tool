#include "WebControllerServer.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <stdexcept>

static void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

static void Put32(uintptr_t address, uint32_t value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}

struct MemoryFixture {
    uintptr_t base;
    uintptr_t presence;
    uintptr_t table;
    uintptr_t alternate;
    uintptr_t game;
    uintptr_t player;
    uintptr_t gold;
    uintptr_t lumber;
    MemoryFixture() {
        base = reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr, 0xC00000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        Require(base != 0, "allocate fixture");
        presence = base + 0x1000;
        table = base + 0x2000;
        alternate = base + 0x3000;
        game = base + 0x4000;
        player = base + 0x5000;
        gold = base + 0x6000;
        lumber = base + 0x7000;
        Put32(base + WC3_127A::THIS_GAME_RVA, presence);
        Put32(base + WC3_127A::UNIT_LIST_RVA, game);
        Put32(presence + 0x0C, table);
        Put32(presence + 0x1C, 4);
        Put32(presence + 0x2C, alternate);
        Put32(presence + 0x3C, 4);
        Put32(game + WC3_127A::PLAYER_TABLE_OFF, player);
        Put32(player + 0x58, 1);
        Put32(player + 0x5C, 101);
        Put32(player + 0x68, 0x80000002u);
        Put32(player + 0x6C, 102);
        Put32(table + 8, 0xFFFFFFFEu);
        Put32(table + 12, gold);
        Put32(alternate + 16, 0xFFFFFFFEu);
        Put32(alternate + 20, lumber);
        Put32(gold + 0x18, 101);
        Put32(lumber + 0x18, 102);
        Put32(gold + 0x78, 12340);
        Put32(lumber + 0x78, 56780);
    }
    ~MemoryFixture() { VirtualFree(reinterpret_cast<void*>(base), 0, MEM_RELEASE); }
};

static SOCKET Connect(int port) {
    SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Require(socketHandle != INVALID_SOCKET, "client socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<u_short>(port));
    Require(connect(socketHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != SOCKET_ERROR, "client connect");
    DWORD timeout = 4000;
    setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
    return socketHandle;
}

static std::string Request(int port, const std::string& method, const std::string& path, const std::string& body = "", bool fragmented = false) {
    SOCKET client = Connect(port);
    const std::string headers = method + " " + path + " HTTP/1.1\r\nHost: localhost\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    Require(send(client, headers.data(), static_cast<int>(headers.size()), 0) == static_cast<int>(headers.size()), "send headers");
    if (fragmented) Sleep(50);
    if (!body.empty()) Require(send(client, body.data(), static_cast<int>(body.size()), 0) == static_cast<int>(body.size()), "send body");
    std::string response;
    char buffer[4096];
    for (;;) {
        int count = recv(client, buffer, sizeof(buffer), 0);
        if (count <= 0) break;
        response.append(buffer, count);
    }
    closesocket(client);
    return response;
}

int main() {
    try {
        MemoryFixture fixture;
        ProcessMemory memory(GetCurrentProcess());
        WC3Resolver resolver(memory, fixture.base);
        ResourceInfo resources{};
        Require(resolver.inspectResources(1, &resources), "resolve resource tags in both tables");
        Require(resources.gold.address == fixture.gold + 0x78 && resources.lumber.address == fixture.lumber + 0x78, "resource addresses must come from player tags");
        int value = 0;
        Require(memory.read(resources.gold.address, &value) && value == 12340, "gold value");
        int32_t wrongRef = 999;
        Require(resolver.gameMemoryAddress(1, &wrongRef) == 0, "stale reference must fail");
        Require(resolver.gameMemoryAddress(4) == 0, "table count bounds");
        Require(resolver.gameMemoryAddress(-1) == 0, "invalid presence tag");
        Put32(fixture.table + 8, 0);
        Require(!resolver.inspectResources(1, &resources), "inactive slot must fail");
        Put32(fixture.table + 8, 0xFFFFFFFEu);
        Require(!resolver.inspectResources(0, &resources) && !resolver.inspectResources(13, &resources), "player bounds");

        using namespace WebController;
        InitMemoryController();
        g_memProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, GetCurrentProcessId());
        Require(g_memProcess != nullptr, "open fixture process");
        g_memGameBase = fixture.base;
        g_memPid = GetCurrentProcessId();
        g_memInstance.setHandle(g_memProcess);
        g_memResolver.setGameBase(fixture.base);

        WSADATA wsa{};
        Require(WSAStartup(MAKEWORD(2, 2), &wsa) == 0, "winsock");
        SOCKET occupied = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Require(bind(occupied, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "reserve test port");
        int addressSize = sizeof(address);
        Require(getsockname(occupied, reinterpret_cast<sockaddr*>(&address), &addressSize) == 0, "test port number");
        int port = ntohs(address.sin_port);
        Require(!StartWebControllerServer(port), "occupied port must report failure");
        closesocket(occupied);
        Require(StartWebControllerServer(port), "server start");
        Require(Request(port, "GET", "/").find("</html>") != std::string::npos, "complete HTML response");
        Require(Request(port, "POST", "/api/write", "{\"field\": \"gold\", \"value\": 4321, \"player\": 1}", true).find("\"success\":true") != std::string::npos, "fragmented POST body");
        Require(memory.read(fixture.gold + 0x78, &value) && value == 43210, "resource write target and scaling");
        Require(Request(port, "POST", "/api/write", "{\"field\":\"gold\",\"player\":1}").find("\"success\":false") != std::string::npos, "missing numeric field must fail");
        Require(memory.read(fixture.gold + 0x78, &value) && value == 43210, "malformed write must not change memory");
        Require(HandleApiWriteItem("{\"slot\":0}") == "{\"success\":false}", "missing charges must fail");
        SOCKET idle = Connect(port);
        Sleep(20);
        const ULONGLONG start = GetTickCount64();
        StopWebControllerServer();
        Require(GetTickCount64() - start < 4500 && g_httpServerThread == nullptr, "idle client shutdown must finish");
        closesocket(idle);
        Require(StartWebControllerServer(port), "server restart after shutdown");
        Require(Request(port, "GET", "/api/resources").find("\"gold\":4321") != std::string::npos, "resources after restart");
        StopWebControllerServer();
        CleanupMemoryController();
        Require(g_memInstance.getHandle() == nullptr && g_memResolver.getGameBase() == 0, "cleanup clears borrowed handle/base");
        CleanupMemoryController();
        WSACleanup();
        std::puts("PASS: resource tags, stale/bounded memory, fragmented HTTP, malformed writes, port collision, idle shutdown and restart");
        return 0;
    } catch (const std::exception& error) {
        WebController::StopWebControllerServer();
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
