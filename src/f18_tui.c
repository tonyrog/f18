//
// F18 Debugger TUI - ncurses interface
//
#define _XOPEN_SOURCE_EXTENDED 1
#include <ncurses.h>
#include <locale.h>
#include <wchar.h>
#include <string.h>
#include <sys/time.h>

#include "f18_tui.h"
#include "f18_node.h"

// External node array from f18_exec.c
extern node_t* node[8][18];
extern uint32_t g_v, g_h;
extern int find_symbol_by_value(uint18_t addr, f18_symbol_table_t* symtab);

// Unicode box drawing characters
#define BOX_H      L'\u2500'  // ─
#define BOX_V      L'\u2502'  // │
#define BOX_TL     L'\u250C'  // ┌
#define BOX_TR     L'\u2510'  // ┐
#define BOX_BL     L'\u2514'  // └
#define BOX_BR     L'\u2518'  // ┘
#define BOX_T_D    L'\u252C'  // ┬
#define BOX_T_U    L'\u2534'  // ┴
#define BOX_T_R    L'\u251C'  // ├
#define BOX_T_L    L'\u2524'  // ┤
#define BOX_X      L'\u253C'  // ┼

// Colors
#define COLOR_TITLE   1
#define COLOR_BORDER  2
#define COLOR_FOCUS   3
#define COLOR_PAUSED  4
#define COLOR_RUNNING 5
#define COLOR_BLOCKED 6

// Window positions and sizes (calculated in tui_init)
static int term_rows, term_cols;
static int grid_top, grid_left, grid_height, grid_width;
static int reg_top, reg_left, reg_height, reg_width;
static int stack_top, stack_left, stack_height, stack_width;
static int disasm_top, disasm_left, disasm_height, disasm_width;
static int ram_top, ram_left, ram_height, ram_width;
static int cmd_top, cmd_left, cmd_height, cmd_width;

static char cmd_line[256] = "";
// static int cmd_pos = 0;  // TODO: for command editing
static int show_help = 0;

// Draw a Unicode horizontal line
static void draw_hline(int y, int x, int len)
{
    move(y, x);
    for (int i = 0; i < len; i++)
        addch(ACS_HLINE);
}

// Draw a Unicode vertical line
static void draw_vline(int y, int x, int len)
{
    for (int i = 0; i < len; i++) {
        move(y + i, x);
        addch(ACS_VLINE);
    }
}

// Draw a box with title
static void draw_box(int y, int x, int h, int w, const char* title)
{
    // Corners
    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x + w - 1, ACS_URCORNER);
    mvaddch(y + h - 1, x, ACS_LLCORNER);
    mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);

    // Edges
    draw_hline(y, x + 1, w - 2);
    draw_hline(y + h - 1, x + 1, w - 2);
    draw_vline(y + 1, x, h - 2);
    draw_vline(y + 1, x + w - 1, h - 2);

    // Title
    if (title && strlen(title) > 0) {
        int title_x = x + 2;
        mvprintw(y, title_x, " %s ", title);
    }
}

int tui_init(void)
{
    setlocale(LC_ALL, "");

    initscr();
    start_color();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);  // Non-blocking input
    curs_set(0);            // Hide cursor

    // Initialize colors
    init_pair(COLOR_TITLE,   COLOR_WHITE, COLOR_BLUE);
    init_pair(COLOR_BORDER,  COLOR_CYAN,  COLOR_BLACK);
    init_pair(COLOR_FOCUS,   COLOR_BLACK, COLOR_YELLOW);
    init_pair(COLOR_PAUSED,  COLOR_YELLOW, COLOR_BLACK);
    init_pair(COLOR_RUNNING, COLOR_GREEN, COLOR_BLACK);
    init_pair(COLOR_BLOCKED, COLOR_RED,   COLOR_BLACK);

    getmaxyx(stdscr, term_rows, term_cols);

    // Calculate layout (assuming 80x24 minimum)
    // Title bar: row 0
    // Left side: grid (rows 1-12)
    // Right side: registers, stacks (rows 1-12)
    // Bottom left: disasm (rows 13-18)
    // Bottom right: RAM (rows 13-18)
    // Command line: rows 19-20

    grid_top = 1;
    grid_left = 0;
    grid_width = term_cols / 2;
    grid_height = 12;

    reg_top = 1;
    reg_left = term_cols / 2;
    reg_width = term_cols - reg_left;
    reg_height = 5;

    stack_top = reg_top + reg_height;
    stack_left = reg_left;
    stack_width = reg_width;
    stack_height = 7;

    disasm_top = grid_top + grid_height;
    disasm_left = 0;
    disasm_width = term_cols / 2;
    disasm_height = 7;

    ram_top = disasm_top;
    ram_left = term_cols / 2;
    ram_width = term_cols - ram_left;
    ram_height = 7;

    cmd_top = term_rows - 2;
    cmd_left = 0;
    cmd_width = term_cols;
    cmd_height = 2;

    return 0;
}

