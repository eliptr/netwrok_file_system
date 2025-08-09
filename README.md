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
