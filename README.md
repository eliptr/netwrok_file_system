# NFS – Network File Synchronization System

## Overview
This project implements a **Network File System synchronization tool** in C, extending the local sync system from Assignment 1 into a **remote, multi-threaded system** using sockets, worker threads, and condition variables.

It consists of three executables:

1. **`nfs_manager`** – Central manager that coordinates synchronization jobs between clients.  
2. **`nfs_console`** – User interface to send commands to `nfs_manager`.  
3. **`nfs_client`** – Remote endpoint that lists, pulls, and pushes files.

The system automatically synchronizes files between specified *source* and *target* directories across different hosts.

---

## Design Summary

### Architecture
- **nfs_manager**
  - Listens on a TCP port for commands from `nfs_console`.
  - Reads initial sync pairs from a `config_file`.
  - Uses a **thread pool** (up to `worker_limit` threads) for parallel file transfers.
  - Maintains a **bounded buffer queue** for pending sync tasks.
  - Synchronizes task queue access using **mutexes** and **condition variables**.
  - Logs all actions to a `manager-logfile`.

- **nfs_console**
  - Connects to `nfs_manager` via TCP.
  - Sends commands: `add`, `cancel`, `shutdown`.
  - Displays responses and logs them to a `console-logfile`.

- **nfs_client**
  - Listens on a TCP port for commands from `nfs_manager` workers.
  - Supports:
    - `LIST <dir>` – returns file names in the directory.
    - `PULL <file>` – streams file contents.
    - `PUSH <file> chunk_size data` – writes data chunks to file.
  - Uses **low-level I/O** (`open`, `read`, `write`, `close`) only.

---
### Synchronization Logic
1. On startup, `nfs_manager`:
 - Loads initial sync pairs from `config_file`.
 - Creates worker thread pool.
 - Connects to each source `nfs_client` to get file lists (`LIST`).
 - Queues sync tasks in the buffer.

2. Worker threads:
 - Dequeue one task at a time.
 - Connect to source `nfs_client` to `PULL` file.
 - Stream chunks to target `nfs_client` using `PUSH`.
 - Log both pull and push results.

3. Commands from `nfs_console` are processed live:
 - `add <src> <tgt>` – Queue all files for sync.
 - `cancel <src>` – Remove pending tasks for that source.
 - `shutdown` – Gracefully finish active + queued tasks and stop.
---
## Compilation

### Run:
```bash
make
```
### Execution
 1. Start nfs_client on both source & target hosts <br>
 ./nfs_client -p 8000 <br>
./nfs_client -p 8080 <br>
2. Start nfs_manager <br>
./nfs_manager -l manager.log -c config.txt -n 5 -p 8035 -b 10 <br>
Arguments: <br>
- -l – Manager log file.
- -c – Config file with sync pairs.
- -n – Max worker threads.
- -p – Port for console commands.
- -b – Task queue capacity. <br>
<br>
Example config.txt <br>
/src1@192.168.1.10:8000 /dst1@192.168.1.20:8080

3. Start nfs_console <br>
  ./nfs_console -l console.log -h 127.0.0.1 -p 8035

4. Commanands (example) <br>
add /src1@192.168.1.10:8000 /dst1@192.168.1.20:8080 <br>
cancel /src1@192.168.1.10:8000 <br>
shutdown <br>

### Logging Format
- Manager Log <br>
[TIMESTAMP] [SRC] [DST] [THREAD_ID] [PULL|PUSH] [SUCCESS|ERROR] [DETAILS] <br>
- Console Log <br> 
[TIMESTAMP] Command add /src@ip:port -> /dst@ip:port <br>

### Assumptions & Notes
- Directories are flat (no subdirectories).
- Paths in config_file are relative to the running nfs_client working directory.
- Existing target files are overwritten without timestamp checks.
- All socket communication uses TCP.
- No external sync tools (scp, rsync) are used — only low-level syscalls.
- Log files are cleared on startup.

