//
// F18 Interactive Debugger - Core Logic
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "f18_debug.h"
#include "f18_node.h"

// Global debugger state
debugger_state_t g_debugger;

// Mode names for display
static const char* mode_names[] = {
    "RUN", "SLOT", "INST", "OVER", "PAUSE", "QUIT"
};

// Node state names
static const char* node_state_names[] = {
    "run", "step", "paused", "blocked", "term"
};

const char* debug_mode_name(dbg_mode_t mode)
{
    if (mode >= 0 && mode <= DBG_MODE_QUIT)
        return mode_names[mode];
    return "???";
}

const char* debug_node_state_name(dbg_node_state_t state)
{
    if (state >= 0 && state <= DBG_NODE_TERMINATED)
        return node_state_names[state];
    return "???";
}

// Initialize the debugger
void debug_init(void)
{
    memset(&g_debugger, 0, sizeof(g_debugger));

    g_debugger.mode = DBG_MODE_PAUSE;  // Start paused
    g_debugger.enabled = 1;
    g_debugger.focus_node = 0;  // Default to node 000

    pthread_mutex_init(&g_debugger.barrier_lock, NULL);
    pthread_cond_init(&g_debugger.barrier_cond, NULL);

    gettimeofday(&g_debugger.start_time, NULL);
}

// Cleanup
void debug_cleanup(void)
{
    pthread_mutex_destroy(&g_debugger.barrier_lock);
    pthread_cond_destroy(&g_debugger.barrier_cond);
}

// Parse step nodes specification: "708", "708,709,710", "700-709"
void debug_parse_step_nodes(const char* spec)
{
    const char* p = spec;

    // Clear existing mask
    memset(g_debugger.step_mask, 0, sizeof(g_debugger.step_mask));
    g_debugger.num_step_nodes = 0;

    while (*p) {
        // Skip whitespace and commas
        while (*p && (isspace(*p) || *p == ','))
            p++;
        if (!*p) break;

        // Parse first number
        char* end;
        long id1 = strtol(p, &end, 10);
        if (end == p) break;  // No number found
        p = end;

        // Check for range
        if (*p == '-') {
            p++;
            long id2 = strtol(p, &end, 10);
            if (end == p) {
                // Single number followed by dash but no second number
                id2 = id1;
            }
            p = end;

            // Add range
            for (long id = id1; id <= id2; id++) {
                int row = id / 100;
                int col = id % 100;
                if (row >= 0 && row < 8 && col >= 0 && col < 18) {
                    int idx = NODE_ID_TO_INDEX(id);
                    STEP_NODE_SET(g_debugger.step_mask, idx);
                    g_debugger.num_step_nodes++;
                }
            }
        } else {
            // Single node
            int row = id1 / 100;
            int col = id1 % 100;
            if (row >= 0 && row < 8 && col >= 0 && col < 18) {
                int idx = NODE_ID_TO_INDEX(id1);
                STEP_NODE_SET(g_debugger.step_mask, idx);
                g_debugger.num_step_nodes++;

                // Set focus to first specified node
                if (g_debugger.num_step_nodes == 1)
                    g_debugger.focus_node = id1;
            }
        }
    }
}

// Check if a node is in step mode
int debug_is_step_node(uint18_t id)
{
    int row = ID_TO_ROW(id);
    int col = ID_TO_COLUMN(id);
    if (row < 0 || row >= 8 || col < 0 || col >= 18)
        return 0;
    int idx = NODE_ID_TO_INDEX(id);
    return STEP_NODE_TEST(g_debugger.step_mask, idx) != 0;
}

// Enable/disable step mode for a node
void debug_set_step_node(uint18_t id, int enable)
{
    int row = ID_TO_ROW(id);
    int col = ID_TO_COLUMN(id);
    if (row < 0 || row >= 8 || col < 0 || col >= 18)
        return;
    int idx = NODE_ID_TO_INDEX(id);

    int was_set = STEP_NODE_TEST(g_debugger.step_mask, idx) != 0;

    if (enable && !was_set) {
        STEP_NODE_SET(g_debugger.step_mask, idx);
        g_debugger.num_step_nodes++;
    } else if (!enable && was_set) {
        STEP_NODE_CLR(g_debugger.step_mask, idx);
        g_debugger.num_step_nodes--;
    }
}

