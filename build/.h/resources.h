/* Resource IDs for process-guardian dialogs. Used by both the .rc
 * (to bind dialogs to numeric IDs) and the C code (via MAKEINTRESOURCEW). */

#ifndef PROCESS_GUARDIAN_RESOURCES_H
#define PROCESS_GUARDIAN_RESOURCES_H

/* Registry tab dialogs (100..199) */
#define IDD_EDITKEY   100
#define IDD_EDITVALUE 101
#define IDD_NEWKEY    102

/* Partition tab dialogs (200..299) */
#define IDD_PICKDISK  200
#define IDD_HEXEDIT   201

/* Control IDs shared by some dialogs */
#define IDC_EDIT_BYTE  1002

#endif
