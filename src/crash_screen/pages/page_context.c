#include <ultra64.h>

#include "types.h"
#include "sm64.h"

#include "crash_screen/crash_controls.h"
#include "crash_screen/crash_draw.h"
#include "crash_screen/crash_main.h"
#include "crash_screen/crash_pages.h"
#include "crash_screen/crash_print.h"
#include "crash_screen/crash_settings.h"
#include "crash_screen/map_parser.h"
#include "crash_screen/memory_read.h"

#include "page_context.h"

#ifdef UNF
#include "usb/debug.h"
#endif


struct CSSetting cs_settings_group_page_context[] = {
    [CS_OPT_HEADER_PAGE_CONTEXT ] = { .type = CS_OPT_TYPE_HEADER,  .name = "CONTEXT",                        .valNames = &gValNames_bool,          .val = SECTION_EXPANDED_DEFAULT,  .defaultVal = SECTION_EXPANDED_DEFAULT,  .lowerBound = FALSE,                 .upperBound = TRUE,                       },
#ifdef INCLUDE_DEBUG_MAP
    [CS_OPT_CONTEXT_PARSE_REG   ] = { .type = CS_OPT_TYPE_SETTING, .name = "Parse register addr names",      .valNames = &gValNames_bool,          .val = FALSE,                     .defaultVal = FALSE,                     .lowerBound = FALSE,                 .upperBound = TRUE,                       },
#endif
    [CS_OPT_CONTEXT_FLOATS_FMT  ] = { .type = CS_OPT_TYPE_SETTING, .name = "Floats print format",            .valNames = &gValNames_print_num_fmt, .val = PRINT_NUM_FMT_DEC,         .defaultVal = PRINT_NUM_FMT_DEC,         .lowerBound = PRINT_NUM_FMT_HEX,     .upperBound = PRINT_NUM_FMT_SCI,          },
    [CS_OPT_END_CONTEXT         ] = { .type = CS_OPT_TYPE_END, },
};


const enum ControlTypes cs_cont_list_context[] = {
    CONT_DESC_SWITCH_PAGE,
    CONT_DESC_SHOW_CONTROLS,
    CONT_DESC_CYCLE_DRAW,
#ifdef UNF
    CONT_DESC_OS_PRINT,
#endif
    CONT_DESC_CYCLE_FLOATS_MODE,
    CONT_DESC_LIST_END,
};


static const ThreadIDName sThreadIDNames[] = {
    { .threadID = THREAD_0,                   .name = "0",              },
    { .threadID = THREAD_1_IDLE,              .name = "idle",           },
    { .threadID = THREAD_2,                   .name = "2",              },
    { .threadID = THREAD_3_MAIN,              .name = "main",           },
    { .threadID = THREAD_4_SOUND,             .name = "sound",          },
    { .threadID = THREAD_5_GAME_LOOP,         .name = "game loop",      },
    { .threadID = THREAD_6_RUMBLE,            .name = "rumble",         },
    { .threadID = THREAD_7_HVQM,              .name = "HVQM",           },
    { .threadID = THREAD_8_TIMEKEEPER,        .name = "timekeeper",     },
    { .threadID = THREAD_9_DA_COUNTER,        .name = "DA counter",     },
    { .threadID = THREAD_1000_CRASH_SCREEN_0, .name = "Crash Screen 0", },
    { .threadID = THREAD_1001_CRASH_SCREEN_1, .name = "Crash Screen 1", },
    { .threadID = THREAD_1002_CRASH_SCREEN_2, .name = "Crash Screen 2", },
};

