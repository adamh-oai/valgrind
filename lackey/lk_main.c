
/*--------------------------------------------------------------------*/
/*--- An example Valgrind tool.                          lk_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Lackey, an example Valgrind tool that does
   some simple program measurement and tracing.

   Copyright (C) 2002-2017 Nicholas Nethercote
      njn@valgrind.org

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 3 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.

   The GNU General Public License is contained in the file COPYING.
*/

// This tool shows how to do some basic instrumentation.
//
// There are four kinds of instrumentation it can do.  They can be turned
// on/off independently with command line options:
//
// * --basic-counts   : do basic counts, eg. number of instructions
//                      executed, jumps executed, etc.
// * --detailed-counts: do more detailed counts:  number of loads, stores
//                      and ALU operations of different sizes.
// * --trace-mem=yes:   trace all (data) memory accesses.
// * --trace-superblocks=yes:   
//                      trace all superblock entries.  Mostly of interest
//                      to the Valgrind developers.
//
// The code for each kind of instrumentation is guarded by a clo_* variable:
// clo_basic_counts, clo_detailed_counts, clo_trace_mem and clo_trace_sbs.
//
// If you want to modify any of the instrumentation code, look for the code
// that is guarded by the relevant clo_* variable (eg. clo_trace_mem)
// If you're not interested in the other kinds of instrumentation you can
// remove them.  If you want to do more complex modifications, please read
// VEX/pub/libvex_ir.h to understand the intermediate representation.
//
//
// Specific Details about --trace-mem=yes
// --------------------------------------
// Lackey's --trace-mem code is a good starting point for building Valgrind
// tools that act on memory loads and stores.  It also could be used as is,
// with its output used as input to a post-mortem processing step.  However,
// because memory traces can be very large, online analysis is generally
// better.
//
// It prints memory data access traces that look like this:
//
//   I  0023C790,2  # instruction read at 0x0023C790 of size 2
//   I  0023C792,5
//    S BE80199C,4  # data store at 0xBE80199C of size 4
//   I  0025242B,3
//    L BE801950,4  # data load at 0xBE801950 of size 4
//   I  0023D476,7
//    M 0025747C,1  # data modify at 0x0025747C of size 1
//   I  0023DC20,2
//    L 00254962,1
//    L BE801FB3,1
//   I  00252305,1
//    L 00254AEB,1
//    S 00257998,1
//
// Every instruction executed has an "instr" event representing it.
// Instructions that do memory accesses are followed by one or more "load",
// "store" or "modify" events.  Some instructions do more than one load or
// store, as in the last two examples in the above trace.
//
// Here are some examples of x86 instructions that do different combinations
// of loads, stores, and modifies.
//
//    Instruction          Memory accesses                  Event sequence
//    -----------          ---------------                  --------------
//    add %eax, %ebx       No loads or stores               instr
//
//    movl (%eax), %ebx    loads (%eax)                     instr, load
//
//    movl %eax, (%ebx)    stores (%ebx)                    instr, store
//
//    incl (%ecx)          modifies (%ecx)                  instr, modify
//
//    cmpsb                loads (%esi), loads(%edi)        instr, load, load
//
//    call*l (%edx)        loads (%edx), stores -4(%esp)    instr, load, store
//    pushl (%edx)         loads (%edx), stores -4(%esp)    instr, load, store
//    movsw                loads (%esi), stores (%edi)      instr, load, store
//
// Instructions using x86 "rep" prefixes are traced as if they are repeated
// N times.
//
// Lackey with --trace-mem gives good traces, but they are not perfect, for
// the following reasons:
//
// - It does not trace into the OS kernel, so system calls and other kernel
//   operations (eg. some scheduling and signal handling code) are ignored.
//
// - It could model loads and stores done at the system call boundary using
//   the pre_mem_read/post_mem_write events.  For example, if you call
//   fstat() you know that the passed in buffer has been written.  But it
//   currently does not do this.
//
// - Valgrind replaces some code (not much) with its own, notably parts of
//   code for scheduling operations and signal handling.  This code is not
//   traced.
//
// - There is no consideration of virtual-to-physical address mapping.
//   This may not matter for many purposes.
//
// - Valgrind modifies the instruction stream in some very minor ways.  For
//   example, on x86 the bts, btc, btr instructions are incorrectly
//   considered to always touch memory (this is a consequence of these
//   instructions being very difficult to simulate).
//
// - Valgrind tools layout memory differently to normal programs, so the
//   addresses you get will not be typical.  Thus Lackey (and all Valgrind
//   tools) is suitable for getting relative memory traces -- eg. if you
//   want to analyse locality of memory accesses -- but is not good if
//   absolute addresses are important.
//
// Despite all these warnings, Lackey's results should be good enough for a
// wide range of purposes.  For example, Cachegrind shares all the above
// shortcomings and it is still useful.
//
// For further inspiration, you should look at cachegrind/cg_main.c which
// uses the same basic technique for tracing memory accesses, but also groups
// events together for processing into twos and threes so that fewer C calls
// are made and things run faster.
//
// Specific Details about --trace-superblocks=yes
// ----------------------------------------------
// Valgrind splits code up into single entry, multiple exit blocks
// known as superblocks.  By itself, --trace-superblocks=yes just
// prints a message as each superblock is run:
//
//  SB 04013170
//  SB 04013177
//  SB 04013173
//  SB 04013177
//
// The hex number is the address of the first instruction in the
// superblock.  You can see the relationship more obviously if you use
// --trace-superblocks=yes and --trace-mem=yes together.  Then a "SB"
// message at address X is immediately followed by an "instr:" message
// for that address, as the first instruction in the block is
// executed, for example:
//
//  SB 04014073
//  I  04014073,3
//   L 7FEFFF7F8,8
//  I  04014076,4
//  I  0401407A,3
//  I  0401407D,3
//  I  04014080,3
//  I  04014083,6


#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_options.h"
#include "pub_tool_machine.h"     // VG_(fnptr_to_fnentry)
#include "pub_tool_libcfile.h"
#include "pub_tool_libcproc.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_gdbserver.h"
#include "pub_tool_transtab.h"
#include "pub_tool_vki.h"
#include "pub_tool_vkiscnums.h"
#include "pub_tool_seqmatch.h"
#include "../callgrind/callgrind.h"

