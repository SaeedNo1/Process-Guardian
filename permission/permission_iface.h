/**
 * permission_iface.h
 * 权限 DLL 公共接口定义
 *
 * 每个 DLL (normal.dll / admin.dll / SYSTEM.dll) 均导出以下函数：
 *
 *   BOOL RestartDaemon(HWND hwndOwner)
 *     - 先终止当前正在运行的 guardiand.exe（若有）
 *     - 再以本 DLL 对应的权限重新启动 guardiand.exe
 *     - hwndOwner: 父窗口句柄，用于弹出 UAC 提示框或错误对话框；可为 NULL
 *     - 返回 TRUE 表示操作成功发起，FALSE 表示失败
 *
 * guardian.exe 调用示例：
 *   HMODULE hDll = LoadLibraryW(L"permission\\admin.dll");
 *   typedef BOOL (*FnRestartDaemon)(HWND);
 *   FnRestartDaemon fn = (FnRestartDaemon)GetProcAddress(hDll, "RestartDaemon");
 *   fn(hwnd);
 *   FreeLibrary(hDll);
 */

#ifndef PERMISSION_IFACE_H
#define PERMISSION_IFACE_H

#include <windows.h>

#ifdef BUILDING_DLL
#  define PERM_API __declspec(dllexport)
#else
#  define PERM_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 终止 guardiand.exe（若在运行），然后以本 DLL 对应的权限重新启动它。
 * @param hwndOwner  父窗口句柄（可为 NULL）
 * @return TRUE = 成功启动；FALSE = 失败
 */
PERM_API BOOL RestartDaemon(HWND hwndOwner);

#ifdef __cplusplus
}
#endif

#endif /* PERMISSION_IFACE_H */