static const char* sCauseDesc[18] = {
    /*EXC_INT       */ "Interrupt",
    /*EXC_MOD       */ "TLB modification",
    /*EXC_RMISS     */ "TLB exception on load or inst.",
    /*EXC_WMISS     */ "TLB exception on store",
    /*EXC_RADE      */ "Address error on load or inst.",
    /*EXC_WADE      */ "Address error on store",
    /*EXC_IBE       */ "Bus error on inst.",
    /*EXC_DBE       */ "Bus error on data",
    /*EXC_SYSCALL   */ "Failed Assert: See Assert Page",
    /*EXC_BREAK     */ "Breakpoint exception",
    /*EXC_II        */ "Reserved instruction",
    /*EXC_CPU       */ "Coprocessor unusable",
    /*EXC_OV        */ "Arithmetic overflow",
    /*EXC_TRAP      */ "Trap exception",
    /*EXC_VCEI      */ "Virtual coherency on inst.",
    /*EXC_FPE       */ "Floating point exception",
    /*EXC_WATCH     */ "Watchpoint exception",
    /*EXC_VCED      */ "Virtual coherency on data",
};

static const char* sFpcsrDesc[6] = {
    /*FPCSR_CE      */ "Unimplemented operation",
    /*FPCSR_CV      */ "Invalid operation",
    /*FPCSR_CZ      */ "Division by zero",
    /*FPCSR_CO      */ "Overflow",
    /*FPCSR_CU      */ "Underflow",
    /*FPCSR_CI      */ "Inexact operation",
};

// CPU register names.
//! TODO: Combine this with sCPURegisterNames in insn_disasm.c.
static const char* sRegNames[29] = {
    "AT", "V0", "V1",
    "A0", "A1", "A2",
    "A3", "T0", "T1",
    "T2", "T3", "T4",
    "T5", "T6", "T7",
    "S0", "S1", "S2",
    "S3", "S4", "S5",
    "S6", "S7", "T8",
    "T9", "GP", "SP",
    "S8", "RA",
};


// Returns a CAUSE description from 'sCauseDesc'.
static const char* get_cause_desc(u32 cause) {
    // Make the last two cause case indexes sequential for array access.
    switch (cause) {
        case EXC_WATCH: cause = 16; break; // 23 -> 16
        case EXC_VCED:  cause = 17; break; // 31 -> 17
        default:        cause = ((cause >> CAUSE_EXCSHIFT) & BITMASK(5)); break;
    }

    if (cause < ARRAY_COUNT(sCauseDesc)) {
        return sCauseDesc[cause];
    }

    return NULL;
}

// Returns a FPCSR description from 'sFpcsrDesc'.
static const char* get_fpcsr_desc(u32 fpcsr) {
    u32 bit = BIT(17);

    for (u32 i = 0; i < ARRAY_COUNT(sFpcsrDesc); i++) {
        if (fpcsr & bit) {
            return sFpcsrDesc[i];
        }

        bit >>= 1;
    }

    return NULL;
}

// Returns a thread name from 'sThreadIDNames'.
static const char* get_thread_name_from_id(enum ThreadID threadID) {
    const ThreadIDName* threadIDName = &sThreadIDNames[0];

    for (int i = 0; i < ARRAY_COUNT(sThreadIDNames); i++) {
        if (threadIDName->threadID == threadID) {
            return threadIDName->name;
        }

        threadIDName++;
    }

    return NULL;
}

void page_context_init(void) {

}

// Print a fixed-point register.
void cs_context_print_reg(u32 x, u32 y, const char* name, Word val) {
    const MapSymbol* symbol = NULL;

    // "[register name]:"
    size_t charX = cs_print(x, y,
        " "STR_COLOR_PREFIX"%s:",
        COLOR_RGBA32_CRASH_VARIABLE, name
    );

#ifdef INCLUDE_DEBUG_MAP
    if (cs_get_setting_val(CS_OPT_GROUP_PAGE_CONTEXT, CS_OPT_CONTEXT_PARSE_REG)) {
        symbol = get_map_symbol(val, SYMBOL_SEARCH_BACKWARD);
    }
#endif

    if (symbol != NULL) {
        // "[symbol name]"
        cs_print_symbol_name((x + TEXT_WIDTH(charX)), y, 10, symbol);
    } else {
        // "[XXXXXXXX]"
        cs_print((x + TEXT_WIDTH(charX + STRLEN(" "))), y,
            STR_COLOR_PREFIX STR_HEX_WORD,
            COLOR_RGBA32_WHITE, val
        );
    }
}