/*------------------------------------------------------------*/
/*--- Command line options                                 ---*/
/*------------------------------------------------------------*/

/* Command line options controlling instrumentation kinds, as described at
 * the top of this file. */
static Bool clo_basic_counts    = True;
static Bool clo_detailed_counts = False;
static Bool clo_trace_mem       = False;
static Bool clo_trace_sbs       = False;
static Bool instrumentation_enabled = True;

typedef struct _Pattern {
   HChar* text;
   struct _Pattern* next;
} Pattern;

static Pattern* function_patterns = NULL;
static Pattern* file_patterns = NULL;
static const HChar* clo_trace_output = NULL;

/* Keep tracing separate from Valgrind's diagnostics. A private descriptor
   prevents client close/dup operations from corrupting the trace sink. */
#define TRACE_BUFFER_SIZE 65536
static Int trace_fd = -1; /* -1 means the ordinary Valgrind log. */
static HChar* trace_path = NULL;
static HChar trace_buffer[TRACE_BUFFER_SIZE];
static SizeT trace_used = 0;

static void flush_trace(void)
{
   SizeT done = 0;
   while (done < trace_used) {
      Int written = VG_(write)(trace_fd, trace_buffer + done,
                               trace_used - done);
      if (written == -VKI_EINTR)
         continue;
      if (written <= 0) {
         VG_(umsg)("Lackey: cannot write trace output %s (error %d)\n",
                    trace_path, written < 0 ? -written : 0);
         VG_(exit)(1);
      }
      done += written;
   }
   trace_used = 0;
}

static void trace_printf(const HChar* format, ...) PRINTF_CHECK(1, 2);

static void trace_printf(const HChar* format, ...)
{
   va_list args;
   va_start(args, format);
   if (trace_fd < 0) {
      /* Preserve stock Lackey output when no dedicated sink is selected. */
      VG_(vprintf)(format, args);
   } else {
      HChar line[128];
      UInt size = VG_(vsnprintf)(line, sizeof(line), format, args);
      tl_assert(size < sizeof(line));
      if (trace_used + size > TRACE_BUFFER_SIZE)
         flush_trace();
      VG_(memcpy)(trace_buffer + trace_used, line, size);
      trace_used += size;
   }
   va_end(args);
}

/* Open first: a failed switch must not close or lose the previous sink.
   Append also makes nested A -> B -> A output contexts preserve both A parts. */
static Bool switch_trace_output(const HChar* path)
{
   Int new_fd = -1;
   HChar* new_path = NULL;
   if (VG_(strcmp)(path, "log") != 0) {
      SysRes result = VG_(open)(path, VKI_O_WRONLY | VKI_O_CREAT | VKI_O_APPEND,
                               VKI_S_IRUSR | VKI_S_IWUSR);
      if (sr_isError(result)) {
         VG_(umsg)("Lackey: cannot open trace output %s (error %lu); "
                    "keeping previous output\n", path, (UWord)sr_Err(result));
         return False;
      }
      new_fd = VG_(safe_fd)(sr_Res(result));
      new_path = VG_(strdup)("lk.trace_path", path);
   }
   flush_trace();
   if (trace_fd >= 0)
      VG_(close)(trace_fd);
   VG_(free)(trace_path);
   trace_fd = new_fd;
   trace_path = new_path;
   return True;
}

static void trace_before_fork(ThreadId tid)
{
   /* Neither process should inherit a second copy of unwritten records. */
   flush_trace();
}

static void trace_pre_syscall(ThreadId tid, UInt syscallno,
                              UWord* args, UInt nArgs)
{
   /* A successful exec replaces tool state without calling lk_fini. Flush
      before the attempt; a failed exec can continue using the same sink. */
   Bool may_exec = False;
#ifdef __NR_execve
   may_exec = may_exec || syscallno == __NR_execve;
#endif
#ifdef __NR_execveat
   may_exec = may_exec || syscallno == __NR_execveat;
#endif
#ifdef __NR_fexecve
   may_exec = may_exec || syscallno == __NR_fexecve;
#endif
#ifdef __NR_posix_spawn
   may_exec = may_exec || syscallno == __NR_posix_spawn;
#endif
   if (may_exec)
      flush_trace();
}

static void trace_post_syscall(ThreadId tid, UInt syscallno,
                               UWord* args, UInt nArgs, SysRes res)
{
   /* The syscall-wrapper interface requires both callbacks. */
}

static void add_pattern(Pattern** patterns, const HChar* text)
{
   Pattern* pattern = VG_(malloc)("lk.pattern", sizeof(*pattern));
   pattern->text = VG_(strdup)("lk.pattern.text", text);
   pattern->next = *patterns;
   *patterns = pattern;
}

static void clear_patterns(Pattern** patterns)
{
   while (*patterns) {
      Pattern* old = *patterns;
      *patterns = old->next;
      VG_(free)(old->text);
      VG_(free)(old);
   }
}

static Bool matches_patterns(const Pattern* patterns, const HChar* text)
{
   for (; patterns; patterns = patterns->next)
      if (VG_(string_match)(patterns->text, text))
         return True;
   return False;
}

/* Called only from the tool's client-request callback: Valgrind's wrapper
   permits safe invalidation there, unlike a dynamic CLO_CHANGE callback. */
static void discard_instrumentation(void)
{
   VG_(discard_translations_safely)((Addr)0x1000, ~(SizeT)0xfff, "lackey");
}

static void set_instrumentation(Bool enabled)
{
   if (!enabled)
      flush_trace();
   if (instrumentation_enabled != enabled) {
      instrumentation_enabled = enabled;
      discard_instrumentation();
   }
}