// Set focus node
void debug_set_focus(uint18_t id)
{
    int row = ID_TO_ROW(id);
    int col = ID_TO_COLUMN(id);
    if (row >= 0 && row < 8 && col >= 0 && col < 18) {
        g_debugger.focus_node = id;
    }
}

// Pre-instruction hook - called before each instruction fetch
// Returns 1 if the emulator should exit
int debug_pre_instruction(void* vp)
{
    reg_node_t* rp = (reg_node_t*)vp;
    node_t* np = &rp->n;

    if (!g_debugger.enabled)
        return 0;

    if (!debug_is_step_node(np->id))
        return 0;  // Not a step node, run freely

    pthread_mutex_lock(&g_debugger.barrier_lock);

    // At instruction boundary, update PC and fetch instruction word for display
    uint18_t pc = np->reg.p & MASK9;
    g_debugger.current_slot = 0;
    g_debugger.current_pc = pc;

    // Read instruction word for display (same logic as f18_emu read_mem)
    if (pc <= RAM_END2) {
        g_debugger.current_iword = np->ram[pc & MASK6];
    } else if (pc <= ROM_END2 && np->rom) {
        g_debugger.current_iword = np->rom[(pc - ROM_START) & MASK6];
    } else {
        g_debugger.current_iword = 0;
    }

    // Check for breakpoints
    if (debug_check_breakpoint(np->id, np->reg.p)) {
        g_debugger.mode = DBG_MODE_PAUSE;
    }

    // In PAUSE mode, wait at barrier
    // STEP_SLOT passes through here to stop at slot barriers instead
    // STEP_INST/STEP_OVER wait here to count instruction words
    if (g_debugger.mode == DBG_MODE_PAUSE ||
        g_debugger.mode == DBG_MODE_STEP_INST ||
        g_debugger.mode == DBG_MODE_STEP_OVER) {

        rp->debug.at_barrier = 1;
        rp->debug.state = DBG_NODE_PAUSED;
        g_debugger.barrier_count++;

        // Wait for release signal
        while (!g_debugger.barrier_release &&
               g_debugger.mode != DBG_MODE_RUN &&
               g_debugger.mode != DBG_MODE_QUIT) {
            pthread_cond_wait(&g_debugger.barrier_cond,
                              &g_debugger.barrier_lock);
        }

        g_debugger.barrier_count--;
        rp->debug.at_barrier = 0;
        rp->debug.state = DBG_NODE_STEP;

        // Decrement step count if stepping instructions
        if (g_debugger.mode == DBG_MODE_STEP_INST) {
            if (g_debugger.step_count > 0) {
                g_debugger.step_count--;
                if (g_debugger.step_count == 0) {
                    g_debugger.mode = DBG_MODE_PAUSE;
                    g_debugger.barrier_release = 0;
                }
            }
        }
        // For STEP_SLOT, don't reset barrier_release here - let slot barrier handle it
    }

    int should_quit = (g_debugger.mode == DBG_MODE_QUIT);
    pthread_mutex_unlock(&g_debugger.barrier_lock);

    return should_quit;
}

// Post-instruction hook - called after each instruction
void debug_post_instruction(void* vp, uint18_t pc, uint8_t opcode)
{
    reg_node_t* rp = (reg_node_t*)vp;
    (void)pc;  // Used for tracking

    if (!g_debugger.enabled)
        return;

    rp->debug.instruction_count++;
    g_debugger.total_instructions++;

    // Track call depth for step-over
    if (opcode == INS_PCALL) {
        rp->debug.call_depth++;
    } else if (opcode == INS_RETURN) {
        if (rp->debug.call_depth > 0)
            rp->debug.call_depth--;
    }

    rp->debug.last_pc = pc;
}

// Add a breakpoint
int debug_add_breakpoint(uint18_t node_id, uint18_t addr)
{
    if (g_debugger.num_breakpoints >= MAX_BREAKPOINTS)
        return -1;

    int idx = g_debugger.num_breakpoints++;
    g_debugger.breakpoints[idx].node_id = node_id;
    g_debugger.breakpoints[idx].addr = addr;
    g_debugger.breakpoints[idx].enabled = 1;
    g_debugger.breakpoints[idx].hit_count = 0;

    return idx;
}

