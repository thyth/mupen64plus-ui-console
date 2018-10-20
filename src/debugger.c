/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-ui-console - debugger.c                                   *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2014 Will Nayes                                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "core_interface.h"
#include "debugger.h"

#include <SDL.h>

/*
 * Variables
 */

// General Purpose Register names
const char *register_names[] = {
    "$r0",
    "$at",
    "v0", "v1",
    "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9",
    "k0", "k1",
    "$gp",
    "$sp",
    "sB",
    "$ra"
};

// Holds the previous GPR values for comparison details.
long long int prev_reg_values[32];
char reg_ran_previously = 0;

// Used to wait for core response before requesting next command.
int debugger_loop_wait = 1;

// Counter indicating the number of DebugStep() calls we need to make yet.
static int debugger_steps_pending = 0;

// Keep track of the run state.
static int cur_run_state = 0;

// Remember the current program counter.
static unsigned int cur_pc = 0;

// Keep track of breakpoints locally.
static m64p_breakpoint *breakpoints;
static int num_breakpoints = 0;

/*
 * Debugger callbacks.
 */
void dbg_frontend_init() {
    breakpoints = (m64p_breakpoint *) malloc(BREAKPOINTS_MAX_NUMBER * sizeof(m64p_breakpoint));
    printf("Debugger initialized.\n");
}

void dbg_frontend_update(unsigned int pc) {
    cur_pc = pc;
    if (!debugger_steps_pending) {
        printf("\nPC at 0x%08X.\n", pc);
        debugger_loop_wait = 0;
        cur_run_state = 0;
    }
    else {
        --debugger_steps_pending;
        debugger_step();
    }
}

void dbg_frontend_vi() {
    //printf("Debugger vertical int.\n");
}

/*
 * Debugger methods.
 */
int debugger_setup_callbacks() {
    m64p_error rval = (*DebugSetCallbacks)(dbg_frontend_init,
                                           dbg_frontend_update,
                                           dbg_frontend_vi);
    return rval != M64ERR_SUCCESS;
}

int debugger_set_run_state(int state) {
    m64p_error rval = (*DebugSetRunState)((m64p_dbg_runstate) state);
    return rval != M64ERR_SUCCESS;
}

int debugger_step() {
    m64p_error rval = (*DebugStep)();
    return rval != M64ERR_SUCCESS;
}

// Retrieve the program counter.
int debugger_get_prev_pc() {
    return (*DebugGetState)(M64P_DBG_PREVIOUS_PC);
}

int64_t debugger_read_64(unsigned int addr) {
    return (*DebugMemRead64)(addr);
}
int debugger_read_32(unsigned int addr) {
    return (*DebugMemRead32)(addr);
}
int debugger_read_16(unsigned int addr) {
    return (*DebugMemRead16)(addr);
}
int debugger_read_8(unsigned int addr) {
    return (*DebugMemRead8)(addr);
}

void debugger_write_64(unsigned int addr, unsigned long long value) {
    (*DebugMemWrite64)(addr, value);
}
void debugger_write_32(unsigned int addr, unsigned int value) {
    (*DebugMemWrite32)(addr, value);
}
void debugger_write_16(unsigned int addr, unsigned short value) {
    (*DebugMemWrite16)(addr, value);
}
void debugger_write_8(unsigned int addr, unsigned char value) {
    (*DebugMemWrite8)(addr, value);
}