// Print important fixed-point registers.
u32 cs_context_print_registers(u32 line, __OSThreadContext* tc) {
    const u32 rows    = 10;
    const u32 columns = 3;
    const size_t columnWidth = 15;

    u32 regNum = 0;
    Register* reg = &tc->at;

    cs_context_print_reg(TEXT_X(0 * columnWidth), TEXT_Y(line), "PC", tc->pc);
    cs_context_print_reg(TEXT_X(1 * columnWidth), TEXT_Y(line), "SR", tc->sr);
    cs_context_print_reg(TEXT_X(2 * columnWidth), TEXT_Y(line), "VA", tc->badvaddr);
    line++;

    Word data = 0;
    if (try_read_data(&data, tc->pc)) {
        cs_context_print_reg(TEXT_X((columns - 1) * columnWidth), TEXT_Y(line + (rows - 1)), "MM", data); // The raw data of the asm code that crashed.
    }

    osWritebackDCacheAll();

    for (u32 y = 0; y < rows; y++) {
        for (u32 x = 0; x < columns; x++) {
            if (regNum >= ARRAY_COUNT(sRegNames)) {
                return (line + y);
            }

            cs_context_print_reg(TEXT_X(x * columnWidth), TEXT_Y(line + y), sRegNames[regNum], *(reg + regNum));

            regNum++;
        }
    }

    return (line + rows);
}

void cs_context_print_fpcsr(u32 x, u32 y, u32 fpcsr) {
    // "FPCSR:[XXXXXXXX]"
    size_t fpcsrSize = cs_print(x, y,
        STR_COLOR_PREFIX"FPCSR: "STR_COLOR_PREFIX STR_HEX_WORD" ",
        COLOR_RGBA32_CRASH_VARIABLE,
        COLOR_RGBA32_WHITE, fpcsr
    );
    x += TEXT_WIDTH(fpcsrSize);

    const char* fpcsrDesc = get_fpcsr_desc(fpcsr);
    if (fpcsrDesc != NULL) {
        // "([float exception description])"
        cs_print(x, y, STR_COLOR_PREFIX"(%s)", COLOR_RGBA32_CRASH_DESCRIPTION, fpcsrDesc);
    }
}

// Print a floating-point register.
void cs_context_print_float_reg(u32 x, u32 y, u32 regNum, f32* data) {
    // "[register name]:"
    size_t charX = cs_print(x, y, STR_COLOR_PREFIX"F%02d:", COLOR_RGBA32_CRASH_VARIABLE, regNum);
    x += TEXT_WIDTH(charX);

    IEEE754_f32 val = {
        .asF32 = *data,
    };

    char prefix = '\0';

    if (val.mantissa != 0) {
        if (val.exponent == 0x00) {
            prefix = 'D'; // Denormalized value.
        } else if (val.exponent == 0xFF) {
            prefix = 'N'; // NaN.
        }
    }

    if (prefix != '\0') {
        // "[prefix][XXXXXXXX]"
        cs_print(x, y, "%c"STR_HEX_WORD, prefix, val.asU32);
    } else {
        const enum CSPrintNumberFormats floatsFormat = cs_get_setting_val(CS_OPT_GROUP_PAGE_CONTEXT, CS_OPT_CONTEXT_FLOATS_FMT);

        switch (floatsFormat) {
            case PRINT_NUM_FMT_HEX: cs_print(x, y, " "STR_HEX_WORD, val.asU32); break; // "[XXXXXXXX]"
            default:
            case PRINT_NUM_FMT_DEC: cs_print(x, y, "% g",           val.asF32); break; // "[±][exponent]"
            case PRINT_NUM_FMT_SCI: cs_print(x, y, "% .3e",         val.asF32); break; // "[scientific notation]"
        }
    }
}