// Delete a breakpoint
int debug_del_breakpoint(int index)
{
    if (index < 0 || index >= g_debugger.num_breakpoints)
        return -1;

    // Shift remaining breakpoints down
    for (int i = index; i < g_debugger.num_breakpoints - 1; i++) {
        g_debugger.breakpoints[i] = g_debugger.breakpoints[i + 1];
    }
    g_debugger.num_breakpoints--;

    return 0;
}

// Check if we hit a breakpoint
int debug_check_breakpoint(uint18_t node_id, uint18_t addr)
{
    for (int i = 0; i < g_debugger.num_breakpoints; i++) {
        breakpoint_t* bp = &g_debugger.breakpoints[i];
        if (!bp->enabled)
            continue;
        if (bp->addr != addr)
            continue;
        if (bp->node_id != 0xFFF && bp->node_id != node_id)
            continue;

        bp->hit_count++;
        return 1;
    }
    return 0;
}

// Add a watchpoint
int debug_add_watchpoint(uint18_t node_id, uint18_t addr, int on_write, int on_read)
{
    if (g_debugger.num_watchpoints >= MAX_WATCHPOINTS)
        return -1;

    int idx = g_debugger.num_watchpoints++;
    g_debugger.watchpoints[idx].node_id = node_id;
    g_debugger.watchpoints[idx].addr = addr;
    g_debugger.watchpoints[idx].enabled = 1;
    g_debugger.watchpoints[idx].on_write = on_write;
    g_debugger.watchpoints[idx].on_read = on_read;
    g_debugger.watchpoints[idx].last_value = 0;

    return idx;
}

// Delete a watchpoint
int debug_del_watchpoint(int index)
{
    if (index < 0 || index >= g_debugger.num_watchpoints)
        return -1;

    for (int i = index; i < g_debugger.num_watchpoints - 1; i++) {
        g_debugger.watchpoints[i] = g_debugger.watchpoints[i + 1];
    }
    g_debugger.num_watchpoints--;

    return 0;
}

// Check watchpoint on write
void debug_check_watchpoint_write(uint18_t node_id, uint18_t addr, uint18_t value)
{
    for (int i = 0; i < g_debugger.num_watchpoints; i++) {
        watchpoint_t* wp = &g_debugger.watchpoints[i];
        if (!wp->enabled || !wp->on_write)
            continue;
        if (wp->addr != addr)
            continue;
        if (wp->node_id != 0xFFF && wp->node_id != node_id)
            continue;

        // Value changed - trigger pause
        if (wp->last_value != value) {
            wp->last_value = value;
            pthread_mutex_lock(&g_debugger.barrier_lock);
            g_debugger.mode = DBG_MODE_PAUSE;
            pthread_mutex_unlock(&g_debugger.barrier_lock);
        }
    }
}

// Check watchpoint on read
void debug_check_watchpoint_read(uint18_t node_id, uint18_t addr)
{
    for (int i = 0; i < g_debugger.num_watchpoints; i++) {
        watchpoint_t* wp = &g_debugger.watchpoints[i];
        if (!wp->enabled || !wp->on_read)
            continue;
        if (wp->addr != addr)
            continue;
        if (wp->node_id != 0xFFF && wp->node_id != node_id)
            continue;

        pthread_mutex_lock(&g_debugger.barrier_lock);
        g_debugger.mode = DBG_MODE_PAUSE;
        pthread_mutex_unlock(&g_debugger.barrier_lock);
    }
}

// Step N slots (micro-step)
void debug_step_slot(int count)
{
    pthread_mutex_lock(&g_debugger.barrier_lock);
    g_debugger.mode = DBG_MODE_STEP_SLOT;
    g_debugger.step_count = count;
    g_debugger.barrier_release = 1;
    pthread_cond_broadcast(&g_debugger.barrier_cond);
    pthread_mutex_unlock(&g_debugger.barrier_lock);
}

