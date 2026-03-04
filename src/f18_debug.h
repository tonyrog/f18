#ifndef __F18_DEBUG_H__
#define __F18_DEBUG_H__

#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include "f18.h"

// Debugger execution modes
typedef enum {
    DBG_MODE_RUN,        // Normal execution (no barrier)
    DBG_MODE_STEP_SLOT,  // Step one slot (micro-step)
    DBG_MODE_STEP_INST,  // Step one instruction word (4 slots)
    DBG_MODE_STEP_OVER,  // Step over call/loop
    DBG_MODE_PAUSE,      // Paused, waiting for user input
    DBG_MODE_QUIT        // Exit debugger
} dbg_mode_t;

// Per-node state
typedef enum {
    DBG_NODE_RUN,        // Node running freely
    DBG_NODE_STEP,       // Node in step mode
    DBG_NODE_PAUSED,     // Node waiting at barrier
    DBG_NODE_BLOCKED,    // Node blocked on I/O
    DBG_NODE_TERMINATED  // Node has exited
} dbg_node_state_t;

// Breakpoint structure
typedef struct {
    uint18_t addr;       // Breakpoint address (P)
    uint18_t node_id;    // Which node (0xFFF = all nodes)
    int      enabled;
    int      hit_count;
} breakpoint_t;

// Watchpoint structure
typedef struct {
    uint18_t addr;       // Watch RAM address
    uint18_t node_id;    // Which node
    int      enabled;
    uint18_t last_value;
    int      on_write;   // Break on write
    int      on_read;    // Break on read
} watchpoint_t;

#define MAX_BREAKPOINTS 32
#define MAX_WATCHPOINTS 16
#define MAX_NODES       144   // 8x18 grid

// Bitmask helpers for step nodes (144 bits = 5 x 32-bit words)
#define STEP_MASK_WORDS 5
#define STEP_NODE_SET(mask, id)   ((mask)[(id)/32] |=  (1U << ((id) % 32)))
#define STEP_NODE_CLR(mask, id)   ((mask)[(id)/32] &= ~(1U << ((id) % 32)))
#define STEP_NODE_TEST(mask, id)  ((mask)[(id)/32] &   (1U << ((id) % 32)))

// Node ID to linear index (for bitmask)
#define NODE_ID_TO_INDEX(id)   (ID_TO_ROW(id) * 18 + ID_TO_COLUMN(id))
#define INDEX_TO_NODE_ID(idx)  MAKE_ID((idx) / 18, (idx) % 18)

// Global debugger state
typedef struct {
    // Global mode
    dbg_mode_t      mode;
    int             enabled;           // -G flag set

    // Step control
    uint32_t        step_count;        // Steps remaining
    int             step_into;         // Follow calls (for step-over)
    int             current_slot;      // Current slot being executed (0-3)
    uint18_t        current_pc;        // Address of current instruction word
    uint18_t        current_iword;     // Current instruction word (for disasm)

    // Node selection
    uint32_t        step_mask[STEP_MASK_WORDS];  // Bitmask: which nodes to step
    uint18_t        focus_node;        // Currently focused node for display
    int             num_step_nodes;    // Count of nodes in step mode

    // Breakpoints and watchpoints
    breakpoint_t    breakpoints[MAX_BREAKPOINTS];
    int             num_breakpoints;
    watchpoint_t    watchpoints[MAX_WATCHPOINTS];
    int             num_watchpoints;

    // Step barrier synchronization
    pthread_mutex_t barrier_lock;
    pthread_cond_t  barrier_cond;
    int             barrier_count;     // Nodes waiting at barrier
    int             barrier_release;   // Signal to release waiting nodes

    // UI scroll positions
    int             grid_scroll_x;
    int             grid_scroll_y;
    int             ram_scroll;
    int             disasm_scroll;
    int             cursor_row;        // Grid cursor position
    int             cursor_col;

    // Timing and stats
    struct timeval  start_time;
    uint64_t        total_instructions;

} debugger_state_t;

// Per-node debug state (to be embedded in reg_node_t)
typedef struct {
    dbg_node_state_t state;
    int              at_barrier;       // Currently waiting at step barrier
    uint64_t         instruction_count;
    uint18_t         last_pc;          // Previous PC (for step-over)
    int              call_depth;       // Track call nesting for step-over
} node_debug_t;

// Global debugger instance
extern debugger_state_t g_debugger;

// Flag for debugger mode
#define FLAG_DEBUG_ENABLE 0x10000

// Debugger initialization/cleanup
void debug_init(void);
void debug_cleanup(void);

// Step node management
void debug_parse_step_nodes(const char* spec);
int  debug_is_step_node(uint18_t id);
void debug_set_step_node(uint18_t id, int enable);
void debug_set_focus(uint18_t id);

// Barrier functions (called from f18_exec.c)
int  debug_pre_instruction(void* rp);  // Returns 1 if should exit
void debug_post_instruction(void* rp, uint18_t pc, uint8_t opcode);

// Breakpoint management
int  debug_add_breakpoint(uint18_t node_id, uint18_t addr);
int  debug_del_breakpoint(int index);
int  debug_check_breakpoint(uint18_t node_id, uint18_t addr);

// Watchpoint management
int  debug_add_watchpoint(uint18_t node_id, uint18_t addr, int on_write, int on_read);
int  debug_del_watchpoint(int index);
void debug_check_watchpoint_write(uint18_t node_id, uint18_t addr, uint18_t value);
void debug_check_watchpoint_read(uint18_t node_id, uint18_t addr);

// Step control (called from UI)
void debug_step_slot(int count);   // Step N slots (micro-step)
void debug_step_inst(int count);   // Step N instruction words
void debug_step_over(void);        // Step over call/loop
void debug_continue(void);         // Continue execution
void debug_pause(void);            // Pause all step nodes
void debug_quit(void);             // Request exit

// Slot-level barrier (called from f18_emu.c slot loop)
int  debug_slot_barrier(void* np, int slot); // Returns 1 if should exit

// Set current instruction info (called after fetch in f18_emu.c)
void debug_set_current_instruction(uint18_t pc, uint18_t iword);

// Utility
const char* debug_mode_name(dbg_mode_t mode);
const char* debug_node_state_name(dbg_node_state_t state);

#endif // __F18_DEBUG_H__