void cs_context_print_float_registers(u32 line, __OSThreadContext* tc) {
    const size_t columnWidth = 15;
    u32 regNum = 0;
    __OSfp* osfp = &tc->fp0;

    cs_context_print_fpcsr(TEXT_X(0), TEXT_Y(line), tc->fpcsr);
    line++;

    osWritebackDCacheAll();

    for (u32 y = 0; y < 6; y++) {
        for (u32 x = 0; x < 3; x++) {
            if (regNum > 30) {
                return;
            }

            cs_context_print_float_reg(TEXT_X(x * columnWidth), TEXT_Y(line + y), regNum, &osfp->f.f_even);

            osfp++;
            regNum += 2;
        }
    }
}

void page_context_draw(void) {
    __OSThreadContext* tc = &gCrashedThread->context;
    u32 line = 1;
    size_t charX = 0;

    const char* desc = get_cause_desc(tc->cause);
    if (desc != NULL) {
        // "CAUSE: ([exception cause description])"
        cs_print(TEXT_X(0), TEXT_Y(line),
            STR_COLOR_PREFIX"CAUSE:\t%s",
            COLOR_RGBA32_CRASH_DESCRIPTION, desc
        );
    }

    line++;

    // "THREAD: [thread id]"
    enum ThreadID threadID = gCrashedThread->id;
    charX = cs_print(TEXT_X(0), TEXT_Y(line), STR_COLOR_PREFIX"THREAD:\t%d",
        COLOR_RGBA32_CRASH_THREAD, threadID
    );
    if (threadID < NUM_THREADS) {
        const char* threadName = get_thread_name_from_id(threadID);

        if (threadName != NULL) {
            // "(thread name)"
            cs_print(TEXT_X(charX + STRLEN(" ")), TEXT_Y(line), STR_COLOR_PREFIX"(%s)",
                COLOR_RGBA32_CRASH_THREAD, threadName
            );
        }
    }
    line++;

    osWritebackDCacheAll();

#ifdef INCLUDE_DEBUG_MAP
    const MapSymbol* symbol = get_map_symbol(tc->pc, SYMBOL_SEARCH_BACKWARD);
    // "IN FUNC:"
    charX = cs_print(TEXT_X(0), TEXT_Y(line),
        STR_COLOR_PREFIX"FUNC:\t",
        COLOR_RGBA32_CRASH_AT
    );
    cs_print_symbol_name(TEXT_X(charX), TEXT_Y(line), (CRASH_SCREEN_NUM_CHARS_X - charX), symbol);
    line++;
#endif

    line = cs_context_print_registers(line, tc);
    line++;

    osWritebackDCacheAll();

    cs_context_print_float_registers(line, tc);
}

void page_context_input(void) {
    if (gCSCompositeController->buttonPressed & B_BUTTON) {
        // Cycle floats print mode.
        cs_inc_setting(CS_OPT_GROUP_PAGE_CONTEXT, CS_OPT_CONTEXT_FLOATS_FMT, 1);
    }
}

void page_context_print(void) {
#ifdef UNF
 #ifdef INCLUDE_DEBUG_MAP
    __OSThreadContext* tc = &gCrashedThread->context;
    const MapSymbol* symbol = get_map_symbol(tc->pc, SYMBOL_SEARCH_BACKWARD);
    if (symbol != NULL) {
        osSyncPrintf("func name\t%s\n", get_map_symbol_name(symbol)); //! TODO: only the name itself is printed.
    }
 #endif // INCLUDE_DEBUG_MAP
    debug_printcontext(gCrashedThread); //! TODO: fix line breaks and debug_printreg in usb/debug.c. Issue with UNFLoader itself?
#endif
}

struct CSPage gCSPage_context ={
    .name         = "CONTEXT",
    .initFunc     = page_context_init,
    .drawFunc     = page_context_draw,
    .inputFunc    = page_context_input,
    .printFunc    = page_context_print,
    .contList     = cs_cont_list_context,
    .settingsList = cs_settings_group_page_context,
    .flags = {
        .initialized = FALSE,
        .crashed     = FALSE,
        .printName   = FALSE,
    },
};