int debugger_print_registers() {
    unsigned long long int *regs = (unsigned long long int *) (*DebugGetCPUDataPtr)(M64P_CPU_REG_REG);
    if (regs == NULL)
        return -1;

    printf("General Purpose Registers:\n");
    int i;
    const char *format_padded = "%4s %016llX ";
    const char *format_nopad = "%4s %16llX ";
    for (i = 0; i < 32; ++i) {
        char val_changed = reg_ran_previously && regs[i] != prev_reg_values[i];

        // Use bold font if the value has changed since last time.
        if (val_changed)
            printf("%c[1m", 27); // Bold on

        // Print the register value, no padding if it is all zeroes. 
        printf(regs[i] == 0 ? format_nopad : format_padded,
               register_names[i], regs[i]);

        // Unset bold.
        if (val_changed)
            printf("%c[0m", 27); // Bold off

        reg_ran_previously = 1;
        prev_reg_values[i] = regs[i];

        // Two registers per line.
        if (i % 2 != 0)
            printf("\n");
    }
    return 0;
}

char *debugger_decode_op(unsigned int instruction, int instruction_addr,
                         char *output) {
    if (output == NULL)
        output = (char *) calloc(40, sizeof(char));

    (*DebugDecodeOp)(instruction, output, output + 10, instruction_addr);
    return output;
}

/*
 * Debugger main loop
 */
