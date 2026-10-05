# RemoteOps - IE3090 Network Programming Assignment

## Student Information
- Registration Number: IT24102434
- Project: RemoteOps
- Language: C
- Platform: Linux / CentOS
- Compiler: GCC

## Personalized Values
- TCP Port: 9410
- Authentication Token: OPS-2434
- Session ID: 4342
- Agent Source File: agent_434.c
- Controller Source File: controller_434.c
- Makefile: Makefile_434
- Log File: remoteops_IT24102434.log
- Agent Storage Directory: ./agentfiles/IT24102434/

## Project Overview
RemoteOps is a remote system monitoring and management application developed using BSD sockets in C.

The Agent acts as the server and listens for Controller connections using TCP. A secondary UDP channel is used for periodic system monitoring information.

The Agent supports multiple simultaneous Controller connections using POSIX threads.

## Supported Commands

### Authentication
AUTH <token>

### System Information
SYSINFO

### Process List
LISTPROC

### Remote Command Execution
EXEC DATE
EXEC UPTIME
EXEC DISKFREE
EXEC HOSTNAME
EXEC WHOAMI

Only the commands listed above are allowed.

### File Upload
PUT <filename>

### File Download
GET <filename>

### UDP Monitoring
MONITOR START <udp_port>
MONITOR STOP

### Disconnect
QUIT

## Build Instructions

Build both programs:

```bash
make -f Makefile_434
