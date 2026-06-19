#ifndef OBSERVER_DLL_H
#define OBSERVER_DLL_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BUILDING_DLL
#define OBSERVER_API __declspec(dllexport)
#else
#define OBSERVER_API __declspec(dllimport)
#endif

/* 获取进程的 CPU/GPU/内存/显存 统计数据
 * pid       - 目标进程PID
 * cpu       - CPU使用率(0-100%)
 * gpu       - GPU使用率(0-100%)
 * memMB     - 内存使用量(MB)
 * vramMB    - 显存使用量(MB, 系统总量)
 */
OBSERVER_API void GetProcessStatsEx(DWORD pid, double *cpu, double *gpu,
                                     double *memMB, double *vramMB);

#ifdef __cplusplus
}
#endif

#endif /* OBSERVER_DLL_H */