int debugger_loop(void *arg) {
    char input[256];
    while (1) {
        if (debugger_loop_wait) {
            SDL_Delay(1);
            continue;
        }

        printf("(dbg) ");
        if (fgets(input, 256, stdin) == NULL) {
            break;
        }
        input[strlen(input) - 1] = 0;

        if (strcmp(input, "run") == 0) {
            cur_run_state = 2;
            if (debugger_set_run_state(cur_run_state))
                printf("Error setting run_state: run\n");
            else {
                debugger_step(); // Hack to kick-start the emulation.
            }
        }
        else if (strcmp(input, "pause") == 0) {
            cur_run_state = 0;
            if (debugger_set_run_state(cur_run_state))
                printf("Error setting run_state: pause\n");
        }
        else if (strncmp(input, "step", 4) == 0) {
            if (cur_run_state == 2) {
              printf("Cannot step while running. Type `pause' first.\n");
              continue;
            }

            debugger_loop_wait = 1;
            debugger_steps_pending = 1;
            sscanf(input, "step %d", &debugger_steps_pending);
            if (debugger_steps_pending < 1)
                debugger_steps_pending = 1;
            --debugger_steps_pending;
            debugger_step();
        }
        else if (strcmp(input, "regs") == 0) {
            debugger_print_registers();
        }
        else if (strcmp(input, "pc") == 0) {
            printf("PC: %08X\n", cur_pc);
        }
        else if (strcmp(input, "pc-1") == 0) {
            printf("Previous PC: %08X\n", debugger_get_prev_pc());
        }
        else if (strcmp(input, "asm") == 0) {
            char decoded[64];
            debugger_decode_op(debugger_read_32(cur_pc), cur_pc, decoded);
            printf("%s", decoded);
            printf(" %s\n", decoded + 10);
        }
        else if (strncmp(input, "disasm", 6) == 0) {
            // simple linear sweep disassembly
            uint32_t addr, size=1, flags;

            int i;
            uint32_t lookupAddr, lookupData;
            char op[64];
            char args[64];

            if (sscanf(input, "disasm %i %i %i", &addr, &size, &flags) == 3) {
            } else if (sscanf(input, "disasm %i %i", &addr, &size) == 2) {
            } else if (sscanf(input, "disasm %i", &addr) == 1) {
            } else {
                printf("Improperly formatted diassembly command: '%s'\n", input);
                continue;
            }
            addr &= ~0x03; // align to 4 byte boundary
            printf("Disassembly of %d instruction%s @ 0x%08x:\n", size, (size == 1 ? "" : "s"), addr);
            for (i = 0; i < size; i++) {
                lookupAddr = addr + (i * 4);
                lookupData = debugger_read_32(lookupAddr);
                (*DebugDecodeOp)(lookupData, op, args, lookupAddr);
                if (flags & 0x01) {
                    printf("% 3d ", i);
                }
                if (flags & 0x02) {
                    printf("%08x ", lookupAddr);
                }
                if (flags & 0x04) {
                    printf("[%08x] ", lookupData);
                }
                printf("%s %s\n", op, args);
            }
        }
        else if (strncmp(input, "mem", 3) == 0) {
            uint32_t readAddr, length=1, rows=1, size=4;
            uint32_t i, j;
            char chSize;
            if (sscanf(input, "mem /%ux%u%c %i", &rows, &length, &chSize, &readAddr) == 4 && (chSize == 'b' || chSize == 'h' || chSize == 'w' || chSize == 'd'))
            {
                if (chSize == 'b')
                    size = 1;
                else if (chSize == 'h')
                    size = 2;
                else if (chSize == 'w')
                    size = 4;
                else // chSize == 'd'
                    size = 8;
            }
            else if (sscanf(input, "mem /%ux%u %i", &rows, &length, &readAddr) == 3)
            {
            }
            else if (sscanf(input, "mem /%u%c %i", &length, &chSize, &readAddr) == 3 && (chSize == 'b' || chSize == 'h' || chSize == 'w' || chSize == 'd'))
            {
                rows = 1;
                if (chSize == 'b')
                    size = 1;
                else if (chSize == 'h')
                    size = 2;
                else if (chSize == 'w')
                    size = 4;
                else // chSize == 'd'
                    size = 8;
            }
            else if (sscanf(input, "mem /%u %i", &length, &readAddr) == 2)
            {
                rows = 1;
            }
            else if (sscanf(input, "mem %i", &readAddr) == 1)
            {
                rows = 1;
                length = 1;
            }
            else
            {
                printf("Improperly formatted memory read command: '%s'\n", input);
                continue;
            }
            for (i = 0; i < rows; i++)
            {
                for (j = 0; j < length; j++)
                {
                    uint32_t thisAddr = readAddr + ((i * length) + j) * size;
                    switch(size)
                    {
                        case 1:
                            printf("%02x ", debugger_read_8(thisAddr));
                            break;
                        case 2:
                            printf("%04x ", debugger_read_16(thisAddr));
                            break;
                        case 4:
                            printf("%08x ", debugger_read_32(thisAddr));
                            break;
                        case 8:
                            printf("%016llx ", (long long unsigned int) debugger_read_64(thisAddr));
                            break;
                    }
                }
                printf("\n");
            }
        }
        else if (strncmp(input, "mflags", 6) == 0) {
            uint32_t addr;
            if (sscanf(input, "mflags %i", &addr) == 1) {
            } else {
                printf("Improperly formatted memory flags command: '%s'\n", input);
                continue;
            }
            int memInfo = (*DebugMemGetMemInfo)(M64P_DBG_MEM_FLAGS, addr);
            printf("Memory Flags @ 0x%08x: 0x%08x [", addr, memInfo);
            if (memInfo & M64P_MEM_FLAG_READABLE) {
                printf("M64P_MEM_FLAG_READABLE ");
            }
            if (memInfo & M64P_MEM_FLAG_WRITABLE) {
                printf("M64P_MEM_FLAG_WRITABLE ");
            }
            if (memInfo & M64P_MEM_FLAG_READABLE_EMUONLY) {
                printf("M64P_MEM_FLAG_READABLE_EMUONLY ");
            }
            if (memInfo & M64P_MEM_FLAG_WRITABLE_EMUONLY) {
                printf("M64P_MEM_FLAG_WRITABLE_EMUONLY ");
            }
            printf("]\n");
        }
        else if (strncmp(input, "write", 5) == 0) {
            uint32_t writeAddr, size=1;
            uint64_t writeVal;
            char chSize;
            if (sscanf(input, "write %i %c %llx", &writeAddr, &chSize, &writeVal) == 3 &&
                (chSize == 'b' || chSize == 'h' || chSize == 'w' || chSize == 'd')) {
                if (chSize == 'b') {
                    size = 1;
                } else if (chSize == 'h') {
                    size = 2;
                } else if (chSize == 'w') {
                    size = 4;
                } else {
                    size = 8;
                }
            } else if (sscanf(input, "write %i %llx", &writeAddr, &writeVal) == 2) {
            } else {
                printf("Improperly formatted memory write command: '%s'\n", input);
                continue;
            }

            switch(size) {
                case 1:
                    debugger_write_8(writeAddr, (unsigned char)writeVal);
                    printf("0x%08x <- 0x%02x\n", writeAddr, (unsigned char)writeVal);
                    break;
                case 2:
                    debugger_write_16(writeAddr, (unsigned short)writeVal);
                    printf("0x%08x <- 0x%04x\n", writeAddr, (unsigned short)writeVal);
                    break;
                case 4:
                    debugger_write_32(writeAddr, (unsigned int)writeVal);
                    printf("0x%08x <- 0x%08x\n", writeAddr, (unsigned int)writeVal);
                    break;
                case 8:
                    debugger_write_64(writeAddr, writeVal);
                    printf("0x%08x <- 0x%016llx\n", writeAddr, writeVal);
                    break;
            }
        }
        else if (strcmp(input, "bp list") == 0 || strcmp(input, "bp ls") == 0) {
            if (num_breakpoints == 0) {
                printf("No breakpoints added. Add with 'bp add 0x...'\n");
                continue;
            }

            printf("Breakpoints:\n");
            int i;
            unsigned int flags;
            for (i = 0; i < num_breakpoints; i++) {
                flags = breakpoints[i].flags;
                if (breakpoints[i].address == breakpoints[i].endaddr) {
                    printf("[%d] 0x%08X [%c%c%c]",
                           i, breakpoints[i].address,
                           flags & M64P_BKP_FLAG_READ ? 'R' : ' ',
                           flags & M64P_BKP_FLAG_WRITE ? 'W' : ' ',
                           flags & M64P_BKP_FLAG_EXEC ? 'X' : ' ');
                } else {
                    printf("[%d] 0x%08X - 0x%08X [%c%c%c]",
                           i, breakpoints[i].address, breakpoints[i].endaddr,
                           flags & M64P_BKP_FLAG_READ ? 'R' : ' ',
                           flags & M64P_BKP_FLAG_WRITE ? 'W' : ' ',
                           flags & M64P_BKP_FLAG_EXEC ? 'X' : ' ');
                }

                if ((breakpoints[i].flags & M64P_BKP_FLAG_ENABLED) == 0)
                    printf(" (Disabled)");
                printf("\n");
            }
        }
        else if (strncmp(input, "bp add ", 7) == 0) {
            unsigned int value = 0;
            if (strcmp(input, "bp add pc") == 0)
                value = cur_pc;
            else if (strncmp(input, "bp add 0x", 9) == 0) {
                sscanf(input, "bp add 0x%x", &value);
                if (value == 0)
                    sscanf(input, "bp add 0x%X", &value);
            }
            else {
                 sscanf(input, "bp add %x", &value);
                 if (value == 0)
                     sscanf(input, "bp add %X", &value);
            }

            if (value == 0) {
                printf("Invalid breakpoint address.\n");
                continue;
            }

            m64p_breakpoint bkpt;
            bkpt.address = value;
            bkpt.endaddr = value;
            bkpt.flags = M64P_BKP_FLAG_ENABLED |
                         M64P_BKP_FLAG_READ |
                         M64P_BKP_FLAG_WRITE |
                         M64P_BKP_FLAG_EXEC |
                         M64P_BKP_FLAG_LOG;
            int numBkps =
                (*DebugBreakpointCommand)(M64P_BKP_CMD_ADD_STRUCT, 0, &bkpt);
            if (numBkps == -1) {
                printf("Maximum breakpoint limit already reached.\n");
                continue;
            }

            breakpoints[num_breakpoints] = bkpt;
            num_breakpoints++;
            printf("Added breakpoint at 0x%08X.\n", value);
        }
        else if (strncmp(input, "bp range ", 9) == 0) {
            uint32_t addr, size, flags;
            if (sscanf(input, "bp range %i %i %i", &addr, &size, &flags) == 3) {
            } else {
                printf("Improperly formatted breakpoint range command: '%s'\n", input);
                continue;
            }

            if (addr == 0) {
                printf("Invalid breakpoint address.\n");
                continue;
            }

            m64p_breakpoint bkpt;
            bkpt.address = addr;
            bkpt.endaddr = addr + size;
            bkpt.flags = M64P_BKP_FLAG_ENABLED |
                         flags |
                         M64P_BKP_FLAG_LOG;
            int numBkps =
                    (*DebugBreakpointCommand)(M64P_BKP_CMD_ADD_STRUCT, 0, &bkpt);
            if (numBkps == -1) {
                printf("Maximum breakpoint limit already reached.\n");
                continue;
            }

            breakpoints[num_breakpoints] = bkpt;
            num_breakpoints++;
            printf("Added breakpoint range for [0x%08X to 0x%08X].\n", addr, addr + size);
        }
        else if (strncmp(input, "bp lookup ", 10) == 0) {
            uint32_t addr, size, flags;
            if (sscanf(input, "bp lookup %i %i %i", &addr, &size, &flags) == 3) {
            } else {
                printf("Improperly formatted breakpoint lookup command: '%s'\n", input);
                continue;
            }

            printf("Looking up breakpoint at 0x%08x with size %d and flags %x...\n",
                    addr, size, flags);
            int bpIdx = (*DebugBreakpointLookup)(addr, size, flags);
            if (bpIdx == -1) {
                printf("\tBreakpoint not found.\n");
            } else {
                printf("\tBreakpoint index found: %d\n", bpIdx);
            }
        }
        else if (strncmp(input, "bp check ", 9) == 0) {
            uint32_t addr;
            if (sscanf(input, "bp check %i", &addr) == 1) {
            } else {
                printf("Improperly formatted breakpoint check command: '%s'\n", input);
                continue;
            }

            printf("Checking breakpoint at 0x%08x...\n", addr);
            int bpIdx = (*DebugBreakpointCommand)(M64P_BKP_CMD_CHECK, addr, NULL);
            if (bpIdx == -1) {
                printf("\tBreakpoint not found.\n");
            } else {
                printf("\tBreakpoint index found: %d\n", bpIdx);
            }
        }
        else if (strncmp(input, "bp rm ", 6) == 0) {
            int index = -1;
            unsigned int addr = 0;
            if (strncmp(input, "bp rm 0x", 8) == 0) {
                sscanf(input, "bp rm 0x%X", &addr);
                if (addr == 0)
                    sscanf(input, "bp rm 0x%x", &addr);

                int i;
                for (i = 0; i < num_breakpoints; i++) {
                    if (breakpoints[i].address == addr)
                        index = i;
                }
            }
            else {
                sscanf(input, "bp rm %d", &index);
                if (index >= 0 && index < num_breakpoints)
                    addr = breakpoints[index].address;
            }

            if (index == -1 || addr == 0) {
                printf("Invalid value passed. ");
                printf("Pass an address or breakpoint number.\n");
                continue;
            }

            (*DebugBreakpointCommand)(M64P_BKP_CMD_REMOVE_IDX, index, NULL);
            num_breakpoints--;
            printf("Breakpoint [%d] 0x%08X removed.\n", index, addr);

            // Shift the array elements ahead of index down.
            int j;
            for (j = index + 1; j < num_breakpoints; j++) {
                breakpoints[j - 1] = breakpoints[j];
            }

        }
        else if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
            (*CoreDoCommand)(M64CMD_STOP, 0, NULL);
            break;
        }
        else if (strlen(input) == 0)
            continue;
        else
            printf("Unrecognized: %s\n", input);
    }

    return -1;
}