static Bool instruction_selected(DiEpoch ep, Addr address)
{
   const HChar* name;
   if (function_patterns) {
      Bool found = False;
      InlIPCursor* cursor = VG_(new_IIPC)(ep, address);
      do {
         if (VG_(get_fnname_inl)(ep, address, &name, cursor)
             && matches_patterns(function_patterns, name)) {
            found = True;
            break;
         }
      } while (VG_(next_IIPC)(cursor));
      VG_(delete_IIPC)(cursor);
      if (!found)
         return False;
   }
   if (file_patterns) {
      const HChar* directory;
      UInt line;
      Bool found;
      if (!VG_(get_filename_linenum)(ep, address, &name, &directory, &line))
         return False;
      found = matches_patterns(file_patterns, name);
      if (!found && directory && directory[0] && name[0] != '/') {
         SizeT size = VG_(strlen)(directory) + VG_(strlen)(name) + 2;
         HChar* path = VG_(malloc)("lk.source_path", size);
         VG_(snprintf)(path, size, "%s/%s", directory, name);
         found = matches_patterns(file_patterns, path);
         VG_(free)(path);
      }
      /* DWARF producers may put an absolute path in the filename field. */
      if (!found)
         found = matches_patterns(file_patterns, VG_(basename)(name));
      if (!found)
         return False;
   }
   return True;
}

static void print_control_help(void)
{
   VG_(gdb_printf)(
      "lackey instrumentation on|off\n"
      "lackey function add <glob> | lackey function clear\n"
      "lackey file add <glob> | lackey file clear\n"
      "lackey filters clear\n"
      "lackey output <path> | lackey output log\n"
      "lackey status\n");
}

static Bool handle_monitor_command(const HChar* command)
{
   const HChar* rest;
   Pattern* pattern;
   if (VG_(strcmp)(command, "help") == 0) {
      print_control_help();
      return True;
   }
   if (VG_(strncmp)(command, "lackey ", 7) != 0)
      return False;
   rest = command + 7;
   if (VG_(strcmp)(rest, "instrumentation on") == 0) {
      set_instrumentation(True);
   } else if (VG_(strcmp)(rest, "instrumentation off") == 0) {
      set_instrumentation(False);
   } else if (VG_(strncmp)(rest, "function add ", 13) == 0 && rest[13]) {
      add_pattern(&function_patterns, rest + 13);
      discard_instrumentation();
   } else if (VG_(strcmp)(rest, "function clear") == 0) {
      clear_patterns(&function_patterns);
      discard_instrumentation();
   } else if (VG_(strncmp)(rest, "file add ", 9) == 0 && rest[9]) {
      add_pattern(&file_patterns, rest + 9);
      discard_instrumentation();
   } else if (VG_(strcmp)(rest, "file clear") == 0) {
      clear_patterns(&file_patterns);
      discard_instrumentation();
   } else if (VG_(strcmp)(rest, "filters clear") == 0) {
      clear_patterns(&function_patterns);
      clear_patterns(&file_patterns);
      discard_instrumentation();
   } else if (VG_(strncmp)(rest, "output ", 7) == 0 && rest[7]) {
      return switch_trace_output(rest + 7);
   } else if (VG_(strcmp)(rest, "status") == 0) {
      flush_trace();
      VG_(gdb_printf)("instrumentation: %s\noutput: %s\n",
                      instrumentation_enabled ? "on" : "off",
                      trace_path ? trace_path : "log");
      for (pattern = function_patterns; pattern; pattern = pattern->next)
         VG_(gdb_printf)("function: %s\n", pattern->text);
      for (pattern = file_patterns; pattern; pattern = pattern->next)
         VG_(gdb_printf)("file: %s\n", pattern->text);
   } else {
      VG_(gdb_printf)("Lackey: invalid control command: %s\n", command);
      print_control_help();
      return False;
   }
   return True;
}

static Bool handle_client_request(ThreadId tid, UWord* args, UWord* ret)
{
   switch (args[0]) {
      case VG_USERREQ__START_INSTRUMENTATION:
         set_instrumentation(True);
         *ret = 0;
         return True;
      case VG_USERREQ__STOP_INSTRUMENTATION:
         set_instrumentation(False);
         *ret = 0;
         return True;
      case VG_USERREQ__GDB_MONITOR_COMMAND: {
         Bool handled = handle_monitor_command((const HChar*)args[1]);
         *ret = handled ? 1 : 0;
         return handled;
      }
      default:
         return False;
   }
}

/* The name of the function of which the number of calls (under
 * --basic-counts=yes) is to be counted, with default. Override with command
 * line option --fnname. */
static const HChar* clo_fnname = "main";

static Bool lk_process_cmd_line_option(const HChar* arg)
{
   const HChar* pattern;
   if VG_STR_CLO(arg, "--fnname", clo_fnname) {}
   else if VG_BOOL_CLO(arg, "--basic-counts",      clo_basic_counts) {}
   else if VG_BOOL_CLO(arg, "--detailed-counts",   clo_detailed_counts) {}
   else if VG_BOOL_CLO(arg, "--trace-mem",         clo_trace_mem) {}
   else if VG_BOOL_CLO(arg, "--trace-superblocks", clo_trace_sbs) {}
   else if VG_BOOL_CLO(arg, "--instr-atstart", instrumentation_enabled) {}
   else if VG_STR_CLO(arg, "--trace-output", clo_trace_output) {}
   else if VG_STR_CLO(arg, "--trace-function", pattern) {
      if (!pattern[0])
         VG_(fmsg_bad_option)(arg, "function glob must not be empty\n");
      add_pattern(&function_patterns, pattern);
   }
   else if VG_STR_CLO(arg, "--trace-file", pattern) {
      if (!pattern[0])
         VG_(fmsg_bad_option)(arg, "file glob must not be empty\n");
      add_pattern(&file_patterns, pattern);
   }
   else
      return False;
   
   tl_assert(clo_fnname);
   tl_assert(clo_fnname[0]);
   return True;
}

static void lk_print_usage(void)
{  
   VG_(printf)(
"    --basic-counts=no|yes     count instructions, jumps, etc. [yes]\n"
"    --detailed-counts=no|yes  count loads, stores and alu ops [no]\n"
"    --trace-mem=no|yes        trace all loads and stores [no]\n"
"    --trace-superblocks=no|yes  trace all superblock entries [no]\n"
"    --instr-atstart=no|yes  enable Lackey instrumentation at startup [yes]\n"
"    --trace-function=<glob> include matching functions (repeatable)\n"
"    --trace-file=<glob>     include matching source files (repeatable)\n"
"    --trace-output=<path>   append traces to a file [Valgrind log]\n"
"    --fnname=<name>           count calls to <name> (only used if\n"
"                              --basic-count=yes)  [main]\n"
   );
}

