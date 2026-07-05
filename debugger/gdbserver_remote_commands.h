#ifndef FUSE_DEBUGGER_GDBSERVER_REMOTE_COMMANDS_H
#define FUSE_DEBUGGER_GDBSERVER_REMOTE_COMMANDS_H

#include <stdint.h>

typedef uint8_t (*remote_command_handler_t)(const char *args);

struct remote_command_entry_t {
    const char *name;
    remote_command_handler_t handler;
};

extern const struct remote_command_entry_t remote_commands[];

#endif