void tui_cleanup(void)
{
    endwin();
}

node_t* tui_get_focused_node(void)
{
    int row = ID_TO_ROW(g_debugger.focus_node);
    int col = ID_TO_COLUMN(g_debugger.focus_node);
    if (row >= 0 && row < (int)g_v && col >= 0 && col < (int)g_h)
        return node[row][col];
    return NULL;
}

void tui_draw_title(void)
{
    node_t* np = tui_get_focused_node();
    const char* mode = debug_mode_name(g_debugger.mode);
    int is_running = (g_debugger.mode == DBG_MODE_RUN);

    struct timeval now, diff;
    gettimeofday(&now, NULL);
    timersub(&now, &g_debugger.start_time, &diff);

    attron(COLOR_PAIR(COLOR_TITLE));
    mvhline(0, 0, ' ', term_cols);
    mvprintw(0, 1, "F18 Debugger");
    if (np)
        mvprintw(0, 16, "[%03d]", np->id);

    if (is_running) {
        attroff(COLOR_PAIR(COLOR_TITLE));
        attron(COLOR_PAIR(COLOR_RUNNING) | A_BOLD);
        mvprintw(0, 25, ">>> RUNNING <<<");
        attroff(COLOR_PAIR(COLOR_RUNNING) | A_BOLD);
        attron(COLOR_PAIR(COLOR_TITLE));
    } else {
        mvprintw(0, 25, "%s s%d PC:%03x", mode, g_debugger.current_slot,
                 g_debugger.current_pc);
    }

    mvprintw(0, term_cols - 12, "%02ld:%02ld:%02ld",
             diff.tv_sec / 3600, (diff.tv_sec / 60) % 60, diff.tv_sec % 60);
    attroff(COLOR_PAIR(COLOR_TITLE));
}

void tui_draw_grid(void)
{
    attron(COLOR_PAIR(COLOR_BORDER));
    draw_box(grid_top, grid_left, grid_height, grid_width, "Node Grid");
    attroff(COLOR_PAIR(COLOR_BORDER));

    // Draw column headers
    int y = grid_top + 1;
    mvprintw(y, grid_left + 3, "  ");
    for (int j = 0; j < (int)g_h && j < 9; j++) {
        mvprintw(y, grid_left + 4 + j * 4, "%02d", j);
    }

    // Draw nodes
    for (int i = (int)g_v - 1; i >= 0 && (g_v - 1 - i) < 8; i--) {
        y = grid_top + 2 + (g_v - 1 - i);
        mvprintw(y, grid_left + 1, "%d", i);

        for (int j = 0; j < (int)g_h && j < 9; j++) {
            reg_node_t* np = (reg_node_t*)node[i][j];
            int id = MAKE_ID(i, j);
            int is_focus = (id == (int)g_debugger.focus_node);
            int is_step = debug_is_step_node(id);
            int at_barrier = (np != NULL) ? np->debug.at_barrier : 0;

            if (is_focus)
                attron(COLOR_PAIR(COLOR_FOCUS) | A_BOLD);
            else if (is_step && at_barrier)
                attron(COLOR_PAIR(COLOR_PAUSED));
            else if (is_step)
                attron(COLOR_PAIR(COLOR_RUNNING));

            if (np == NULL)
                mvprintw(y, grid_left + 3 + j * 4, "----");
            else if (is_focus)
                mvprintw(y, grid_left + 3 + j * 4, "[>>]");
            else if (is_step)
                mvprintw(y, grid_left + 3 + j * 4, "[**]");
            else
                mvprintw(y, grid_left + 3 + j * 4, "[  ]");

            attroff(COLOR_PAIR(COLOR_FOCUS) | COLOR_PAIR(COLOR_PAUSED) |
                    COLOR_PAIR(COLOR_RUNNING) | A_BOLD);
        }
    }

    mvprintw(grid_top + grid_height - 2, grid_left + 2,
             "[>>]=focus [**]=step");
}

