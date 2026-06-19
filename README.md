# Process Guardian

## 📖 Introduction

**Process Guardian** is a Windows-based process management tool that provides process protection, automatic restart, persistent termination, real-time monitoring, and more. It supports three privilege levels (Normal/Admin/SYSTEM) and can be configured for auto-start and background daemon operation.

---

## 🎯 Core Features

### 1. Process Protection
- Monitors specified processes and automatically restarts them when they exit
- Suitable for critical programs that need to run continuously

### 2. Repeated Termination
- Continuously terminates specified processes to prevent them from running
- Supports single process or process tree termination
- Useful for blocking malware or unwanted programs (including rogue software)

### 3. Real-time Monitoring
- Monitor process metrics in real-time via `observer.exe`:
  - **CPU Usage** (green curve)
  - **Memory Usage** (blue curve)
  - **Cache Usage** (yellow curve)
- Hover over graphs to view historical data points

### 4. Multiple Privilege Modes (Note: Service installation may not always work properly; SYSTEM elevation may fail)
| Mode | Description | Use Case |
|------|-------------|----------|
| Normal | Runs with current user privileges | Managing regular processes |
| Administrator | Requests UAC elevation | Managing system processes |
| SYSTEM | Runs as a Windows service | Managing core system processes |

### 5. Auto-start
- Automatically registers to Windows startup
- Supports hidden mode for background operation

---

## 📁 Project Structure

```
process-guardian/
├── guardian.exe          # Main program (GUI interface)
├── guardiand.exe         # Background daemon process
├── observer.exe          # Real-time monitoring tool
├── service_wrapper.exe   # Windows service wrapper
├── settings.exe          # Settings program
├── install_service.exe   # Service installation tool
├── setup_service.bat     # Service installation script
├── build.bat             # Build script
├── build_settings.bat    # Build settings script
├── data/
│   ├── config.dat        # Configuration file (binary)
│   ├── settings.ini      # Settings file
│   ├── process_guardian.log  # Log file
│   └── reload.signal     # Config reload signal file
├── src/
│   ├── core/             # Core modules
│   │   ├── process_monitor.c/h    # Process monitoring
│   │   └── process_protector.c/h  # Process protection
│   ├── gui/              # GUI interface
│   │   ├── gui.c/h
│   │   └── main.c               # Main entry point
│   └── utils/            # Utility modules
│       └── logger.c/h           # Logging system
└── build/                # Source code (build directory)
    ├── main.c
    ├── daemon.c
    ├── observer.c
    ├── service_wrapper.c
    ├── settings.c
    └── Makefile
```

---

## 🚀 Usage

### Launch Main Program

```bash
# Double-click guardian.exe
# Or launch from command line
guardian.exe

# Launch in hidden mode (background)
guardian.exe --hidden
```

### Interface Overview

#### Left Panel: Running Processes List
- Displays all currently running processes:
  - Process name
  - PID (Process ID)
  - Memory usage (KB)

#### Right Panel: Protected Processes List
- Shows configured processes and their status:
  - Process name
  - PID
  - Type (Protected/Repeated Terminate/Repeated Terminate Tree)

### Right-Click Menu Functions

#### Right-click on a running process:

| Option | Function |
|--------|----------|
| **Terminate Process** | Kill the selected process |
| **Terminate Process Tree** | Kill the process and all its child processes |
| **Repeated Terminate Process** | Continuously terminate this process (block execution) |
| **Repeated Terminate Process Tree** | Continuously terminate process and its children |
| **Protect Process** | Protect process; auto-restart on exit |
| **Open Process Location** | Open the process directory in Explorer |
| **Real-time Monitor** | Open observer window to monitor this process |
| **Install SYSTEM Service** | Install service with SYSTEM privileges (May not work sometimes!!! May not work sometimes!!! May not work sometimes!!!) |

#### Right-click on a protected process:

| Option | Function |
|--------|----------|
| **Cancel Repeated Terminate** | Remove repeated termination rule |
| **Cancel Protection** | Remove protection rule |

### Start Daemon

Click the **"Start Daemon"** button to choose:

1. **Normal** - Run with current user identity
2. **Administrator** - Run after UAC elevation
3. **SYSTEM** - Install and run as Windows service

### Real-time Monitoring

```bash
# Monitor a process by PID
observer.exe --pid <PID>

# Example: Monitor process with PID 1234
observer.exe --pid 1234
```

Monitoring window displays:
- 30-minute historical data curves
- Hover to view specific values at any time point
- Initial 2-second refresh interval, switches to 20 seconds after collecting 10 data points

---

## ⚙️ Configuration Files

### settings.ini

Located at `data/settings.ini`:

