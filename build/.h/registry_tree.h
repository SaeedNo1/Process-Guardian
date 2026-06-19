#ifndef REGISTRY_TREE_H
#define REGISTRY_TREE_H

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>

/* Static link - no DLL export/import needed */

typedef struct {
    wchar_t *fullPath;
    BOOL childrenAdded;
} RegTreeNodeData;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize TreeView with 5 root keys (call after TreeView control is created) */
void RT_InitTreeView(HWND hTree);

/* Handle WM_NOTIFY for TreeView (call from main.c WM_NOTIFY) */
void RT_OnNotify(NMHDR *pnmh);

/* Get selected registry path (caller must free) */
wchar_t *RT_GetSelectedPath(HWND hTree);

/* Show/hide TreeView */
void RT_Show(BOOL show);

/* Free all node data */
void RT_Free(void);

/* Context menu handler for registry TreeView */
BOOL RT_OnContextMenu(HWND hwndParent, HWND hTree, POINT pt,
                      void (*addRegRepeated)(const wchar_t*));

#ifdef __cplusplus
}
#endif

#endif /* REGISTRY_TREE_H */