static void lk_print_debug_usage(void)
{  
   VG_(printf)(
"    (none)\n"
   );
}

/*------------------------------------------------------------*/
/*--- Stuff for --basic-counts                             ---*/
/*------------------------------------------------------------*/

/* Nb: use ULongs because the numbers can get very big */
static ULong n_func_calls    = 0;
static ULong n_SBs_entered   = 0;
static ULong n_SBs_completed = 0;
static ULong n_IRStmts       = 0;
static ULong n_guest_instrs  = 0;
static ULong n_Jccs          = 0;
static ULong n_Jccs_untaken  = 0;
static ULong n_IJccs         = 0;
static ULong n_IJccs_untaken = 0;

static void add_one_func_call(void)
{
   n_func_calls++;
}

static void add_one_SB_entered(void)
{
   n_SBs_entered++;
}

static void add_one_SB_completed(void)
{
   n_SBs_completed++;
}

static void add_one_IRStmt(void)
{
   n_IRStmts++;
}

static void add_one_guest_instr(void)
{
   n_guest_instrs++;
}

static void add_one_Jcc(void)
{
   n_Jccs++;
}

static void add_one_Jcc_untaken(void)
{
   n_Jccs_untaken++;
}

static void add_one_inverted_Jcc(void)
{
   n_IJccs++;
}

static void add_one_inverted_Jcc_untaken(void)
{
   n_IJccs_untaken++;
}

/*------------------------------------------------------------*/
/*--- Stuff for --detailed-counts                          ---*/
/*------------------------------------------------------------*/

typedef
   IRExpr 
   IRAtom;

/* --- Operations --- */

typedef enum { OpLoad=0, OpStore=1, OpAlu=2 } Op;

#define N_OPS 3


/* --- Types --- */

#define N_TYPES 14

static Int type2index ( IRType ty )
{
   switch (ty) {
      case Ity_I1:      return 0;
      case Ity_I8:      return 1;
      case Ity_I16:     return 2;
      case Ity_I32:     return 3;
      case Ity_I64:     return 4;
      case Ity_I128:    return 5;
      case Ity_F32:     return 6;
      case Ity_F64:     return 7;
      case Ity_F128:    return 8;
      case Ity_V128:    return 9;
      case Ity_V256:    return 10;
      case Ity_D32:     return 11;
      case Ity_D64:     return 12;
      case Ity_D128:    return 13;
      default: tl_assert(0);
   }
}

static const HChar* nameOfTypeIndex ( Int i )
{
   switch (i) {
      case 0: return "I1";   break;
      case 1: return "I8";   break;
      case 2: return "I16";  break;
      case 3: return "I32";  break;
      case 4: return "I64";  break;
      case 5: return "I128"; break;
      case 6: return "F32";  break;
      case 7: return "F64";  break;
      case 8: return "F128";  break;
      case 9: return "V128";  break;
      case 10: return "V256"; break;
      case 11: return "D32";  break;
      case 12: return "D64";  break;
      case 13: return "D128"; break;
      default: tl_assert(0);
   }
}


/* --- Counts --- */

static ULong detailCounts[N_OPS][N_TYPES];

/* The helper that is called from the instrumented code. */
static VG_REGPARM(1)
void increment_detail(ULong* detail)
{
   (*detail)++;
}

/* A helper that adds the instrumentation for a detail.  guard ::
   Ity_I1 is the guarding condition for the event.  If NULL it is
   assumed to mean "always True". */
static void instrument_detail(IRSB* sb, Op op, IRType type, IRAtom* guard)
{
   IRDirty* di;
   IRExpr** argv;
   const UInt typeIx = type2index(type);

   tl_assert(op < N_OPS);
   tl_assert(typeIx < N_TYPES);

   argv = mkIRExprVec_1( mkIRExpr_HWord( (HWord)&detailCounts[op][typeIx] ) );
   di = unsafeIRDirty_0_N( 1, "increment_detail",
                              VG_(fnptr_to_fnentry)( &increment_detail ), 
                              argv);
   if (guard) di->guard = guard;
   addStmtToIRSB( sb, IRStmt_Dirty(di) );
}

/* Summarize and print the details. */
static void print_details ( void )
{
   Int typeIx;
   VG_(umsg)("   Type        Loads       Stores       AluOps\n");
   VG_(umsg)("   -------------------------------------------\n");
   for (typeIx = 0; typeIx < N_TYPES; typeIx++) {
      VG_(umsg)("   %-4s %'12llu %'12llu %'12llu\n",
                nameOfTypeIndex( typeIx ),
                detailCounts[OpLoad ][typeIx],
                detailCounts[OpStore][typeIx],
                detailCounts[OpAlu  ][typeIx]
      );
   }
}


/*------------------------------------------------------------*/
/*--- Stuff for --trace-mem                                ---*/
/*------------------------------------------------------------*/

#define MAX_DSIZE    512

typedef 
   enum { Event_Ir, Event_Dr, Event_Dw, Event_Dm }
   EventKind;

typedef
   struct {
      IRAtom*    addr;
      EventKind  ekind;
      Int        size;
      IRAtom*    guard; /* :: Ity_I1, or NULL=="always True" */
   }
   Event;

/* Up to this many unnotified events are allowed.  Must be at least two,
   so that reads and writes to the same address can be merged into a modify.
   Beyond that, larger numbers just potentially induce more spilling due to
   extending live ranges of address temporaries. */
#define N_EVENTS 4

