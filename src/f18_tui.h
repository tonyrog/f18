#ifndef __F18_TUI_H__
#define __F18_TUI_H__

#include "f18.h"
#include "f18_debug.h"

// Initialize the TUI (ncurses)
int tui_init(void);

// Cleanup TUI
void tui_cleanup(void);

// Main debugger loop
void debug_tui_main(void);

// Refresh display
void tui_refresh(void);

// Draw individual components
void tui_draw_title(void);
void tui_draw_grid(void);
void tui_draw_registers(node_t* np);
void tui_draw_stacks(node_t* np);
void tui_draw_disasm(node_t* np);
void tui_draw_ram(node_t* np);
void tui_draw_command(void);
void tui_draw_help(void);

// Handle keyboard input
int tui_handle_input(int ch);

// Get focused node
node_t* tui_get_focused_node(void);

#endif // __F18_TUI_H__