void tui_draw_registers(node_t* np)
{
    attron(COLOR_PAIR(COLOR_BORDER));
    char title[32];
    snprintf(title, sizeof(title), "Registers [%03d]", np ? np->id : 0);
    draw_box(reg_top, reg_left, reg_height, reg_width, title);
    attroff(COLOR_PAIR(COLOR_BORDER));

    if (!np) return;

    int y = reg_top + 1;
    mvprintw(y, reg_left + 2, "P=%03x  A=%05x  B=%03x",
             np->reg.p, np->reg.a, np->reg.b);
    y++;
    mvprintw(y, reg_left + 2, "I=%05x  C=%d  SP=%d  RP=%d",
             np->reg.i, np->reg.c, np->reg.sp, np->reg.rp);
}

void tui_draw_stacks(node_t* np)
{
    attron(COLOR_PAIR(COLOR_BORDER));
    draw_box(stack_top, stack_left, stack_height, stack_width, "Stacks");
    attroff(COLOR_PAIR(COLOR_BORDER));

    if (!np) return;

    // Data stack (left half)
    int y = stack_top + 1;
    int half = stack_width / 2;
    mvprintw(y, stack_left + 2, "Data Stack");
    y++;
    mvprintw(y++, stack_left + 2, "T: %05x", np->reg.t);
    mvprintw(y++, stack_left + 2, "S: %05x", np->reg.s);
    for (int i = 0; i < 3 && y < stack_top + stack_height - 1; i++) {
        mvprintw(y++, stack_left + 2, "%d: %05x",
                 i, np->ds[(np->reg.sp - 1 - i) & 7]);
    }

    // Return stack (right half)
    y = stack_top + 1;
    mvprintw(y, stack_left + half + 1, "Ret Stack");
    y++;
    mvprintw(y++, stack_left + half + 1, "R: %05x", np->reg.r);
    for (int i = 0; i < 4 && y < stack_top + stack_height - 1; i++) {
        mvprintw(y++, stack_left + half + 1, "%d: %05x",
                 i, np->rs[(np->reg.rp - 1 - i) & 7]);
    }
}

// Check if opcode is a branch/jump instruction that uses remaining bits for address
static int is_branch_opcode(int opcode)
{
    return (opcode == INS_PJUMP || opcode == INS_PCALL ||
            opcode == INS_NEXT || opcode == INS_IF || opcode == INS_MINUS_IF);
}


// Decode an instruction word into slot strings
// Returns number of valid slots (1-4)
static int decode_instruction(node_t* np, uint18_t addr, uint18_t word,
                              char slots[4][16], uint18_t dests[4])
{
    uint32_t II = (word ^ IMASK) << 2;
    int num_slots = 0;

    for (int s = 0; s < 4; s++) {
        int opcode = (II >> 15) & MASK5;
        dests[s] = 0;

        if (is_branch_opcode(opcode)) {
            // Calculate destination address
            uint18_t dest;
	    int i;
	    switch(s) {
	    case 0: dest = (addr & ~MASK10) | (word & MASK10); break;
            case 1: dest = (addr & ~MASK8) | (word & MASK8); break;
            case 2: dest = (addr & ~MASK3) | (word & MASK3); break;
	    default: dest = 0; // Slot 3 branch not possible
	    }
            dests[s] = dest;
	    if ((i = find_symbol_by_value(dest, np->symtab)) >= 0)
		snprintf(slots[s], 16, "%s:%s", f18_ins_name[opcode],
			 np->symtab->symbol[i].name);
	    else
		snprintf(slots[s], 16, "%s:%03x", f18_ins_name[opcode], dest);
            num_slots = s + 1;
            break;  // Branch ends instruction
        } else if (opcode == INS_RETURN || opcode == INS_EXECUTE) {
            snprintf(slots[s], 16, "%s", f18_ins_name[opcode]);
            num_slots = s + 1;
            break;  // These also end instruction
        } else {
            snprintf(slots[s], 16, "%s", f18_ins_name[opcode]);
            num_slots = s + 1;
        }
        II <<= 5;
    }

    // Mark remaining slots as empty
    for (int s = num_slots; s < 4; s++) {
        slots[s][0] = '\0';
    }

    return num_slots;
}