/* Maintain an ordered list of memory events which are outstanding, in
   the sense that no IR has yet been generated to do the relevant
   helper calls.  The SB is scanned top to bottom and memory events
   are added to the end of the list, merging with the most recent
   notified event where possible (Dw immediately following Dr and
   having the same size and EA can be merged).

   This merging is done so that for architectures which have
   load-op-store instructions (x86, amd64), the instr is treated as if
   it makes just one memory reference (a modify), rather than two (a
   read followed by a write at the same address).

   At various points the list will need to be flushed, that is, IR
   generated from it.  That must happen before any possible exit from
   the block (the end, or an IRStmt_Exit).  Flushing also takes place
   when there is no space to add a new event, and before entering a
   RMW (read-modify-write) section on processors supporting LL/SC.

   If we require the simulation statistics to be up to date with
   respect to possible memory exceptions, then the list would have to
   be flushed before each memory reference.  That's a pain so we don't
   bother.

   Flushing the list consists of walking it start to end and emitting
   instrumentation IR for each event, in the order in which they
   appear. */

static Event events[N_EVENTS];
static Int   events_used = 0;


static VG_REGPARM(2) void trace_instr(Addr addr, SizeT size)
{
   trace_printf("I  %08lx,%lu\n", addr, size);
}

static VG_REGPARM(2) void trace_load(Addr addr, SizeT size)
{
   trace_printf(" L %08lx,%lu\n", addr, size);
}

static VG_REGPARM(2) void trace_store(Addr addr, SizeT size)
{
   trace_printf(" S %08lx,%lu\n", addr, size);
}

static VG_REGPARM(2) void trace_modify(Addr addr, SizeT size)
{
   trace_printf(" M %08lx,%lu\n", addr, size);
}


static void flushEvents(IRSB* sb)
{
   Int        i;
   const HChar* helperName;
   void*      helperAddr;
   IRExpr**   argv;
   IRDirty*   di;
   Event*     ev;

   for (i = 0; i < events_used; i++) {

      ev = &events[i];
      
      // Decide on helper fn to call and args to pass it.
      switch (ev->ekind) {
         case Event_Ir: helperName = "trace_instr";
                        helperAddr =  trace_instr;  break;

         case Event_Dr: helperName = "trace_load";
                        helperAddr =  trace_load;   break;

         case Event_Dw: helperName = "trace_store";
                        helperAddr =  trace_store;  break;

         case Event_Dm: helperName = "trace_modify";
                        helperAddr =  trace_modify; break;
         default:
            tl_assert(0);
      }

      // Add the helper.
      argv = mkIRExprVec_2( ev->addr, mkIRExpr_HWord( ev->size ) );
      di   = unsafeIRDirty_0_N( /*regparms*/2, 
                                helperName, VG_(fnptr_to_fnentry)( helperAddr ),
                                argv );
      if (ev->guard) {
         di->guard = ev->guard;
      }
      addStmtToIRSB( sb, IRStmt_Dirty(di) );
   }

   events_used = 0;
}

// WARNING:  If you aren't interested in instruction reads, you can omit the
// code that adds calls to trace_instr() in flushEvents().  However, you
// must still call this function, addEvent_Ir() -- it is necessary to add
// the Ir events to the events list so that merging of paired load/store
// events into modify events works correctly.
static void addEvent_Ir ( IRSB* sb, IRAtom* iaddr, UInt isize )
{
   Event* evt;
   tl_assert(clo_trace_mem);
   tl_assert( (VG_MIN_INSTR_SZB <= isize && isize <= VG_MAX_INSTR_SZB)
            || VG_CLREQ_SZB == isize );
   if (events_used == N_EVENTS)
      flushEvents(sb);
   tl_assert(events_used >= 0 && events_used < N_EVENTS);
   evt = &events[events_used];
   evt->ekind = Event_Ir;
   evt->addr  = iaddr;
   evt->size  = isize;
   evt->guard = NULL;
   events_used++;
}

/* Add a guarded read event. */
static
void addEvent_Dr_guarded ( IRSB* sb, IRAtom* daddr, Int dsize, IRAtom* guard )
{
   Event* evt;
   tl_assert(clo_trace_mem);
   tl_assert(isIRAtom(daddr));
   tl_assert(dsize >= 1 && dsize <= MAX_DSIZE);
   if (events_used == N_EVENTS)
      flushEvents(sb);
   tl_assert(events_used >= 0 && events_used < N_EVENTS);
   evt = &events[events_used];
   evt->ekind = Event_Dr;
   evt->addr  = daddr;
   evt->size  = dsize;
   evt->guard = guard;
   events_used++;
}

/* Add an ordinary read event, by adding a guarded read event with an
   always-true guard. */
static
void addEvent_Dr ( IRSB* sb, IRAtom* daddr, Int dsize )
{
   addEvent_Dr_guarded(sb, daddr, dsize, NULL);
}

/* Add a guarded write event. */
static
void addEvent_Dw_guarded ( IRSB* sb, IRAtom* daddr, Int dsize, IRAtom* guard )
{
   Event* evt;
   tl_assert(clo_trace_mem);
   tl_assert(isIRAtom(daddr));
   tl_assert(dsize >= 1 && dsize <= MAX_DSIZE);
   if (events_used == N_EVENTS)
      flushEvents(sb);
   tl_assert(events_used >= 0 && events_used < N_EVENTS);
   evt = &events[events_used];
   evt->ekind = Event_Dw;
   evt->addr  = daddr;
   evt->size  = dsize;
   evt->guard = guard;
   events_used++;
}

/* Add an ordinary write event.  Try to merge it with an immediately
   preceding ordinary read event of the same size to the same
   address. */
static
void addEvent_Dw ( IRSB* sb, IRAtom* daddr, Int dsize )
{
   Event* lastEvt;
   Event* evt;
   tl_assert(clo_trace_mem);
   tl_assert(isIRAtom(daddr));
   tl_assert(dsize >= 1 && dsize <= MAX_DSIZE);

   // Is it possible to merge this write with the preceding read?
   lastEvt = &events[events_used-1];
   if (events_used > 0
       && lastEvt->ekind == Event_Dr
       && lastEvt->size  == dsize
       && lastEvt->guard == NULL
       && eqIRAtom(lastEvt->addr, daddr))
   {
      lastEvt->ekind = Event_Dm;
      return;
   }

   // No.  Add as normal.
   if (events_used == N_EVENTS)
      flushEvents(sb);
   tl_assert(events_used >= 0 && events_used < N_EVENTS);
   evt = &events[events_used];
   evt->ekind = Event_Dw;
   evt->size  = dsize;
   evt->addr  = daddr;
   evt->guard = NULL;
   events_used++;
}


