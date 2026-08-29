/* A text script of client actions, parsed to a typed action list. */
#ifndef MMO_SCRIPT_H
#define MMO_SCRIPT_H

#include <stddef.h>

#include "client.h"

typedef enum {
    OPENMMO_SCRIPT_CONNECT = 0,  /* start the session FSM (target from the CLI) */
    OPENMMO_SCRIPT_AWAIT_STATUS, /* pump until .arg (an openmmo_status) is reached */
    OPENMMO_SCRIPT_AWAIT_EVENT,  /* pump until an .arg (openmmo_event_kind) is polled */
    OPENMMO_SCRIPT_FRAMES,       /* pump .arg frames (let a burst settle / pace) */
    OPENMMO_SCRIPT_MOVE,         /* send one step; .arg is a facing DIR_* (0..3) */
    OPENMMO_SCRIPT_CHAT,         /* send .text as a chat line */
    OPENMMO_SCRIPT_DISCONNECT    /* tear the session down */
} openmmo_script_op;

typedef struct {
    openmmo_script_op op;
    int  arg;         /* AWAIT_STATUS: openmmo_status; AWAIT_EVENT: openmmo_event_kind;
                       * FRAMES: count (>0); MOVE: facing DIR_* (0..3) */
    char text[128];   /* CHAT: the line to send */
    int  line;        /* 1-based source line, for the executor's trace */
} openmmo_script_action;

#define OPENMMO_SCRIPT_MAX 256   /* actions per script; overflow is a parse error */

typedef struct {
    openmmo_script_action act[OPENMMO_SCRIPT_MAX];
    int count;
} openmmo_script;

/* Parse `len` bytes of script text into *out. */
int openmmo_script_parse(const char *text, size_t len, openmmo_script *out,
                         char *err, size_t err_sz);

/* The directive verb for a parsed op, for the executor's trace. */
const char *openmmo_script_op_name(openmmo_script_op op);

#endif /* MMO_SCRIPT_H */