```ini
[Settings]
CheckInterval=100      # Check interval (milliseconds), default 100ms
ReloadInterval=3000    # Config reload interval (milliseconds), default 3000ms
```

**Parameter Details:**
- `CheckInterval`: Frequency at which the daemon checks process status
  - Range: 100 - 60000 milliseconds
  - Lower values = faster response but higher CPU usage
- `ReloadInterval`: Frequency for automatic configuration reload
  - Range: 1000 - 3600000 milliseconds
  - Can manually trigger reload by creating `reload.signal` file

### config.dat

Binary configuration file storing the protected process list:
- Process name
- PID
- Process tree mode flag
- Repeated termination mode flag

---

## 📝 Log File

Logs are located at `data/process_guardian.log`:

```
[2026-06-13 11:30:00] Guardian started
[2026-06-13 11:30:01] Guardian Daemon started (hidden=1)
[2026-06-13 11:30:02] Loaded 3 protected processes
[2026-06-13 11:30:05] Protected process restarted: notepad.exe
[2026-06-13 11:30:10] Repeated terminate: malware.exe
[2026-06-13 11:30:15] Registered for auto-start
```

---

## 🔧 Advanced Features

### Running as a Service (SYSTEM Privileges)

1. Click **"Start Daemon"** → Select **"SYSTEM"**
2. The program will automatically:
   - Check if `GuardianDaemon` service exists
   - If not, request administrator privileges to create the service
   - Start the service

**Manual Service Installation:**
```bash
# Using sc command
sc create GuardianDaemon binPath= "full_path\service_wrapper.exe" start= auto

# Start service
sc start GuardianDaemon

# Stop service
sc stop GuardianDaemon

# Delete service
sc delete GuardianDaemon
```

### Configuration Hot Reload

The daemon periodically checks for configuration changes. You can also manually trigger:

```bash
# Create signal file to trigger immediate reload
type nul > data\reload.signal
```

### Auto-start

The program automatically adds a startup entry to:
```
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
```
Key name: `GuardianDaemon`

---

## 🛡️ Security Notes

### Privilege Requirements

- **Regular Process Management**: Current user privileges sufficient
- **System Process Management**: Administrator privileges required
- **Core Process Management**: SYSTEM privileges required (via service)

### SeDebugPrivilege

The program automatically acquires `SeDebugPrivilege` to terminate protected processes.

### Single Instance Protection

The daemon uses mutex `Global\GuardianDaemonMutex` to ensure single-instance operation.

---

## 🐛 Troubleshooting

### Problem: Cannot terminate certain processes
**Solution**: Restart the daemon with Administrator or SYSTEM privileges

### Problem: Service fails to start
**Solution**:
1. Ensure main program is run as Administrator
2. Check if service already exists: `sc query GuardianDaemon`
3. Delete old service and reinstall

### Problem: Configuration not taking effect
**Solution**:
1. Verify `data/settings.ini` format is correct
2. Create `data\reload.signal` to trigger reload
3. Restart the daemon

### Problem: Log file too large
**Solution**: Periodically clean up `data/process_guardian.log`

### Problem: DO NOT make accidental operations (CRITICAL WARNING)
**Solution**: This is the MOST IMPORTANT section in the entire README. I once selected ALL running processes in my computer (except the Guardian process itself) and clicked "Repeated Terminate". Then... my screen went black. After reboot, it showed "Your PC ran into a problem and couldn't be repaired"... This was truly bizarre (and embarrassing).

**What to do if you get a Blue Screen of Death (BSOD)**: 
1. Insert a USB drive into another working computer
2. Install PE (Pre-installation Environment) on the USB drive (this will FORMAT the USB, so BACKUP FIRST!! Recommend wePE)
3. Plug the USB into the bricked computer
4. Restart and repeatedly press F2 to enter BIOS/Boot settings
5. Select your PE USB drive to boot
6. Once in the PE desktop, delete this software

---

## 📋 Build Instructions

### Requirements
- Windows SDK
- GCC (MinGW) or MSVC
- Make

### Build Main Program
```bash
cd build
make
```

### Build Settings Program
```bash
build_settings.bat
```

### Manual Build (GCC)
```bash
gcc -o guardian.exe -mwindows build/main.c -lcomctl32 -lpsapi
gcc -o guardiand.exe -mwindows build/daemon.c -lpsapi
gcc -o observer.exe -mwindows build/observer.c -lpsapi
gcc -o service_wrapper.exe build/service_wrapper.c
```

---

## 📄 License

This project is a personal/internal use tool.

---

## 📞 Support

Check the log file `data/process_guardian.log` for detailed runtime information.

---

## Please give me a star!!!😄

---

*Last updated: 2026-06-13*
