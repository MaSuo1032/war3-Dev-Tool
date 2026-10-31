#define WAR3DEVTOOL_HOOK_TEST
#include "../src/JassHook.cpp"
#include <iostream>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

int __cdecl OriginalNative(int a, int b, int c) { return a + b * 10 + c * 100; }
int __cdecl AlternateNative(int a, int b, int c) { return a + b + c; }
static void* registeredHandler = nullptr;
void __fastcall RecordRegistration(void* handler, const char*, const char*) { registeredHandler = handler; }

// Exercise all four possible x86 stack alignments. The thunk must restore the
// exact ESP and preserve EBX/ESI/EDI before reaching the actual native.
__declspec(naked) int InvokeAligned(void* target, unsigned padding, DWORD* registers) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov eax, [ebp + 12]
        sub esp, eax
        mov ebx, 11223344h
        mov esi, 55667788h
        mov edi, 12345678h
        push 3
        push 2
        push 1
        call dword ptr [ebp + 8]
        add esp, 12
        mov edx, [ebp + 16]
        mov [edx], ebx
        mov [edx + 4], esi
        mov [edx + 8], edi
        lea esp, [ebp - 12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

void TestThunks() {
    g_nativeCount = 0;
    int index = FindOrAddNative("DzRegression", "(III)I", reinterpret_cast<void*>(OriginalNative));
    CHECK(index == 0);
    void* thunk = CreateThunk(index, reinterpret_cast<void*>(OriginalNative));
    CHECK(thunk && thunk != reinterpret_cast<void*>(OriginalNative));
    for (unsigned padding = 0; padding < 16; padding += 4) {
        DWORD registers[3] = {};
        CHECK(InvokeAligned(thunk, padding, registers) == 321);
        CHECK(registers[0] == 0x11223344 && registers[1] == 0x55667788 && registers[2] == 0x12345678);
    }
    CHECK(CreateThunk(index, reinterpret_cast<void*>(OriginalNative)) == thunk);
    BYTE saved[kThunkStride]; memcpy(saved, thunk, sizeof(saved));
    CHECK(CreateThunk(index, reinterpret_cast<void*>(AlternateNative)) == reinterpret_cast<void*>(AlternateNative));
    CHECK(memcmp(saved, thunk, sizeof(saved)) == 0);
    CHECK(CreateThunk(-1, reinterpret_cast<void*>(OriginalNative)) == reinterpret_cast<void*>(OriginalNative));
    CHECK(CreateThunk(kNativeCapacity, reinterpret_cast<void*>(OriginalNative)) == reinterpret_cast<void*>(OriginalNative));
}

void TestRegistryBounds() {
    g_nativeCount = 0;
    for (int pass = 0; pass < 4; ++pass) {
        for (int i = 0; i < kNativeCapacity + 50; ++i) {
            char name[64]; sprintf_s(name, "DzNative%d", i);
            int index = FindOrAddNative(name, "(III)I", reinterpret_cast<void*>(OriginalNative));
            CHECK(index == (i < kNativeCapacity ? i : -1));
            CHECK(g_nativeCount <= kNativeCapacity);
        }
    }
    CHECK(g_nativeCount == kNativeCapacity);
    CHECK(FindOrAddNative(nullptr, "()V", reinterpret_cast<void*>(OriginalNative)) == -1);
    OriginalRegisterNative = RecordRegistration;
    HookedRegisterNative(reinterpret_cast<void*>(OriginalNative), "DzOverflow", "(III)I");
    CHECK(registeredHandler == reinterpret_cast<void*>(OriginalNative));
    CHECK(g_nativeCount == kNativeCapacity);
}

void TestGuardedStringsAndPluginIdentity() {
    char copied[16] = "sentinel";
    CHECK(!CopyJassString(0x10001, copied, sizeof(copied)));
    CHECK(copied[0] == '\0');
    CHECK(!CopyJassString(42, copied, sizeof(copied)));
    DWORD text = CreateFakeJassString("abcdefghijklmnopqrstuvwxyz");
    CHECK(CopyJassString(text, copied, sizeof(copied)));
    CHECK(strcmp(copied, "abcdefghijklmno") == 0);
    FakeJassString* fake = reinterpret_cast<FakeJassString*>(text);
    delete[] fake->data->chars;
    delete fake->data;
    delete fake;

    BYTE headers[1024] = {};
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(headers);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 128;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(headers + 128);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->FileHeader.TimeDateStamp = kDzPluginTimeDateStamp;
    nt->OptionalHeader.SizeOfImage = kDzPluginSizeOfImage;
    nt->OptionalHeader.CheckSum = kDzPluginCheckSum;
    CHECK(MatchesPluginRegistry(reinterpret_cast<HMODULE>(headers)));
    ++nt->FileHeader.TimeDateStamp;
    CHECK(!MatchesPluginRegistry(reinterpret_cast<HMODULE>(headers)));
}

void TestInstructionBoundariesAndGateway() {
    const BYTE gameEntry[] = {0x55,0x8b,0xec,0x81,0xec,0x0c,0x01,0x00,0x00};
    const BYTE saveEntry[] = {0x55,0x8b,0xec,0x51,0x8b,0x55,0x0c};
    const BYTE pluginEntry[] = {0x55,0x8b,0xec,0x6a,0xff};
    const BYTE unsupported[] = {0xeb,0xfe,0x90,0x90,0x90};
    CHECK(X86PatchSpan(gameEntry) == 9);
    CHECK(X86PatchSpan(saveEntry) == 7);
    CHECK(X86PatchSpan(pluginEntry) == 5);
    CHECK(X86PatchSpan(unsupported) == 0);

    // A small independent native. Installing its hook and calling its gateway
    // tests the real allocation, protection and instruction-cache path.
    BYTE native[] = {0x55,0x8b,0xec,0xb8,0x2a,0x00,0x00,0x00,0x5d,0xc3};
    BYTE* memory = static_cast<BYTE*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    CHECK(memory != nullptr);
    if (!memory) return;
    memcpy(memory, native, sizeof(native));
    typedef int (__cdecl *Function)();
    CHECK(reinterpret_cast<Function>(memory)() == 42);
    HookData data;
    CHECK(InstallCodeHook(memory, reinterpret_cast<void*>(OriginalNative), data));
    CHECK(data.patchSize == 8);
    CHECK(reinterpret_cast<Function>(data.originalCallable)() == 42);
    HookData rejected;
    CHECK(!InstallCodeHook(nullptr, reinterpret_cast<void*>(OriginalNative), rejected));
    CHECK(!InstallCodeHook(reinterpret_cast<void*>(1), reinterpret_cast<void*>(OriginalNative), rejected));
    VirtualFree(memory, 0, MEM_RELEASE);
    if (data.originalCallable) VirtualFree(data.originalCallable, 0, MEM_RELEASE);
}

static DWORD blinkUnit = 123;
DWORD __cdecl TestSelected();
static int killCalls = 0;
void __cdecl TestKill(DWORD unit) { CHECK(unit == 123); ++killCalls; }
void TestKillDispatch() {
    blinkUnit = 123;
    CHECK(DispatchSelectedKill(TestSelected, TestKill) && killCalls == 1);
    blinkUnit = 0;
    CHECK(!DispatchSelectedKill(TestSelected, TestKill) && killCalls == 1);
    CHECK(!DispatchSelectedKill(nullptr, TestKill));
    CHECK(!DispatchSelectedKill(TestSelected, nullptr));
}
static DWORD testType = 77, testOwner = 8;
static int testEnemy = 1;
static float testLife = 100.0f;
DWORD __cdecl TestMassType(DWORD) { return testType; }
DWORD __cdecl TestMassOwner(DWORD) { return testOwner; }
int __cdecl TestMassEnemy(DWORD owner, DWORD player) { CHECK(owner == 8 && player == 1); return testEnemy; }
DWORD __cdecl TestMassLife(DWORD) { DWORD bits; memcpy(&bits, &testLife, sizeof(bits)); return bits; }
void TestMassKillFilter() {
    MassKillNatives natives = {};
    natives.typeId = TestMassType;
    natives.owner = TestMassOwner;
    natives.isEnemy = TestMassEnemy;
    natives.life = TestMassLife;
    const MassKillTarget target = {123, 8, 77};
    CHECK(MassKillEligible(natives, target, 1));
    testEnemy = 0; CHECK(!MassKillEligible(natives, target, 1)); testEnemy = 1;
    testLife = 0.0f; CHECK(!MassKillEligible(natives, target, 1)); testLife = 100.0f;
    testType = 78; CHECK(!MassKillEligible(natives, target, 1)); testType = 77;
    testOwner = 9; CHECK(!MassKillEligible(natives, target, 1)); testOwner = 8;
    CHECK(!MassKillEligible(natives, {0, 8, 77}, 1));
}
static float blinkX = 0, blinkY = 456;
static int blinkCalls = 0;
DWORD __cdecl TestSelected() { return blinkUnit; }
DWORD __cdecl TestMouseX() { DWORD v; memcpy(&v, &blinkX, 4); return v; }
DWORD __cdecl TestMouseY() { DWORD v; memcpy(&v, &blinkY, 4); return v; }
void __cdecl TestMove(DWORD unit, const float* x, const float* y) {
    CHECK(unit == 123 && *x == blinkX && *y == blinkY); ++blinkCalls;
}
void TestBlink() {
    g_nativeCount = 0;
    BlinkSelectedUnit(0, 0); CHECK(blinkCalls == 0);
    FindOrAddNative("DzGetSelectedLeaderUnit", "()Hunit;", (void*)TestSelected);
    FindOrAddNative("DzGetMouseTerrainX", "()R", (void*)TestMouseX);
    FindOrAddNative("DzGetMouseTerrainY", "()R", (void*)TestMouseY);
    FindOrAddNative("DzSetUnitPosition", "(Hunit;RR)V", (void*)TestMove);
    BlinkSelectedUnit(0, 0); CHECK(blinkCalls == 1);
    blinkUnit = 0; BlinkSelectedUnit(0, 0); CHECK(blinkCalls == 1);
    blinkUnit = 123; blinkX = 1000000; BlinkSelectedUnit(0, 0); CHECK(blinkCalls == 1);
    BlinkSelectedUnit(-1, -1); CHECK(g_blinkArmed && blinkCalls == 1);
}

int main() {
    wchar_t current[MAX_PATH];
    if (GetCurrentDirectoryW(MAX_PATH, current)) wcscpy_s(g_DllDirectory, current);
    TestThunks();
    TestRegistryBounds();
    TestInstructionBoundariesAndGateway();
    TestGuardedStringsAndPluginIdentity();
    TestBlink();
    TestKillDispatch();
    TestMassKillFilter();
    if (failures) return 1;
    std::cout << "Hook regression passed: stack alignment, immutable thunks, registry capacity, full-instruction gateways.\n";
    return 0;
}
