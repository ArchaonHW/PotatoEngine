/**
 * FakeCheatModule - 紅隊測試用的無害 DLL
 *
 * 用途：模擬「被注入的作弊模組」，用來驗證 SecurityManager 的
 * CheckLoadedModules() / FindUntrustedModules() 是否能偵測到未信任 DLL。
 *
 * 此 DLL 不執行任何惡意行為，僅作為一個可被 LoadLibrary 載入的
 * 合法 Windows 模組存在。
 */

#ifdef _WIN32
#include <windows.h>

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD reason, LPVOID /*reserved*/) {
    // 不做任何事，純粹作為一個「可被列舉到的模組」
    return reason == DLL_PROCESS_ATTACH || TRUE;
}
#endif