void tui_draw_disasm(node_t* np)
{
    attron(COLOR_PAIR(COLOR_BORDER));
    char title[32];
    snprintf(title, sizeof(title), "Disasm [slot %d]", g_debugger.current_slot);
    draw_box(disasm_top, disasm_left, disasm_height, disasm_width, title);
    attroff(COLOR_PAIR(COLOR_BORDER));

    if (!np) return;

    int y = disasm_top + 1;
    // Use tracked PC from debugger (correct even when P has been incremented)
    uint18_t pc = g_debugger.current_pc & MASK9;
    int cur_slot = g_debugger.current_slot;

    // Show 2 instructions before and 2 after current
    int start = (pc > 2) ? pc - 2 : 0;

    for (int i = 0; i < 5 && y < disasm_top + disasm_height - 1; i++) {
        uint18_t addr = start + i;
        uint18_t word;

        // For current instruction, use tracked word (guaranteed correct)
        if (addr == pc) {
            word = g_debugger.current_iword;
        } else if (addr <= RAM_END2) {
            word = np->ram[addr & MASK6];
        } else if (addr <= ROM_END2 && np->rom) {
            word = np->rom[(addr - ROM_START) & MASK6];
        } else {
            word = 0;
        }

        // Decode instruction
        char slots[4][16];
        uint18_t dests[4];
        int num_slots = decode_instruction(np, addr, word, slots, dests);

        int is_current = (addr == pc);
        mvprintw(y, disasm_left + 1, "%c%03x:", is_current ? '>' : ' ', addr);

        // Print each slot
        int x = disasm_left + 6;
        for (int s = 0; s < 4; s++) {
            int width = (s < num_slots && strlen(slots[s]) > 5) ? 16 : 6;
            if (s >= num_slots || slots[s][0] == '\0') {
                mvprintw(y, x, "%*s", width, "");  // Empty slot
            } else if (is_current && s == cur_slot) {
                attron(A_REVERSE | A_BOLD);
                mvprintw(y, x, " %-*s", width - 1, slots[s]);
                attroff(A_REVERSE | A_BOLD);
            } else if (is_current && s < cur_slot) {
                attron(A_DIM);
                mvprintw(y, x, " %-*s", width - 1, slots[s]);
                attroff(A_DIM);
            } else {
                mvprintw(y, x, " %-*s", width - 1, slots[s]);
            }
            x += width;
        }
        y++;
    }
}

void tui_draw_ram(node_t* np)
{
    attron(COLOR_PAIR(COLOR_BORDER));
    draw_box(ram_top, ram_left, ram_height, ram_width, "RAM");
    attroff(COLOR_PAIR(COLOR_BORDER));

    if (!np) return;

    int y = ram_top + 1;
    int addr = 0;

    for (int row = 0; row < 5 && y < ram_top + ram_height - 1; row++) {
        mvprintw(y, ram_left + 1, "%02x:", addr);
        for (int col = 0; col < 4 && addr < 64; col++) {
            mvprintw(y, ram_left + 5 + col * 7, "%05x", np->ram[addr]);
            addr++;
        }
        y++;
    }
}

void tui_draw_command(void)
{
    attron(COLOR_PAIR(COLOR_BORDER));
    draw_box(cmd_top, cmd_left, cmd_height, cmd_width, "Command");
    attroff(COLOR_PAIR(COLOR_BORDER));

    mvprintw(cmd_top + 1, cmd_left + 1, "> %s_", cmd_line);

    // Show different hints based on mode
    if (g_debugger.mode == DBG_MODE_RUN) {
        attron(A_BOLD | A_BLINK);
        mvprintw(cmd_top + 1, cmd_left + cmd_width - 30, "RUNNING - ESC/p to pause");
        attroff(A_BOLD | A_BLINK);
    } else {
        mvprintw(cmd_top + 1, cmd_left + cmd_width - 42,
                 "?:help TAB:slot SPC:inst c:run ESC:pause");
    }
}