/*------------------------------------------------------------*/
/*--- Stuff for --trace-superblocks                        ---*/
/*------------------------------------------------------------*/

static void trace_superblock(Addr addr)
{
   trace_printf("SB %08lx\n", addr);
}


/*------------------------------------------------------------*/
/*--- Basic tool functions                                 ---*/
/*------------------------------------------------------------*/

static void lk_post_clo_init(void)
{
   Int op, tyIx;

   if (clo_trace_output && !switch_trace_output(clo_trace_output))
      VG_(exit)(1);
   VG_(atfork)(trace_before_fork, NULL, NULL);

   if (clo_detailed_counts) {
      for (op = 0; op < N_OPS; op++)
         for (tyIx = 0; tyIx < N_TYPES; tyIx++)
            detailCounts[op][tyIx] = 0;
   }
}

static
IRSB* lk_instrument ( VgCallbackClosure* closure,
                      IRSB* sbIn, 
                      const VexGuestLayout* layout, 
                      const VexGuestExtents* vge,
                      const VexArchInfo* archinfo_host,
                      IRType gWordTy, IRType hWordTy )
{
   IRDirty*   di;
   Int        i;
   IRSB*      sbOut;
   IRTypeEnv* tyenv = sbIn->tyenv;
   Addr       iaddr = 0, dst;
   UInt       ilen = 0;
   Bool       condition_inverted = False;
   DiEpoch    ep = VG_(current_DiEpoch)();
   Bool       filtered = function_patterns != NULL || file_patterns != NULL;
   Bool       selected = !filtered;
   Bool       selected_in_block = False;

   if (!instrumentation_enabled)
      return sbIn;

   if (gWordTy != hWordTy) {
      /* We don't currently support this case. */
      VG_(tool_panic)("host/guest word size mismatch");
   }

   /* Set up SB */
   sbOut = deepCopyIRSBExceptStmts(sbIn);

   // Copy verbatim any IR preamble preceding the first IMark
   i = 0;
   while (i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark) {
      addStmtToIRSB( sbOut, sbIn->stmts[i] );
      i++;
   }

   if (clo_basic_counts && !filtered) {
      /* Count this superblock. */
      di = unsafeIRDirty_0_N( 0, "add_one_SB_entered", 
                                 VG_(fnptr_to_fnentry)( &add_one_SB_entered ),
                                 mkIRExprVec_0() );
      addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
   }

   if (clo_trace_sbs && !filtered) {
      /* Print this superblock's address. */
      di = unsafeIRDirty_0_N( 
              0, "trace_superblock", 
              VG_(fnptr_to_fnentry)( &trace_superblock ),
              mkIRExprVec_1( mkIRExpr_HWord( vge->base[0] ) ) 
           );
      addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
   }

   if (clo_trace_mem) {
      events_used = 0;
   }

   for (/*use current i*/; i < sbIn->stmts_used; i++) {
      IRStmt* st = sbIn->stmts[i];
      if (!st || st->tag == Ist_NoOp) continue;

      if (st->tag == Ist_IMark) {
         selected = !filtered || instruction_selected(
            ep, st->Ist.IMark.addr + st->Ist.IMark.delta);
         if (!selected && clo_trace_mem)
            flushEvents(sbOut);
         if (selected && !selected_in_block) {
            selected_in_block = True;
            /* Defer block helpers until an included instruction is reached:
               earlier conditional exits may bypass the included portion. */
            if (filtered && clo_basic_counts) {
               di = unsafeIRDirty_0_N(
                  0, "add_one_SB_entered",
                  VG_(fnptr_to_fnentry)(&add_one_SB_entered), mkIRExprVec_0());
               addStmtToIRSB(sbOut, IRStmt_Dirty(di));
            }
            if (filtered && clo_trace_sbs) {
               di = unsafeIRDirty_0_N(
                  0, "trace_superblock",
                  VG_(fnptr_to_fnentry)(&trace_superblock),
                  mkIRExprVec_1(mkIRExpr_HWord(st->Ist.IMark.addr)));
               addStmtToIRSB(sbOut, IRStmt_Dirty(di));
            }
         }
      }
      if (!selected) {
         /* Keep the guest statement, without adding any Lackey helpers. */
         addStmtToIRSB(sbOut, st);
         continue;
      }

      if (clo_basic_counts) {
         /* Count one VEX statement. */
         di = unsafeIRDirty_0_N( 0, "add_one_IRStmt", 
                                    VG_(fnptr_to_fnentry)( &add_one_IRStmt ), 
                                    mkIRExprVec_0() );
         addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
      }
      
      switch (st->tag) {
         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_Put:
         case Ist_PutI:
         case Ist_MBE:
            addStmtToIRSB( sbOut, st );
            break;

         case Ist_IMark:
            if (clo_basic_counts) {
               /* Needed to be able to check for inverted condition in Ist_Exit */
               iaddr = st->Ist.IMark.addr;
               ilen  = st->Ist.IMark.len;

               /* Count guest instruction. */
               di = unsafeIRDirty_0_N( 0, "add_one_guest_instr",
                                          VG_(fnptr_to_fnentry)( &add_one_guest_instr ), 
                                          mkIRExprVec_0() );
               addStmtToIRSB( sbOut, IRStmt_Dirty(di) );

               /* An unconditional branch to a known destination in the
                * guest's instructions can be represented, in the IRSB to
                * instrument, by the VEX statements that are the
                * translation of that known destination. This feature is
                * called 'SB chasing' and can be influenced by command
                * line option --vex-guest-chase=[yes|no].
                *
                * To get an accurate count of the calls to a specific
                * function, taking SB chasing into account, we need to
                * check for each guest instruction (Ist_IMark) if it is
                * the entry point of a function.
                */
               tl_assert(clo_fnname);
               tl_assert(clo_fnname[0]);
               const HChar *fnname;
               if (VG_(get_fnname_if_entry)(ep, st->Ist.IMark.addr,
                                            &fnname)
                   && 0 == VG_(strcmp)(fnname, clo_fnname)) {
                  di = unsafeIRDirty_0_N( 
                          0, "add_one_func_call", 
                             VG_(fnptr_to_fnentry)( &add_one_func_call ), 
                             mkIRExprVec_0() );
                  addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
               }
            }
            if (clo_trace_mem) {
               // WARNING: do not remove this function call, even if you
               // aren't interested in instruction reads.  See the comment
               // above the function itself for more detail.
               addEvent_Ir( sbOut, mkIRExpr_HWord( (HWord)st->Ist.IMark.addr ),
                            st->Ist.IMark.len );
            }
            addStmtToIRSB( sbOut, st );
            break;

         case Ist_WrTmp:
            // Add a call to trace_load() if --trace-mem=yes.
            if (clo_trace_mem) {
               IRExpr* data = st->Ist.WrTmp.data;
               if (data->tag == Iex_Load) {
                  addEvent_Dr( sbOut, data->Iex.Load.addr,
                               sizeofIRType(data->Iex.Load.ty) );
               }
            }
            if (clo_detailed_counts) {
               IRExpr* expr = st->Ist.WrTmp.data;
               IRType  type = typeOfIRExpr(sbOut->tyenv, expr);
               tl_assert(type != Ity_INVALID);
               switch (expr->tag) {
                  case Iex_Load:
                    instrument_detail( sbOut, OpLoad, type, NULL/*guard*/ );
                     break;
                  case Iex_Unop:
                  case Iex_Binop:
                  case Iex_Triop:
                  case Iex_Qop:
                  case Iex_ITE:
                     instrument_detail( sbOut, OpAlu, type, NULL/*guard*/ );
                     break;
                  default:
                     break;
               }
            }
            addStmtToIRSB( sbOut, st );
            break;

         case Ist_Store: {
            IRExpr* data = st->Ist.Store.data;
            IRType  type = typeOfIRExpr(tyenv, data);
            tl_assert(type != Ity_INVALID);
            if (clo_trace_mem) {
               addEvent_Dw( sbOut, st->Ist.Store.addr,
                            sizeofIRType(type) );
            }
            if (clo_detailed_counts) {
               instrument_detail( sbOut, OpStore, type, NULL/*guard*/ );
            }
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_StoreG: {
            IRStoreG* sg   = st->Ist.StoreG.details;
            IRExpr*   data = sg->data;
            IRType    type = typeOfIRExpr(tyenv, data);
            tl_assert(type != Ity_INVALID);
            if (clo_trace_mem) {
               addEvent_Dw_guarded( sbOut, sg->addr,
                                    sizeofIRType(type), sg->guard );
            }
            if (clo_detailed_counts) {
               instrument_detail( sbOut, OpStore, type, sg->guard );
            }
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_LoadG: {
            IRLoadG* lg       = st->Ist.LoadG.details;
            IRType   type     = Ity_INVALID; /* loaded type */
            IRType   typeWide = Ity_INVALID; /* after implicit widening */
            typeOfIRLoadGOp(lg->cvt, &typeWide, &type);
            tl_assert(type != Ity_INVALID);
            if (clo_trace_mem) {
               addEvent_Dr_guarded( sbOut, lg->addr,
                                    sizeofIRType(type), lg->guard );
            }
            if (clo_detailed_counts) {
               instrument_detail( sbOut, OpLoad, type, lg->guard );
            }
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_Dirty: {
            if (clo_trace_mem) {
               Int      dsize;
               IRDirty* d = st->Ist.Dirty.details;
               if (d->mFx != Ifx_None) {
                  // This dirty helper accesses memory.  Collect the details.
                  tl_assert(d->mAddr != NULL);
                  tl_assert(d->mSize != 0);
                  dsize = d->mSize;
                  if (d->mFx == Ifx_Read || d->mFx == Ifx_Modify)
                     addEvent_Dr( sbOut, d->mAddr, dsize );
                  if (d->mFx == Ifx_Write || d->mFx == Ifx_Modify)
                     addEvent_Dw( sbOut, d->mAddr, dsize );
               } else {
                  tl_assert(d->mAddr == NULL);
                  tl_assert(d->mSize == 0);
               }
            }
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_CAS: {
            /* We treat it as a read and a write of the location.  I
               think that is the same behaviour as it was before IRCAS
               was introduced, since prior to that point, the Vex
               front ends would translate a lock-prefixed instruction
               into a (normal) read followed by a (normal) write. */
            Int    dataSize;
            IRType dataTy;
            IRCAS* cas = st->Ist.CAS.details;
            tl_assert(cas->addr != NULL);
            tl_assert(cas->dataLo != NULL);
            dataTy   = typeOfIRExpr(tyenv, cas->dataLo);
            dataSize = sizeofIRType(dataTy);
            if (cas->dataHi != NULL)
               dataSize *= 2; /* since it's a doubleword-CAS */
            if (clo_trace_mem) {
               addEvent_Dr( sbOut, cas->addr, dataSize );
               addEvent_Dw( sbOut, cas->addr, dataSize );
            }
            if (clo_detailed_counts) {
               instrument_detail( sbOut, OpLoad, dataTy, NULL/*guard*/ );
               if (cas->dataHi != NULL) /* dcas */
                  instrument_detail( sbOut, OpLoad, dataTy, NULL/*guard*/ );
               instrument_detail( sbOut, OpStore, dataTy, NULL/*guard*/ );
               if (cas->dataHi != NULL) /* dcas */
                  instrument_detail( sbOut, OpStore, dataTy, NULL/*guard*/ );
            }
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_LLSC: {
            IRType dataTy;
            if (st->Ist.LLSC.storedata == NULL) {
               /* LL */
               dataTy = typeOfIRTemp(tyenv, st->Ist.LLSC.result);
               if (clo_trace_mem) {
                  addEvent_Dr( sbOut, st->Ist.LLSC.addr,
                                      sizeofIRType(dataTy) );
                  /* flush events before LL, helps SC to succeed */
                  flushEvents(sbOut);
	       }
               if (clo_detailed_counts)
                  instrument_detail( sbOut, OpLoad, dataTy, NULL/*guard*/ );
            } else {
               /* SC */
               dataTy = typeOfIRExpr(tyenv, st->Ist.LLSC.storedata);
               if (clo_trace_mem)
                  addEvent_Dw( sbOut, st->Ist.LLSC.addr,
                                      sizeofIRType(dataTy) );
               if (clo_detailed_counts)
                  instrument_detail( sbOut, OpStore, dataTy, NULL/*guard*/ );
            }
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_Exit:
            if (clo_basic_counts) {
               // The condition of a branch was inverted by VEX if a taken
               // branch is in fact a fall trough according to client address
               tl_assert(iaddr != 0);
               dst = (sizeof(Addr) == 4) ? st->Ist.Exit.dst->Ico.U32 :
                                           st->Ist.Exit.dst->Ico.U64;
               condition_inverted = (dst == iaddr + ilen);

               /* Count Jcc */
               if (!condition_inverted)
                  di = unsafeIRDirty_0_N( 0, "add_one_Jcc", 
                                          VG_(fnptr_to_fnentry)( &add_one_Jcc ), 
                                          mkIRExprVec_0() );
               else
                  di = unsafeIRDirty_0_N( 0, "add_one_inverted_Jcc",
                                          VG_(fnptr_to_fnentry)(
                                             &add_one_inverted_Jcc ),
                                          mkIRExprVec_0() );

               addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
            }
            if (clo_trace_mem) {
               flushEvents(sbOut);
            }

            addStmtToIRSB( sbOut, st );      // Original statement

            if (clo_basic_counts) {
               /* Count non-taken Jcc */
               if (!condition_inverted)
                  di = unsafeIRDirty_0_N( 0, "add_one_Jcc_untaken", 
                                          VG_(fnptr_to_fnentry)(
                                             &add_one_Jcc_untaken ),
                                          mkIRExprVec_0() );
               else
                  di = unsafeIRDirty_0_N( 0, "add_one_inverted_Jcc_untaken",
                                          VG_(fnptr_to_fnentry)(
                                             &add_one_inverted_Jcc_untaken ),
                                          mkIRExprVec_0() );

               addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
            }
            break;

         default:
            ppIRStmt(st);
            tl_assert(0);
      }
   }

   if (clo_basic_counts && selected) {
      /* Count this basic block. */
      di = unsafeIRDirty_0_N( 0, "add_one_SB_completed", 
                                 VG_(fnptr_to_fnentry)( &add_one_SB_completed ),
                                 mkIRExprVec_0() );
      addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
   }

   if (clo_trace_mem) {
      /* At the end of the sbIn.  Flush outstandings. */
      flushEvents(sbOut);
   }

   return sbOut;
}

static void lk_fini(Int exitcode)
{
   flush_trace();
   if (trace_fd >= 0) {
      VG_(close)(trace_fd);
      trace_fd = -1;
   }
   VG_(free)(trace_path);
   trace_path = NULL;
   clear_patterns(&function_patterns);
   clear_patterns(&file_patterns);
   tl_assert(clo_fnname);
   tl_assert(clo_fnname[0]);

   if (clo_basic_counts) {
      ULong total_Jccs = n_Jccs + n_IJccs;
      ULong taken_Jccs = (n_Jccs - n_Jccs_untaken) + n_IJccs_untaken;

      VG_(umsg)("Counted %'llu call%s to %s()\n",
                n_func_calls, ( n_func_calls==1 ? "" : "s" ), clo_fnname);

      VG_(umsg)("\n");
      VG_(umsg)("Jccs:\n");
      VG_(umsg)("  total:         %'llu\n", total_Jccs);
      VG_(umsg)("  taken:         %'llu (%.0f%%)\n",
                taken_Jccs, taken_Jccs * 100.0 / (total_Jccs ? total_Jccs : 1));
      
      VG_(umsg)("\n");
      VG_(umsg)("Executed:\n");
      VG_(umsg)("  SBs entered:   %'llu\n", n_SBs_entered);
      VG_(umsg)("  SBs completed: %'llu\n", n_SBs_completed);
      VG_(umsg)("  guest instrs:  %'llu\n", n_guest_instrs);
      VG_(umsg)("  IRStmts:       %'llu\n", n_IRStmts);
      
      VG_(umsg)("\n");
      VG_(umsg)("Ratios:\n");
      VG_(umsg)("  guest instrs : SB entered  = %'llu : 10\n",
         n_SBs_entered ? 10 * n_guest_instrs / n_SBs_entered : 0);
      VG_(umsg)("       IRStmts : SB entered  = %'llu : 10\n",
         n_SBs_entered ? 10 * n_IRStmts / n_SBs_entered : 0);
      VG_(umsg)("       IRStmts : guest instr = %'llu : 10\n",
         n_guest_instrs ? 10 * n_IRStmts / n_guest_instrs : 0);
   }

   if (clo_detailed_counts) {
      VG_(umsg)("\n");
      VG_(umsg)("IR-level counts by type:\n");
      print_details();
   }

   if (clo_basic_counts) {
      VG_(umsg)("\n");
      VG_(umsg)("Exit code:       %d\n", exitcode);
   }
}

static void lk_pre_clo_init(void)
{
   VG_(details_name)            ("Lackey");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("an example Valgrind tool");
   VG_(details_copyright_author)(
      "Copyright (C) 2002-2024, and GNU GPL'd, by Nicholas Nethercote et al.");
   VG_(details_bug_reports_to)  (VG_BUGS_TO);
   VG_(details_avg_translation_sizeB) ( 200 );

   VG_(basic_tool_funcs)          (lk_post_clo_init,
                                   lk_instrument,
                                   lk_fini);
   VG_(needs_command_line_options)(lk_process_cmd_line_option,
                                   lk_print_usage,
                                   lk_print_debug_usage);
   VG_(needs_client_requests)(handle_client_request);
   VG_(needs_syscall_wrapper)(trace_pre_syscall, trace_post_syscall);
}

VG_DETERMINE_INTERFACE_VERSION(lk_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                lk_main.c ---*/
/*--------------------------------------------------------------------*/