// Step N instruction words (each word = up to 4 slots)
void debug_step_inst(int count)
{
    pthread_mutex_lock(&g_debugger.barrier_lock);
    g_debugger.mode = DBG_MODE_STEP_INST;
    g_debugger.step_count = count;
    g_debugger.barrier_release = 1;
    pthread_cond_broadcast(&g_debugger.barrier_cond);
    pthread_mutex_unlock(&g_debugger.barrier_lock);
}

// Slot-level barrier - called before each slot execution
// Returns 1 if should exit
int debug_slot_barrier(void* vp, int slot)
{
    reg_node_t* rp = (reg_node_t*)vp;
    node_t* np = &rp->n;

    if (!g_debugger.enabled)
        return 0;

    if (!debug_is_step_node(np->id))
        return 0;

    pthread_mutex_lock(&g_debugger.barrier_lock);

    // Track current slot for display (under lock for consistency)
    g_debugger.current_slot = slot;

    // In STEP_INST mode, don't stop at slots (execute whole word)
    if (g_debugger.mode == DBG_MODE_STEP_INST ||
        g_debugger.mode == DBG_MODE_STEP_OVER ||
        g_debugger.mode == DBG_MODE_RUN) {
        pthread_mutex_unlock(&g_debugger.barrier_lock);
        return 0;
    }

    // PAUSE or STEP_SLOT: wait at barrier
    rp->debug.at_barrier = 1;
    rp->debug.state = DBG_NODE_PAUSED;
    g_debugger.barrier_count++;

    // Wait for release
    while (!g_debugger.barrier_release &&
           g_debugger.mode != DBG_MODE_RUN &&
           g_debugger.mode != DBG_MODE_QUIT) {
        pthread_cond_wait(&g_debugger.barrier_cond,
                          &g_debugger.barrier_lock);
    }

    g_debugger.barrier_count--;
    rp->debug.at_barrier = 0;
    rp->debug.state = DBG_NODE_STEP;

    // In STEP_SLOT mode, decrement step count and pause when done
    if (g_debugger.mode == DBG_MODE_STEP_SLOT) {
        // Consume the release signal for this slot step
        g_debugger.barrier_release = 0;
        if (g_debugger.step_count > 0) {
            g_debugger.step_count--;
            if (g_debugger.step_count == 0) {
                g_debugger.mode = DBG_MODE_PAUSE;
            }
        }
    }

    int should_quit = (g_debugger.mode == DBG_MODE_QUIT);
    pthread_mutex_unlock(&g_debugger.barrier_lock);

    return should_quit;
}

// Step over (run until call returns)
void debug_step_over(void)
{
    pthread_mutex_lock(&g_debugger.barrier_lock);
    g_debugger.mode = DBG_MODE_STEP_OVER;
    g_debugger.step_into = 0;
    g_debugger.barrier_release = 1;
    pthread_cond_broadcast(&g_debugger.barrier_cond);
    pthread_mutex_unlock(&g_debugger.barrier_lock);
}

// Continue execution
void debug_continue(void)
{
    pthread_mutex_lock(&g_debugger.barrier_lock);
    g_debugger.mode = DBG_MODE_RUN;
    g_debugger.barrier_release = 1;
    pthread_cond_broadcast(&g_debugger.barrier_cond);
    pthread_mutex_unlock(&g_debugger.barrier_lock);
}

// Pause all step nodes
void debug_pause(void)
{
    pthread_mutex_lock(&g_debugger.barrier_lock);
    g_debugger.mode = DBG_MODE_PAUSE;
    g_debugger.barrier_release = 0;
    pthread_mutex_unlock(&g_debugger.barrier_lock);
}

// Request exit
void debug_quit(void)
{
    pthread_mutex_lock(&g_debugger.barrier_lock);
    g_debugger.mode = DBG_MODE_QUIT;
    g_debugger.barrier_release = 1;
    pthread_cond_broadcast(&g_debugger.barrier_cond);
    pthread_mutex_unlock(&g_debugger.barrier_lock);
}

// Set current instruction info (called after fetch)
void debug_set_current_instruction(uint18_t pc, uint18_t iword)
{
    g_debugger.current_pc = pc;
    g_debugger.current_iword = iword;
}