void tui_draw_help(void)
{
    if (!show_help) return;

    int h = 16, w = 50;
    int y = (term_rows - h) / 2;
    int x = (term_cols - w) / 2;

    attron(COLOR_PAIR(COLOR_TITLE));
    draw_box(y, x, h, w, "Help");

    mvprintw(y + 1, x + 2, "Keyboard Commands:");
    mvprintw(y + 3, x + 2, "TAB / s   Step one slot (micro-step)");
    mvprintw(y + 4, x + 2, "SPACE / S Step one instruction word");
    mvprintw(y + 5, x + 2, "n         Step over call/loop");
    mvprintw(y + 6, x + 2, "c         Continue");
    mvprintw(y + 7, x + 2, "p         Pause");
    mvprintw(y + 8, x + 2, "f         Cycle focus node");
    mvprintw(y + 9, x + 2, "r         Refresh display");
    mvprintw(y + 10, x + 2, "b         Set breakpoint");
    mvprintw(y + 11, x + 2, "q         Quit");
    mvprintw(y + 13, x + 2, "Press any key to close");

    attroff(COLOR_PAIR(COLOR_TITLE));
}

void tui_refresh(void)
{
    erase();

    node_t* np = tui_get_focused_node();

    tui_draw_title();
    tui_draw_grid();
    tui_draw_registers(np);
    tui_draw_stacks(np);
    tui_draw_disasm(np);
    tui_draw_ram(np);
    tui_draw_command();
    tui_draw_help();

    refresh();
}


int tui_handle_input(int ch)
{
    if (show_help) {
        show_help = 0;
        return 0;
    }

    switch (ch) {
    case '\t':
    case 's':
    case KEY_F(10):
        debug_step_slot(1);  // Micro-step: one slot
        break;

    case ' ':
    case 'S':
    case KEY_F(11):
        debug_step_inst(1);  // Step one instruction word
        break;

    case 'n':
        debug_step_over();
        break;

    case 'c':
    case KEY_F(5):
        debug_continue();
        break;

    case 'p':
    case 27:  // ESC key
        debug_pause();
        break;

    case 'r':
    case KEY_F(12):
        // Refresh - just redraw
        break;

    case 'q':
    case 'Q':
        debug_quit();
        return 1;  // Exit

    case KEY_F(1):
    case 'h':
    case '?':
        show_help = 1;
        break;

    case 'f': {
        // Cycle to next step node
        int idx = NODE_ID_TO_INDEX(g_debugger.focus_node);
        for (int i = 1; i <= MAX_NODES; i++) {
            int next = (idx + i) % MAX_NODES;
            int id = INDEX_TO_NODE_ID(next);
            if (debug_is_step_node(id)) {
                debug_set_focus(id);
                break;
            }
        }
        break;
    }

    case KEY_UP:
        g_debugger.cursor_row = (g_debugger.cursor_row + 1) % g_v;
        break;

    case KEY_DOWN:
        g_debugger.cursor_row = (g_debugger.cursor_row + g_v - 1) % g_v;
        break;

    case KEY_LEFT:
        g_debugger.cursor_col = (g_debugger.cursor_col + g_h - 1) % g_h;
        break;

    case KEY_RIGHT:
        g_debugger.cursor_col = (g_debugger.cursor_col + 1) % g_h;
        break;

    case '\n':
    case KEY_ENTER:
        // Toggle step on cursor node or set focus
        {
            int id = MAKE_ID(g_debugger.cursor_row, g_debugger.cursor_col);
            if (debug_is_step_node(id))
                debug_set_focus(id);
            else
                debug_set_step_node(id, 1);
        }
        break;

    default:
        break;
    }

    return 0;
}

void debug_tui_main(void)
{
    int done = 0;
    int refresh_counter = 0;

    if (tui_init() < 0) {
        fprintf(stderr, "Failed to initialize TUI\n");
        return;
    }

    // Nodes start paused at barrier (mode=PAUSE, barrier_release=0)
    // User must press 's' or 'c' to start execution

    while (!done && g_debugger.mode != DBG_MODE_QUIT) {
        // When running, only refresh display every 5th iteration (250ms)
        // to reduce flicker, but check keys more often for responsive pause
        int is_running = (g_debugger.mode == DBG_MODE_RUN);

        if (!is_running || (refresh_counter % 5) == 0) {
            tui_refresh();
        }
        refresh_counter++;

        int ch = getch();
        if (ch != ERR) {
            done = tui_handle_input(ch);
        }

        // Shorter delay when running for responsive ESC handling
        napms(is_running ? 20 : 50);
    }

    tui_cleanup();
}
