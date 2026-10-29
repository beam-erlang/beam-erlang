/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 1997-2026. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * %CopyrightEnd%
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "sys.h"
#include <ctype.h>
#include <fenv.h>
#include "erl_vm.h"
#include "global.h"
#include <beam/native.h>
#include "erl_process.h"
#include "error.h"
#include "erl_version.h"
#include "erl_db.h"
#include "beam_bp.h"
#include "erl_bits.h"
#include "erl_binary.h"
#define ERTS_WANT_EXTERNAL_TAGS
#include "external.h"
#include "dist.h"
#include "erl_mseg.h"
#include "erl_threads.h"
#include "erl_hl_timer.h"
#include "erl_printf_term.h"
#include "erl_misc_utils.h"
#include "packet_parser.h"
#include "erl_cpu_topology.h"
#include "erl_thr_progress.h"
#include "erl_thr_queue.h"
#include "erl_async.h"
#include "erl_ptab.h"
#include "erl_bif_unique.h"
#define ERTS_WANT_TIMER_WHEEL_API
#include "erl_time.h"
#include "erl_check_io.h"
#include "erl_osenv.h"
#include "erl_proc_sig_queue.h"
#include "beam_load.h"
#include "erl_global_literals.h"
#include "erl_iolist.h"
#include "erl_debugger.h"
#include "erl_record.h"
#include "erl_crash_dump.h"
#include "beam_internal.h"
#include "beam_core_modules.h"

#include "jit/beam_asm.h"

#ifdef HAVE_SYS_RESOURCE_H
#  include <sys/resource.h>
#endif

#define ERTS_DEFAULT_NO_ASYNC_THREADS	1

#define ERTS_DEFAULT_SCHED_STACK_SIZE   128
#define ERTS_DEFAULT_DCPU_SCHED_STACK_SIZE 40
#define ERTS_DEFAULT_DIO_SCHED_STACK_SIZE 40

#define ERTS_HALT_FLUSH_TIMEOUT_EXIT_CODE 255
ErtsMonotonicTime erts_halt_flush_timeout = -1; /* negative value -> infinity */

static int modified_sched_thread_suggested_stack_size = 0;

Eterm erts_init_process_id = ERTS_INVALID_PID;
int erts_bare_mode = 0;

extern void erl_crash_dump_v(char *, int, const char *, va_list);
#ifdef __WIN32__
extern void ConNormalExit(void);
extern void ConWaitForExit(void);
#endif

static void erl_init(int ncpu,
		     int proc_tab_sz,
		     int legacy_proc_tab,
		     int port_tab_sz,
		     int port_tab_sz_ignore_files,
		     int legacy_port_tab,
                     Uint sys_proc_outst_req_lim,
		     int time_correction,
		     ErtsTimeWarpMode time_warp_mode,
		     int node_tab_delete_delay,
		     ErtsDbSpinCount db_spin_count);

static erts_atomic_t exiting;

erts_atomic32_t erts_writing_erl_crash_dump;
erts_tsd_key_t erts_is_crash_dumping_key;
int erts_initialized = 0;

/*
 * Configurable parameters.
 */

int H_MIN_SIZE;			/* The minimum heap grain */
int BIN_VH_MIN_SIZE;		/* The minimum binary virtual*/
Uint H_MAX_SIZE;		/* The maximum heap size */
int H_MAX_FLAGS;		/* The maximum heap flags */

Uint32 erts_debug_flags;	/* Debug flags. */
int erts_backtrace_depth;	/* How many functions to show in a backtrace
				 * in error codes.
				 */

erts_atomic32_t erts_max_gen_gcs;

Eterm erts_error_logger_warnings; /* What to map warning logs to, am_error, 
				     am_info or am_warning, am_error is 
				     the default for BC */

int erts_compat_rel;

static int no_schedulers;
static int no_schedulers_online;
static int no_dirty_cpu_schedulers;
static int no_dirty_cpu_schedulers_online;
static int no_dirty_io_schedulers;

#ifdef DEBUG
Uint32 verbose;             /* See erl_debug.h for information about verbose */
#endif

int erts_atom_table_size = ATOM_LIMIT;	/* Maximum number of atoms */

int erts_pd_initial_size = 8;  /* must be power of 2 */

int erts_modified_timing_level;

int erts_no_crash_dump = 0;	/* Use -d to suppress crash dump. */

int erts_no_line_info = 0;	/* -L: Don't load line information */

#ifdef BEAMASM
Uint erts_coverage_mode = ERTS_COV_NONE; /* -JPcover: Enable coverage */
int erts_jit_asm_dump = 0;	/* -JDdump: Dump assembly code */
#endif

/*
 * Other global variables.
 */

ErtsModifiedTimings erts_modified_timings[] = {
    /* 0 */	{make_small(0), CONTEXT_REDS},
    /* 1 */	{make_small(0), (3*CONTEXT_REDS)/4},
    /* 2 */	{make_small(0), CONTEXT_REDS/2},
    /* 3 */	{make_small(0), (7*CONTEXT_REDS)/8},
    /* 4 */	{make_small(0), CONTEXT_REDS/3},
    /* 5 */	{make_small(0), (10*CONTEXT_REDS)/11},
    /* 6 */	{make_small(1), CONTEXT_REDS/4},
    /* 7 */	{make_small(1), (5*CONTEXT_REDS)/7},
    /* 8 */	{make_small(10), CONTEXT_REDS/5},
    /* 9 */	{make_small(10), (6*CONTEXT_REDS)/7}
};

#define ERTS_MODIFIED_TIMING_LEVELS \
  (sizeof(erts_modified_timings)/sizeof(ErtsModifiedTimings))

Export *erts_delay_trap = NULL;

int ignore_break;
int replace_intr;

static ERTS_INLINE int
has_prefix(const char *prefix, const char *string)
{
    int i;
    for (i = 0; prefix[i]; i++)
	if (prefix[i] != string[i])
	    return 0;
    return 1;
}

static char*
progname(char *fullname) 
{
    int i;

    i = sys_strlen(fullname);
    while (i >= 0) {
	if ((fullname[i] != '/') && (fullname[i] != '\\')) 
	    i--;
	else 
	    break;
    }
    return fullname+i+1;
}

static int
this_rel_num(void)
{
    static int this_rel = -1;

    if (this_rel < 1) {
	int i;
	char this_rel_str[] = ERLANG_OTP_RELEASE;
	    
	i = 0;
	while (this_rel_str[i] && !isdigit((int) this_rel_str[i]))
	    i++;
	this_rel = atoi(&this_rel_str[i]); 
	if (this_rel < 1)
	    erts_exit(1, "Unexpected ERLANG_OTP_RELEASE format\n");
    }
    return this_rel;
}

static ERTS_INLINE void
set_default_time_adj(int *time_correction_p, ErtsTimeWarpMode *time_warp_mode_p)
{
    *time_correction_p = 1;
    *time_warp_mode_p = ERTS_MULTI_TIME_WARP_MODE;
    if (!erts_check_time_adj_support(*time_correction_p,
				     *time_warp_mode_p)) {
	*time_correction_p = 0;
	ASSERT(erts_check_time_adj_support(*time_correction_p,
					   *time_warp_mode_p));
    }
}

/*
 * Common error printout function, all error messages
 * that don't go to the error logger go through here.
 */

void erl_error(const char *fmt, va_list args)
{
    erts_vfprintf(stderr, fmt, args);
}

static int early_init(int *argc, char **argv, const beam_vm_options *options);

static void
erl_init(int ncpu,
	 int proc_tab_sz,
	 int legacy_proc_tab,
	 int port_tab_sz,
	 int port_tab_sz_ignore_files,
	 int legacy_port_tab,
         Uint sys_proc_outst_req_lim,
	 int time_correction,
	 ErtsTimeWarpMode time_warp_mode,
	 int node_tab_delete_delay,
	 ErtsDbSpinCount db_spin_count)
{
    init_global_literals();
    erts_monitor_link_init();
    erts_bif_unique_init();
    erts_proc_sig_queue_init(); /* Must be after erts_bif_unique_init(); */
    erts_init_time(time_correction, time_warp_mode);
    erts_init_sys_common_misc();
    erts_init_process(ncpu, proc_tab_sz, legacy_proc_tab);
    erts_init_scheduling(no_schedulers,
			 no_schedulers_online,
                         erts_no_poll_threads,
			 no_dirty_cpu_schedulers,
			 no_dirty_cpu_schedulers_online,
			 no_dirty_io_schedulers
			 );
    erts_late_init_time_sup();
    erts_init_cpu_topology(); /* Must be after init_scheduling */
    erts_init_gc(); /* Must be after init_scheduling */
    erts_alloc_late_init();

    H_MIN_SIZE      = erts_next_heap_size(H_MIN_SIZE, 0);
    BIN_VH_MIN_SIZE = erts_next_heap_size(BIN_VH_MIN_SIZE, 0);

    erts_init_debugger();
    erts_code_ix_init();
    erts_init_fun_table();
    init_atom_table();
    init_export_table();
    erts_record_init_table();
    init_module_table();
    init_register_table();
    init_message();
#ifdef BEAMASM
    beamasm_init();
#endif
    erts_bif_info_init();
    erts_ddll_init();
    init_emulator();
    erts_init_trace();  /* Must be after init_emulator() */
    erts_ptab_init();   /* Must be after init_emulator() */
    erts_init_binary(); /* Must be after init_emulator() */
    erts_init_iolist(); /* Must be after init_emulator() */
    erts_bp_init();
    init_db(db_spin_count); /* Must be after init_emulator */
    erts_init_node_tables(node_tab_delete_delay);
    init_dist();
    erl_drv_thr_init();
    erts_init_async();
    erts_init_io(port_tab_sz, port_tab_sz_ignore_files, legacy_port_tab);
    init_load();
    erts_init_bif();
    erts_init_bif_chksum();
    erts_init_bif_binary();
    erts_init_bif_guard();
    erts_init_bif_persistent_term();
    erts_init_bif_re();
    erts_init_unicode(); /* after RE to get access to PCRE unicode */
    erts_init_external();
    erts_init_map();
    erts_beam_bif_load_init(sys_proc_outst_req_lim);
    erts_delay_trap = erts_export_put(am_erlang, am_delay_trap, 2);
    erts_late_init_process();
#if HAVE_ERTS_MSEG
    erts_mseg_late_init(); /* Must be after timer (erts_init_time()) and thread
			      initializations */
#endif
    if (!erts_bare_mode)
        erl_sys_late_init();
    packet_parser_init();
    erl_nif_init();
    erts_msacc_init();
    beamfile_init();
    erts_late_init_external();
}

static Eterm

erl_spawn_system_process(Process* parent, Eterm mod, Eterm func, Eterm args,
                         ErlSpawnOpts *so)
{
    Eterm res;
    int arity;

    ERTS_LC_ASSERT(ERTS_PROC_LOCK_MAIN & erts_proc_lc_my_proc_locks(parent));
    arity = erts_list_length(args);

    if (erts_find_function(mod, func, arity, erts_active_code_ix()) == NULL) {
	erts_exit(ERTS_ERROR_EXIT, "No function %T:%T/%i\n", mod, func, arity);
    }

    so->flags |= SPO_SYSTEM_PROC;

    res = erl_create_process(parent, mod, func, args, so);

    return res;
}

static Eterm
erl_first_process_bare(void)
{
    Process parent;
    ErlSpawnOpts so;
    Eterm ref, args, pid;
    Eterm *hp;
    Eterm await_result = erts_atom_put((byte *) "await_result", 12,
                                      ERTS_ATOM_ENC_LATIN1, 1);

    /* Keep a stable runtime identity without starting an Erlang boot system.
     * The private reference is never sent: this root remains suspended until
     * controlled VM shutdown retires the runtime's system processes. */
    erts_init_empty_process(&parent);
    erts_proc_lock(&parent, ERTS_PROC_LOCK_MAIN);
    ref = erts_make_ref(&parent);
    hp = HAlloc(&parent, 2);
    args = CONS(hp, ref, NIL);
    ERTS_SET_DEFAULT_SPAWN_OPTS(&so);
    pid = erl_spawn_system_process(&parent, am_erts_internal, await_result,
                                   args, &so);
    ASSERT(is_internal_pid(pid));
    erts_proc_unlock(&parent, ERTS_PROC_LOCK_MAIN);
    erts_cleanup_empty_process(&parent);
    return pid;
}

int
erts_bare_is_core_module(const char *name)
{
    unsigned int i;

    for (i = 0; i < sizeof(beam_core_module_names)/sizeof(beam_core_module_names[0]); i++) {
        if (sys_strcmp(name, beam_core_module_names[i]) == 0)
            return 1;
    }
    return 0;
}

static int
preload_enabled(const char *name)
{
    return !erts_bare_mode || erts_bare_is_core_module(name);
}

static Eterm
erl_system_process_otp(Eterm parent_pid, char* modname, int off_heap_msgq, int prio)
{ 
    Process *parent;
    ErlSpawnOpts so;
    Eterm mod, res;

    parent = erts_pid2proc(NULL, 0, parent_pid, ERTS_PROC_LOCK_MAIN);
    mod = erts_atom_put((byte *) modname, sys_strlen(modname),
                        ERTS_ATOM_ENC_LATIN1, 1);

    ERTS_SET_DEFAULT_SPAWN_OPTS(&so);

    so.flags |= SPO_USE_ARGS;

    if (off_heap_msgq) {
        so.flags |= SPO_OFF_HEAP_MSGQ;
    }

    so.min_heap_size  = H_MIN_SIZE;
    so.min_vheap_size = BIN_VH_MIN_SIZE;
    so.max_heap_size  = H_MAX_SIZE;
    so.max_heap_flags = H_MAX_FLAGS;
    so.priority       = prio;
    so.max_gen_gcs    = (Uint16) erts_atomic32_read_nob(&erts_max_gen_gcs);
    so.scheduler      = 0;

    res = erl_spawn_system_process(parent, mod, am_start, NIL, &so);
    ASSERT(is_internal_pid(res));

    erts_proc_unlock(parent, ERTS_PROC_LOCK_MAIN);

    return res;
}

Eterm erts_internal_spawn_system_process_3(BIF_ALIST_3) {
    Eterm mod, func, args, res;
    ErlSpawnOpts so;

    mod = BIF_ARG_1;
    func = BIF_ARG_2;
    args = BIF_ARG_3;

    ASSERT(is_atom(mod));
    ASSERT(is_atom(func));
    ASSERT(erts_list_length(args) >= 0);

    ERTS_SET_DEFAULT_SPAWN_OPTS(&so);
    res = erl_spawn_system_process(BIF_P, mod, func, args, &so);

    if (is_non_value(res)) {
        BIF_ERROR(BIF_P, so.error_code);
    }

    BIF_RET(res);
}

Eterm
erts_preloaded(Process* p)
{
    Eterm previous;
    int j;
    int need;
    Eterm mod;
    Eterm* hp;
    char* name;
    const Preload *preload = sys_preloaded();
    const beam_module_spec *selected;
    size_t selected_count = erts_vm_preloads(&selected);

    j = 0;
    need = (int)selected_count * 2;
    while (preload[j].name != NULL) {
	if (preload_enabled(preload[j].name))
	    need += 2;
	j++;
    }
    previous = NIL;
    hp = HAlloc(p, need);
    j = 0;
    while ((name = preload[j].name) != NULL)  {
	if (preload_enabled(name)) {
	    mod = am_atom_put(name, sys_strlen(name));
	    previous = CONS(hp, mod, previous);
	    hp += 2;
	}
	j++;
    }
    for (size_t i = 0; i < selected_count; ++i) {
        const char *selected_name = selected[i].name;
        mod = erts_atom_put((const byte *)selected_name, strlen(selected_name),
                            ERTS_ATOM_ENC_UTF8, 1);
        previous = CONS(hp, mod, previous);
        hp += 2;
    }
    return previous;
}


/* static variables that must not change (use same values at restart) */
static char* program;
static char* init = "init";
static int    boot_argc;
static char** boot_argv;

static char *
get_arg(char* rest, char* next, int* ip)
{
    if (*rest == '\0') {
	if (next == NULL) {
	    erts_fprintf(stderr, "too few arguments\n");
	    erts_usage();
	}
	(*ip)++;
	return next;
    }
    return rest;
}

static void 
load_preloaded(void)
{
    int i;
    Eterm res;
    Preload* preload_p;
    Eterm module_name;
    byte* code;
    char* name;
    int length;

    if ((preload_p = sys_preloaded()) == NULL) {
	return;
    }
    i = 0;
    while ((name = preload_p[i].name) != NULL) {
	if (!preload_enabled(name)) {
	    i++;
	    continue;
	}
	length = preload_p[i].size;
	module_name = erts_atom_put((byte *) name, sys_strlen(name), ERTS_ATOM_ENC_LATIN1, 1);
	if ((code = sys_preload_begin(&preload_p[i])) == 0)
	    erts_exit(ERTS_ERROR_EXIT, "Failed to find preloaded code for module %s\n",
		     name);
	res = erts_preload_module(NULL, 0, NIL, &module_name, code, length);
	sys_preload_end(&preload_p[i]);
	if (res != NIL)
	    erts_exit(ERTS_ERROR_EXIT,"Failed loading preloaded module %s (%T)\n",
		     name, res);
	i++;
    }
}

/* be helpful (or maybe downright rude:-) */
__decl_noreturn void __noreturn  erts_usage(void)
{
    int this_rel = this_rel_num();
    erts_fprintf(stderr, "Usage: %s [flags] [ -- [init_args] ]\n", progname(program));
    erts_fprintf(stderr, "The flags are:\n\n");
    erts_fprintf(stderr, "-a size        suggest stack size in kilo words for threads\n");
    erts_fprintf(stderr, "               in the async-thread pool; valid range is [%d-%d]\n",
		 ERTS_ASYNC_THREAD_MIN_STACK_SIZE,
		 ERTS_ASYNC_THREAD_MAX_STACK_SIZE);
    erts_fprintf(stderr, "-A number      set number of threads in async thread pool;\n");
    erts_fprintf(stderr, "               valid range is [1-%d]\n",
		 ERTS_MAX_NO_OF_ASYNC_THREADS);
    erts_fprintf(stderr, "\n");

    erts_fprintf(stderr, "-B[c|d|i]      set break (Ctrl+C) behavior; valid letters are:\n");
    erts_fprintf(stderr, "                  'c' to have Ctrl+C interrupt the Erlang shell;\n");
    erts_fprintf(stderr, "                  'd' (or no extra option) to disable the break handler;\n");
    erts_fprintf(stderr, "                  'i' to ignore break signals\n");
    erts_fprintf(stderr, "\n");

    erts_fprintf(stderr, "-c bool        enable or disable time correction\n");
    erts_fprintf(stderr, "-C mode        set time warp mode; valid modes are:\n");
    erts_fprintf(stderr, "               no_time_warp | single_time_warp | multi_time_warp\n");
    erts_fprintf(stderr, "\n");

    erts_fprintf(stderr, "-D             enable debugging support\n");
    erts_fprintf(stderr, "-Dibpl bool    enable or disable instrumentation for breakpoints on lines (default true)\n");
    erts_fprintf(stderr, "-d             don't write a crash dump for internally detected errors\n");
    erts_fprintf(stderr, "               (halt(String) will still produce a crash dump)\n");
    erts_fprintf(stderr, "-dcg           set the limit for the number of decentralized counter groups\n");
    erts_fprintf(stderr, "\n");

    erts_fprintf(stderr, "-fn[u|a|l]     control how filenames are interpreted\n");
    erts_fprintf(stderr, "\n");

    erts_fprintf(stderr, "-hms size      set minimum heap size in words (default %d)\n",
	       H_DEFAULT_SIZE);
    erts_fprintf(stderr, "-hmbs size     set minimum binary virtual heap size in words (default %d)\n",
	       VH_DEFAULT_SIZE);
    erts_fprintf(stderr, "-hmax size     set maximum heap size in words (default %d)\n",
	       H_DEFAULT_MAX_SIZE);
    erts_fprintf(stderr, "-hmaxk bool    enable or disable kill at max heap size (default true)\n");
    erts_fprintf(stderr, "-hmaxel bool   enable or disable error_logger report at max heap size (default true)\n");
    erts_fprintf(stderr, "-hmaxib bool   enable or disable including off-heap binaries into max heap size (default false)\n");
    erts_fprintf(stderr, "-hpds size     set initial process dictionary size (default %d)\n",
	       erts_pd_initial_size);
    erts_fprintf(stderr, "-hmqd  val     set default message queue data flag for processes;\n");
    erts_fprintf(stderr, "               valid values are: off_heap | on_heap\n");
    erts_fprintf(stderr, "\n");

    erts_fprintf(stderr, "-IOp number    set number of pollsets to be used to poll for I/O\n");
    erts_fprintf(stderr, "               (this value must be equal or smaller than the\n");
    erts_fprintf(stderr, "               number of poll threads; ignored if the current platform\n");
    erts_fprintf(stderr, "               does not support concurrent update of pollsets)\n");
    erts_fprintf(stderr, "-IOt number    set number of threads to be used to poll for I/O\n");
    erts_fprintf(stderr, "-IOPp number   set number of pollsets as a percentage of the\n");
    erts_fprintf(stderr, "               number of poll threads\n");
    erts_fprintf(stderr, "-IOPt number   set number of threads to be used to poll for I/O\n");
    erts_fprintf(stderr, "               as a percentage of the number of schedulers\n");
    erts_fprintf(stderr, "-i module      set the boot module (default init)\n");
    erts_fprintf(stderr, "\n");

#ifdef BEAMASM
    erts_fprintf(stderr, "-JDdump bool   enable or disable dumping of generated assembly code for each module loaded\n");
    erts_fprintf(stderr, "-JPcover true|false|line|line_counters|function|function_counters  enable or disable instrumentation for coverage\n");
    erts_fprintf(stderr, "-JPperf true|false|dump|map|fp|no_fp   enable or disable support for perf on Linux\n");
    erts_fprintf(stderr, "-JPperfdirectory <directory>    set the directory perf files are stored. Default: /tmp\n");
    erts_fprintf(stderr, "-JMsingle bool enable the use of single-mapped RWX memory for JIT:ed code\n");
    erts_fprintf(stderr, "\n");
#endif

    erts_fprintf(stderr, "-n[s|a|d]      control behavior of signals to ports (this flag is deprecated)\n");
    erts_fprintf(stderr, "\n");

    erts_fprintf(stderr, "-M<X> <Y>      set memory allocator switches;\n");
    erts_fprintf(stderr, "               see the erts_alloc(3) documentation for more info\n");
    erts_fprintf(stderr, "\n");

    erts_fprintf(stderr, "-pc <set>      control what characters are considered printable (default latin1)\n");
    erts_fprintf(stderr, "-pad bool      set default process async data (default false)\n");
    erts_fprintf(stderr, "-P number      set maximum number of processes on this node;\n");
    erts_fprintf(stderr, "               valid range is [%d-%d]\n",
		 ERTS_MIN_PROCESSES, ERTS_MAX_PROCESSES);
    erts_fprintf(stderr, "\n");

    erts_fprintf(stderr, "-Q number      set maximum number of ports on this node;\n");
    erts_fprintf(stderr, "               valid range is [%d-%d]\n",
		 ERTS_MIN_PORTS, ERTS_MAX_PORTS);
    erts_fprintf(stderr, "\n");

    erts_fprintf(stderr, "-R number      set compatibility release number;\n");
    erts_fprintf(stderr, "               valid range [%d-%d]\n",
		 this_rel-2, this_rel);
    erts_fprintf(stderr, "-r             force ets memory block to be moved on realloc\n");
    erts_fprintf(stderr, "-rg amount     set reader groups limit\n");
    erts_fprintf(stderr, "\n");

    erts_fprintf(stderr, "-sbt type      set scheduler bind type; valid types are:\n");
    erts_fprintf(stderr, "               u | ns | ts | ps | s | nnts | nnps| tnnps | db\n");
    erts_fprintf(stderr, "-sbwt val      set scheduler busy wait threshold; valid values are:\n");
    erts_fprintf(stderr, "               none | very_short | short | medium | long | very_long\n");
    erts_fprintf(stderr, "-sbwtdcpu val  set dirty CPU scheduler busy wait threshold; valid values are:\n");
    erts_fprintf(stderr, "               none | very_short | short | medium | long | very_long\n");
    erts_fprintf(stderr, "-sbwtdio val   set dirty IO scheduler busy wait threshold; valid values are:\n");
    erts_fprintf(stderr, "               none | very_short | short | medium | long | very_long\n");
    erts_fprintf(stderr, "-scl bool      enable/disable compaction of scheduler load\n");
    erts_fprintf(stderr, "-sct cput      set cpu topology\n");
    erts_fprintf(stderr, "-ssrct         skip reading cpu topology\n");
    erts_fprintf(stderr, "-secio bool    enable or disable eager check I/O scheduling\n");
#if ERTS_HAVE_SCHED_UTIL_BALANCING_SUPPORT_OPT
    erts_fprintf(stderr, "-sub bool      enable or disable scheduler utilization balancing\n");
#else
    erts_fprintf(stderr, "-sub false     disable scheduler utilization balancing\n");
#endif
    erts_fprintf(stderr, "-sws val       set scheduler wakeup strategy; valid values are:\n");
    erts_fprintf(stderr, "               default | legacy\n");
    erts_fprintf(stderr, "-swct val      set scheduler wake cleanup threshold; valid values are:\n");
    erts_fprintf(stderr, "               very_lazy | lazy | medium | eager | very_eager\n");
    erts_fprintf(stderr, "-swt val       set scheduler wakeup threshold; valid values are:\n");
    erts_fprintf(stderr, "               very_low | low | medium | high | very_high\n");
    erts_fprintf(stderr, "-swtdcpu val   set dirty CPU scheduler wakeup threshold; valid values are:\n");
    erts_fprintf(stderr, "               very_low | low | medium | high | very_high\n");
    erts_fprintf(stderr, "-swtdio val    set dirty IO scheduler wakeup threshold; valid values are:\n");
    erts_fprintf(stderr, "               very_low | low | medium | high | very_high\n");
    erts_fprintf(stderr, "-sss size      suggest stack size in kilo words for scheduler threads;\n");
    erts_fprintf(stderr, "               valid range is [%d-%d] (default %d)\n",
		 ERTS_SCHED_THREAD_MIN_STACK_SIZE,
		 ERTS_SCHED_THREAD_MAX_STACK_SIZE,
                 ERTS_DEFAULT_SCHED_STACK_SIZE);
    erts_fprintf(stderr, "-sssdcpu size  suggest stack size in kilo words for dirty CPU scheduler\n");
    erts_fprintf(stderr, "               threads; valid range is [%d-%d] (default %d)\n",
		 ERTS_SCHED_THREAD_MIN_STACK_SIZE,
		 ERTS_SCHED_THREAD_MAX_STACK_SIZE,
                 ERTS_DEFAULT_DCPU_SCHED_STACK_SIZE);
    erts_fprintf(stderr, "-sssdio size   suggest stack size in kilo words for dirty IO scheduler\n");
    erts_fprintf(stderr, "               threads; valid range is [%d-%d] (default %d)\n",
		 ERTS_SCHED_THREAD_MIN_STACK_SIZE,
		 ERTS_SCHED_THREAD_MAX_STACK_SIZE,
                 ERTS_DEFAULT_DIO_SCHED_STACK_SIZE);
    erts_fprintf(stderr, "-spp Bool      set port parallelism scheduling hint\n");
    erts_fprintf(stderr, "-S n1:n2       set number of schedulers (n1), and number of\n");
    erts_fprintf(stderr, "               schedulers online (n2); maximum for both\n");
    erts_fprintf(stderr, "               numbers is %d\n",
		 ERTS_MAX_NO_OF_SCHEDULERS);
    erts_fprintf(stderr, "-SP p1:p2      specify schedulers (p1) and schedulers online (p2)\n");
    erts_fprintf(stderr, "               as percentages of logical processors configured and logical\n");
    erts_fprintf(stderr, "               processors available, respectively\n");
    erts_fprintf(stderr, "-SDcpu n1:n2   set number of dirty CPU schedulers (n1), and number of\n");
    erts_fprintf(stderr, "               dirty CPU schedulers online (n2); valid range for both\n");
    erts_fprintf(stderr, "               numbers is [1-%d], and n2 must be less than or equal to n1\n",
		 ERTS_MAX_NO_OF_DIRTY_CPU_SCHEDULERS);
    erts_fprintf(stderr, "-SDPcpu p1:p2  specify dirty CPU schedulers (p1) and dirty CPU schedulers\n");
    erts_fprintf(stderr, "               online (p2) as percentages of logical processors configured\n");
    erts_fprintf(stderr, "               and logical processors available, respectively\n");
    erts_fprintf(stderr, "-SDio n        set number of dirty I/O schedulers; valid range is [1-%d]\n",
		 ERTS_MAX_NO_OF_DIRTY_IO_SCHEDULERS);
    erts_fprintf(stderr, "-t size        set the maximum number of atoms the emulator can handle;\n");
    erts_fprintf(stderr, "               valid range is [%d-%d]\n",
		 MIN_ATOM_TABLE_SIZE, MAX_ATOM_TABLE_SIZE);
    erts_fprintf(stderr, "\n");

    erts_fprintf(stderr, "-T number      set modified timing level; valid range is [0-%d]\n",
		 ERTS_MODIFIED_TIMING_LEVELS-1);
    erts_fprintf(stderr, "\n");

    erts_fprintf(stderr, "-V             print Erlang version\n");
    erts_fprintf(stderr, "-v             turn on chatty mode (GCs will be reported etc)\n");
    erts_fprintf(stderr, "\n");

    erts_fprintf(stderr, "-W<i|w|e>      set error logger warnings mapping;\n");
    erts_fprintf(stderr, "               see error_logger documentation for details\n");
    erts_fprintf(stderr, "\n");

    erts_fprintf(stderr, "-zdbbl size    set the distribution buffer busy limit in kilobytes;\n");
    erts_fprintf(stderr, "               valid range is [1-%d]\n", INT_MAX/1024);
    erts_fprintf(stderr, "-zdntgc time   set delayed node table gc in seconds;\n");
    erts_fprintf(stderr, "               valid values are infinity or integers in the range [0-%d]\n",
		 ERTS_NODE_TAB_DELAY_GC_MAX);
    erts_fprintf(stderr, "-zosrl number  set outstanding requests limit for system processes,\n");
    erts_fprintf(stderr, "               valid range [1-%d]\n",
                 ERTS_MAX_PROCESSES);
    erts_fprintf(stderr, "-zhft timeout  set halt flush timeout,\n");
    erts_fprintf(stderr, "               valid range [0-%d] or infinity\n", INT_MAX);
    erts_fprintf(stderr, "\n");
    erts_fprintf(stderr, "Note that if the emulator is started with erlexec (typically\n");
    erts_fprintf(stderr, "from the erl script), these flags should be specified with +.\n");
    erts_fprintf(stderr, "\n\n");
    erts_exit(1, "");
}

static int
early_init(int *argc, char **argv, const beam_vm_options *options)
{
    /* Only dependencies required before general allocation belong here. */
    ErtsAllocInitOpts alloc_opts = ERTS_ALLOC_INIT_DEF_OPTS_INITER;
    int ncpu;
    int ncpuonln;
    int ncpuavail;
    int ncpuquota;
    int schdlrs;
    int schdlrs_onln;
    int schdlrs_percentage = 100;
    int schdlrs_onln_percentage = 100;
    int max_main_threads;
    int dirty_cpu_scheds;
    int dirty_cpu_scheds_online;
    int dirty_cpu_scheds_pctg = 100;
    int dirty_cpu_scheds_onln_pctg = 100;
    int dirty_io_scheds;
    int aux_threads = 1;
    int max_reader_groups;
    int reader_groups;
    int max_decentralized_counter_groups;
    int decentralized_counter_groups;
    char envbuf[21]; /* enough for any 64-bit integer */
    size_t envbufsz;
    int skip_read_topology = 0;
    int thread_error;

    if (argc && argv)
        erts_save_emu_args(*argc, argv);

    erts_sched_compact_load = 1;
    erts_printf_eterm_func = erts_printf_term;
    erts_backtrace_depth = DEFAULT_BACKTRACE_SIZE;
    erts_async_max_threads = ERTS_DEFAULT_NO_ASYNC_THREADS;
    erts_async_thread_suggested_stack_size = ERTS_ASYNC_THREAD_MIN_STACK_SIZE;
    H_MIN_SIZE = H_DEFAULT_SIZE;
    BIN_VH_MIN_SIZE = VH_DEFAULT_SIZE;
    H_MAX_SIZE = H_DEFAULT_MAX_SIZE;
    H_MAX_FLAGS = MAX_HEAP_SIZE_KILL|MAX_HEAP_SIZE_LOG;

    erts_term_init();

    erts_initialized = 0;

    erts_pre_early_init_cpu_topology(&max_decentralized_counter_groups,
                                     &max_reader_groups,
				     &ncpu,
				     &ncpuonln,
				     &ncpuavail,
				     &ncpuquota);

    ignore_break = 0;
    replace_intr = 0;
    program = argv ? argv[0] : (char *)erts_vm_program_name();

    erts_modified_timing_level = -1;

    erts_compat_rel = this_rel_num();

    if (!options)
        erts_sys_pre_init();
    erts_sys_vm_prepare();
    thread_error = ethr_runtime_begin();
    if (thread_error)
        erts_thr_fatal_error(thread_error, "prepare native VM thread resources");
    erts_atomic_init_nob(&exiting, 0);
    erts_thr_progress_pre_init();

    erts_atomic32_init_nob(&erts_writing_erl_crash_dump, 0L);
    erts_tsd_key_create_static(&erts_is_crash_dumping_key,"erts_is_crash_dumping_key");

    erts_atomic32_init_nob(&erts_max_gen_gcs,
			       (erts_aint32_t) ((Uint16) -1));

    erts_pre_init_process();

    /*
     * We need to know the number of schedulers to use before we
     * can initialize the allocators.
     */
    no_schedulers = (Uint) (ncpu > 0 ? ncpu : 1);

    if (ncpuavail > 0) {
        if (ncpuquota > 0) {
            no_schedulers_online = MIN(ncpuquota, ncpuavail);
        } else {
            no_schedulers_online = ncpuavail;
        }
    } else if (ncpuonln > 0) {
        no_schedulers_online = ncpuonln;
    } else {
        no_schedulers_online = no_schedulers;
    }

    schdlrs = no_schedulers;
    schdlrs_onln = no_schedulers_online;

    dirty_cpu_scheds = no_schedulers;
    dirty_cpu_scheds_online = no_schedulers_online;
    dirty_io_scheds = 10;

    envbufsz = sizeof(envbuf);

    /* erts_osenv hasn't been initialized yet, so we need to fall back to
     * erts_sys_explicit_host_getenv() */
    if (!options &&
        erts_sys_explicit_host_getenv("ERL_THREAD_POOL_SIZE", envbuf, &envbufsz) == 1)
	erts_async_max_threads = atoi(envbuf);
    else
	erts_async_max_threads = ERTS_DEFAULT_NO_ASYNC_THREADS;
    if (erts_async_max_threads > ERTS_MAX_NO_OF_ASYNC_THREADS)
	erts_async_max_threads = ERTS_MAX_NO_OF_ASYNC_THREADS;

    if (argc && argv) {
	int i = 1;
	while (i < *argc) {
	    if (sys_strcmp(argv[i], "--") == 0) { /* end of emulator options */
		i++;
		break;
	    }
	    if (argv[i][0] == '-') {
		switch (argv[i][1]) {
		case 'd': {
		    char *sub_param = argv[i]+2;
		    if (has_prefix("cg", sub_param)) {
			char *arg = get_arg(sub_param+2, argv[i+1], &i);
			if (sscanf(arg, "%d", &max_decentralized_counter_groups) != 1) {
			    erts_fprintf(stderr,
					 "bad decentralized counter groups limit: %s\n", arg);
			    erts_usage();
			}
			if (max_decentralized_counter_groups < 0) {
			    erts_fprintf(stderr,
					 "bad decentralized counter groups limit: %d\n",
					 max_decentralized_counter_groups);
			    erts_usage();
			}
		    }
		    break;
		}
		case 'r': {
		    char *sub_param = argv[i]+2;
		    if (has_prefix("g", sub_param)) {
			char *arg = get_arg(sub_param+1, argv[i+1], &i);
			if (sscanf(arg, "%d", &max_reader_groups) != 1) {
			    erts_fprintf(stderr,
					 "bad reader groups limit: %s\n", arg);
			    erts_usage();
			}
			if (max_reader_groups < 0) {
			    erts_fprintf(stderr,
					 "bad reader groups limit: %d\n",
					 max_reader_groups);
			    erts_usage();
			}
		    }
		    break;
		}
		case 'A': {
		    /* set number of threads in thread pool */
		    char *arg = get_arg(argv[i]+2, argv[i+1], &i);
		    if (((erts_async_max_threads = atoi(arg)) < ERTS_MIN_NO_OF_ASYNC_THREADS) ||
			(erts_async_max_threads > ERTS_MAX_NO_OF_ASYNC_THREADS)) {
			erts_fprintf(stderr,
				     "bad number of async threads %s\n",
				     arg);
			erts_usage();
			VERBOSE(DEBUG_SYSTEM, ("using %d async-threads\n",
					       erts_async_max_threads));
		    }
		    break;
		}
		case 's' : {
		    char *sub_param = argv[i]+2;
		    if (has_prefix("srct", sub_param)) {
			/* skip reading cpu topology */
			skip_read_topology = 1;
		    }
		    else if (has_prefix("ct", sub_param)) {
			/* cpu topology */
			skip_read_topology = 1;
		    }
		    break;
		}
		case 'S' :
		    if (argv[i][2] == 'P') {
			int ptot, ponln;
			char *arg = get_arg(argv[i]+3, argv[i+1], &i);
			switch (sscanf(arg, "%d:%d", &ptot, &ponln)) {
			case 0:
			    switch (sscanf(arg, ":%d", &ponln)) {
			    case 1:
				if (ponln < 0)
				    goto bad_SP;
				ptot = 100;
				goto chk_SP;
			    default:
				goto bad_SP;
			    }
			case 1:
			    if (ptot < 0)
				goto bad_SP;
			    ponln = ptot < 100 ? ptot : 100;
			    goto chk_SP;
			case 2:
			    if (ptot < 0 || ponln < 0)
				goto bad_SP;
			chk_SP:
			    schdlrs_percentage = ptot;
			    schdlrs_onln_percentage = ponln;
			    break;
			default:
                        bad_SP:
                            erts_fprintf(stderr,
                                         "bad schedulers percentage specifier %s\n",
                                         arg);
                            erts_usage();
                            break;
                        }

                        VERBOSE(DEBUG_SYSTEM,
                                ("using %d:%d scheduler percentages\n",
                                 schdlrs_percentage, schdlrs_onln_percentage));
                    }
		    else if (argv[i][2] == 'D') {
			char *arg;
			char *type = argv[i]+3;
			if (sys_strncmp(type, "Pcpu", 4) == 0) {
			    int ptot, ponln;
			    arg = get_arg(argv[i]+7, argv[i+1], &i);
			    switch (sscanf(arg, "%d:%d", &ptot, &ponln)) {
			    case 0:
				switch (sscanf(arg, ":%d", &ponln)) {
				case 1:
				    if (ponln < 0)
					goto bad_SDPcpu;
				    ptot = 100;
				    goto chk_SDPcpu;
				default:
				    goto bad_SDPcpu;
				}
			    case 1:
				if (ptot < 0)
				    goto bad_SDPcpu;
				ponln = ptot < 100 ? ptot : 100;
				goto chk_SDPcpu;
			    case 2:
				if (ptot < 0 || ponln < 0)
				    goto bad_SDPcpu;
			    chk_SDPcpu:
				dirty_cpu_scheds_pctg = ptot;
				dirty_cpu_scheds_onln_pctg = ponln;
				break;
			    default:
			    bad_SDPcpu:
				erts_fprintf(stderr,
					     "bad dirty CPU schedulers percentage specifier %s\n",
					     arg);
				erts_usage();
				break;
			    }
			    VERBOSE(DEBUG_SYSTEM,
				    ("using %d:%d dirty CPU scheduler percentages\n",
				     dirty_cpu_scheds_pctg, dirty_cpu_scheds_onln_pctg));
			} else if (sys_strncmp(type, "cpu", 3) == 0) {
			    int tot, onln;
			    arg = get_arg(argv[i]+6, argv[i+1], &i);
			    switch (sscanf(arg, "%d:%d", &tot, &onln)) {
			    case 0:
				switch (sscanf(arg, ":%d", &onln)) {
				case 1:
				    tot = no_schedulers;
				    goto chk_SDcpu;
				default:
				    goto bad_SDcpu;
				}
			    case 1:
				onln = tot < dirty_cpu_scheds_online ?
				    tot : dirty_cpu_scheds_online;
                                ERTS_FALLTHROUGH();
			    case 2:
			    chk_SDcpu:
				if (tot > 0)
				    dirty_cpu_scheds = tot;
				else
				    dirty_cpu_scheds = no_schedulers + tot;
				if (onln > 0)
				    dirty_cpu_scheds_online = onln;
				else
				    dirty_cpu_scheds_online = no_schedulers_online + onln;
				if (dirty_cpu_scheds < 1 ||
				    ERTS_MAX_NO_OF_DIRTY_CPU_SCHEDULERS < dirty_cpu_scheds) {
				    erts_fprintf(stderr,
						 "bad amount of dirty CPU schedulers %d\n",
						 tot);
				    erts_usage();
				}
				if (dirty_cpu_scheds_online < 1 ||
				    dirty_cpu_scheds < dirty_cpu_scheds_online) {
				    erts_fprintf(stderr,
						 "bad amount of dirty CPU schedulers online %d "
						 "(total amount of dirty CPU schedulers %d)\n",
						 dirty_cpu_scheds_online, dirty_cpu_scheds);
				    erts_usage();
				}
				break;
			    default:
			    bad_SDcpu:
				erts_fprintf(stderr,
					     "bad amount of dirty CPU schedulers %s\n",
					     arg);
				erts_usage();
				break;
			    }
			    VERBOSE(DEBUG_SYSTEM,
				    ("using %d:%d dirty CPU scheduler(s)\n", tot, onln));
			} else if (sys_strncmp(type, "io", 2) == 0) {
			    arg = get_arg(argv[i]+5, argv[i+1], &i);
			    dirty_io_scheds = atoi(arg);
			    if (dirty_io_scheds < 1 ||
				dirty_io_scheds > ERTS_MAX_NO_OF_DIRTY_IO_SCHEDULERS) {
				erts_fprintf(stderr,
					     "bad number of dirty I/O schedulers %s\n",
					     arg);
				erts_usage();
			    }
			    VERBOSE(DEBUG_SYSTEM,
				    ("using %d dirty I/O scheduler(s)\n", dirty_io_scheds));
			} else {
			    erts_fprintf(stderr,
					 "bad or missing dirty scheduler specifier: %s\n",
					 argv[i]);
			    erts_usage();
			    break;
			}
		    }
		    else {
			int tot, onln;
			char *arg = get_arg(argv[i]+2, argv[i+1], &i);
			switch (sscanf(arg, "%d:%d", &tot, &onln)) {
			case 0:
			    switch (sscanf(arg, ":%d", &onln)) {
			    case 1:
				tot = no_schedulers;
				goto chk_S;
			    default:
				goto bad_S;
			    }
			case 1:
			    onln = tot < schdlrs_onln ? tot : schdlrs_onln;
                            ERTS_FALLTHROUGH();
			case 2:
			chk_S:
			    if (tot > 0)
				schdlrs = tot;
			    else
				schdlrs = no_schedulers + tot;
			    if (onln > 0)
				schdlrs_onln = onln;
			    else
				schdlrs_onln = no_schedulers_online + onln;
			    if (schdlrs < 1 || ERTS_MAX_NO_OF_SCHEDULERS < schdlrs) {
				erts_fprintf(stderr,
					     "bad amount of schedulers %d\n",
					     tot);
				erts_usage();
			    }
			    if (schdlrs_onln < 1 || schdlrs < schdlrs_onln) {
				erts_fprintf(stderr,
					     "bad amount of schedulers online %d "
					     "(total amount of schedulers %d)\n",
					     schdlrs_onln, schdlrs);
				erts_usage();
			    }
			    break;
			default:
			bad_S:
			    erts_fprintf(stderr,
					 "bad amount of schedulers %s\n",
					 arg);
			    erts_usage();
			    break;
			}

			VERBOSE(DEBUG_SYSTEM,
				("using %d:%d scheduler(s)\n", tot, onln));
		    }
                    break;
		default:
		    break;
		}
	    }
	    i++;
	}

        if (erts_async_max_threads < 1)
            erts_async_max_threads = 1;

	/* apply any scheduler percentages */
	if (schdlrs_percentage != 100 || schdlrs_onln_percentage != 100) {
	    schdlrs = schdlrs * schdlrs_percentage / 100;
	    schdlrs_onln = schdlrs_onln * schdlrs_onln_percentage / 100;
	    if (schdlrs < 1)
                schdlrs = 1;
            if (ERTS_MAX_NO_OF_SCHEDULERS < schdlrs) {
		erts_fprintf(stderr,
			     "bad schedulers percentage %d "
			     "(total amount of schedulers %d)\n",
			     schdlrs_percentage, schdlrs);
		erts_usage();
	    }
	    if (schdlrs_onln < 1)
                schdlrs_onln = 1;
            if (schdlrs < schdlrs_onln) {
		erts_fprintf(stderr,
			     "bad schedulers online percentage %d "
			     "(total amount of schedulers %d, online %d)\n",
			     schdlrs_onln_percentage, schdlrs, schdlrs_onln);
		erts_usage();
	    }
	}
	/* apply any dirty scheduler precentages */
	if (dirty_cpu_scheds_pctg != 100 || dirty_cpu_scheds_onln_pctg != 100) {
	    dirty_cpu_scheds = dirty_cpu_scheds * dirty_cpu_scheds_pctg / 100;
	    dirty_cpu_scheds_online = dirty_cpu_scheds_online * dirty_cpu_scheds_onln_pctg / 100;
	}
	if (dirty_cpu_scheds > schdlrs)
	    dirty_cpu_scheds = schdlrs;
        if (dirty_cpu_scheds < 1)
            dirty_cpu_scheds = 1;
	if (dirty_cpu_scheds_online > schdlrs_onln)
	    dirty_cpu_scheds_online = schdlrs_onln;
	if (dirty_cpu_scheds_online < 1)
	    dirty_cpu_scheds_online = 1;
    }
    if (options) {
        schdlrs = options->schedulers;
        schdlrs_onln = options->schedulers_online;
        dirty_cpu_scheds = options->dirty_cpu_schedulers;
        dirty_cpu_scheds_online = options->dirty_cpu_schedulers_online;
        dirty_io_scheds = options->dirty_io_schedulers;
        erts_async_max_threads = options->async_threads;
    }

    no_schedulers = schdlrs;
    no_schedulers_online = schdlrs_onln;

    erts_no_schedulers = (Uint) no_schedulers;
    erts_no_dirty_cpu_schedulers = no_dirty_cpu_schedulers = dirty_cpu_scheds;
    no_dirty_cpu_schedulers_online = dirty_cpu_scheds_online;
    erts_no_dirty_io_schedulers = no_dirty_io_schedulers = dirty_io_scheds;
    erts_early_init_scheduling(no_schedulers + 1 + dirty_cpu_scheds);

    alloc_opts.ncpu = ncpu;
    if (options && options->allocator_tagging != BEAM_ALLOCATOR_TAGGING_DEFAULT)
        alloc_opts.allocation_tags =
            options->allocator_tagging == BEAM_ALLOCATOR_TAGGING_ENABLED;
    erts_alloc_init(argc, argv, &alloc_opts); /* Handles (and removes)
						 -M flags. */
    aux_threads += erts_no_dirty_alloc_instances;
    /* Require allocators */
    erts_errno_init();

    if (options)
        erts_init_check_io_config(options->poll_threads);
    else
        erts_init_check_io(argc, argv);

    /*
     * Thread progress management:
     *
     * * Managed threads:
     * ** Scheduler threads (see erl_process.c)
     * ** Aux threads (see erl_process.c)
     * ** Sys message dispatcher thread (see erl_trace.c)
     * ** IO Poll threads (see erl_check_io.c)
     *
     * * Unmanaged threads that need to register:
     * ** Async threads (see erl_async.c)
     * ** Dirty scheduler threads
     */
    erts_thr_progress_init(no_schedulers,
			   (no_schedulers
			    + aux_threads
			    + 1
			    + erts_no_poll_threads),
			   (erts_async_max_threads
			    + erts_no_dirty_cpu_schedulers
			    + erts_no_dirty_io_schedulers));
    erts_thr_q_init();
    erts_init_utils();
    erts_early_init_cpu_topology(no_schedulers,
				 &max_main_threads,
				 max_reader_groups,
				 &reader_groups,
                                 max_decentralized_counter_groups,
                                 &decentralized_counter_groups,
                                 skip_read_topology);
    erts_flxctr_setup(decentralized_counter_groups);
    thread_error = ethr_runtime_bind(max_main_threads, reader_groups);
    if (thread_error)
        erts_thr_fatal_error(thread_error, "bind native VM thread dimensions");

    erts_errno_late_init();
    erts_msacc_early_init();

#ifdef ERTS_ENABLE_LOCK_CHECK
    erts_lc_late_init();
#endif

#ifdef ERTS_ENABLE_LOCK_COUNT
    erts_lcnt_late_init();
#endif

    if (argc && argv)
        erl_sys_args(argc, argv);
    else
        erts_sys_vm_init();

    /* Creates threads on Windows that depend on the arguments, so has to be after erl_sys_args */
    erl_sys_init();

    erts_ets_realloc_always_moves = false;
    erts_ets_always_compress = false;
    erts_dist_buf_busy_limit = ERTS_DE_BUSY_LIMIT;

    return ncpu;
}


static void
start_system_processes(void)
{
	/*
	 * System processes that are *always* alive. If they terminate
	 * they bring the whole VM down.
	 */
	Eterm pid;

	pid = erl_system_process_otp(erts_init_process_id,
                                     "erts_code_purger", !0,
                                     PRIORITY_HIGH);
	erts_code_purger
	    = (Process *) erts_ptab_pix2intptr_ddrb(&erts_proc,
						    internal_pid_index(pid));
	ASSERT(erts_code_purger && erts_code_purger->common.id == pid);
	erts_proc_inc_refc(erts_code_purger);

	pid = erl_system_process_otp(erts_init_process_id,
                                     "erts_literal_area_collector",
                                     !0, PRIORITY_HIGH);
	erts_literal_area_collector
	    = (Process *) erts_ptab_pix2intptr_ddrb(&erts_proc,
						    internal_pid_index(pid));
	ASSERT(erts_literal_area_collector
	       && erts_literal_area_collector->common.id == pid);
	erts_proc_inc_refc(erts_literal_area_collector);

	pid = erl_system_process_otp(erts_init_process_id,
                                     "erts_dirty_process_signal_handler",
                                     !0, PRIORITY_NORMAL);
        erts_dirty_process_signal_handler
	    = (Process *) erts_ptab_pix2intptr_ddrb(&erts_proc,
						    internal_pid_index(pid));
	ASSERT(erts_dirty_process_signal_handler
	       && erts_dirty_process_signal_handler->common.id == pid);
	erts_proc_inc_refc(erts_dirty_process_signal_handler);

	pid = erl_system_process_otp(erts_init_process_id,
                                     "erts_dirty_process_signal_handler",
                                     !0, PRIORITY_HIGH);
        erts_dirty_process_signal_handler_high
	    = (Process *) erts_ptab_pix2intptr_ddrb(&erts_proc,
						    internal_pid_index(pid));
	ASSERT(erts_dirty_process_signal_handler_high
	       && erts_dirty_process_signal_handler_high->common.id == pid);
	erts_proc_inc_refc(erts_dirty_process_signal_handler_high);

	pid = erl_system_process_otp(erts_init_process_id,
                                     "erts_dirty_process_signal_handler",
                                     !0, PRIORITY_MAX);
        erts_dirty_process_signal_handler_max
	    = (Process *) erts_ptab_pix2intptr_ddrb(&erts_proc,
						    internal_pid_index(pid));
	ASSERT(erts_dirty_process_signal_handler_max
	       && erts_dirty_process_signal_handler_max->common.id == pid);
	erts_proc_inc_refc(erts_dirty_process_signal_handler_max);

        pid = erl_system_process_otp(erts_init_process_id,
                                     "erts_trace_cleaner", !0,
                                     PRIORITY_NORMAL);
        erts_trace_cleaner
            = (Process *) erts_ptab_pix2intptr_ddrb(&erts_proc,
                                                    internal_pid_index(pid));
        ASSERT(erts_trace_cleaner && erts_trace_cleaner->common.id == pid);
        erts_proc_inc_refc(erts_trace_cleaner);

}


static void
bare_enter_native(fenv_t *host_fenv)
{
    if (fegetenv(host_fenv) != 0)
        ERTS_INTERNAL_ERROR("Unable to save host floating-point environment");
    if (fesetenv(FE_DFL_ENV) != 0)
        ERTS_INTERNAL_ERROR("Unable to initialize VM floating-point environment");
}

static void
bare_leave_native(const fenv_t *host_fenv)
{
    if (fesetenv(host_fenv) != 0)
        ERTS_INTERNAL_ERROR("Unable to restore host floating-point environment");
}

void
erts_bare_init(const beam_vm_options *options)
{
    int ncpu, time_correction;
    ErtsTimeWarpMode time_warp_mode;
    fenv_t host_fenv;

    bare_enter_native(&host_fenv);
    ncpu = early_init(NULL, NULL, options);
    ignore_break = options->break_policy == BEAM_BREAK_IGNORE;
    set_default_time_adj(&time_correction, &time_warp_mode);
    user_requested_db_max_tabs = options->ets_limit;
    erts_atomic32_set_nob(&erts_max_gen_gcs, options->fullsweep_after);
    erts_error_logger_warnings = am_warning;
    erts_sched_thread_suggested_stack_size = ERTS_DEFAULT_SCHED_STACK_SIZE;
    erts_dcpu_sched_thread_suggested_stack_size = ERTS_DEFAULT_DCPU_SCHED_STACK_SIZE;
    erts_dio_sched_thread_suggested_stack_size = ERTS_DEFAULT_DIO_SCHED_STACK_SIZE;
    erts_atom_table_size = options->atom_limit;
    H_MIN_SIZE = options->min_heap_size;
    BIN_VH_MIN_SIZE = options->min_bin_vheap_size;
    H_MAX_SIZE = options->max_heap_size;

    erl_init(ncpu, options->process_limit, 0,
             options->port_limit, options->port_limit_ignore_files, 0,
             2 * options->schedulers,
             time_correction, time_warp_mode,
             ERTS_NODE_TAB_DELAY_GC_DEFAULT, ERTS_DB_SPNCNT_NORMAL);
    load_preloaded();
    /* Keep the single-threaded initial code index open until explicit start.
     * No Erlang execution, worker or native child helper starts here. */
    bare_leave_native(&host_fenv);
}

beam_status
erts_bare_load_initial(beam_bytes bytes)
{
    Binary *prepared;
    Eterm module = NIL, result;
    beam_status status;
    int i;
    fenv_t host_fenv;

    ASSERT(erts_bare_mode && !erts_initialized);
    bare_enter_native(&host_fenv);
    prepared = erts_alloc_loader_state();
    result = erts_prepare_loading(prepared, NULL, NIL, &module,
                                  bytes.data, bytes.size);
    if (result != NIL) {
        status = BEAM_INVALID_BEAM;
        goto done;
    }
    if (erts_has_code_on_load(prepared) == am_true) {
        beam_load_prepared_free(prepared);
        status = BEAM_UNSUPPORTED;
        goto done;
    }
    for (i = 0; i < module_code_size(erts_staging_code_ix()); i++) {
        Module *existing = module_code(i, erts_staging_code_ix());
        if (existing->module == atom_val(module)
            && (existing->curr.code_hdr || existing->old.code_hdr
                || existing->on_load)) {
            beam_load_prepared_free(prepared);
            status = BEAM_ALREADY_LOADED;
            goto done;
        }
    }
    result = erts_finish_loading(prepared, NULL, 0, &module);
    ASSERT(result == NIL);
    status = BEAM_OK;
done:
    bare_leave_native(&host_fenv);
    return status;
}

beam_status
erts_bare_start(const beam_entry *entry)
{
    fenv_t host_fenv;
    Process parent;
    Eterm arguments = NIL, module = NIL, function = NIL;
    beam_status status = BEAM_OK;
    int parent_initialized = 0;

    ASSERT(erts_bare_mode && !erts_initialized);
    bare_enter_native(&host_fenv);
    if (entry) {
        ErtsHeapFactory factory;
        const byte *cursor = entry->arguments.data;
        Sint words, arity;
        if (entry->arguments.size < 2 || cursor[0] != VERSION_MAGIC ||
            cursor[1] == COMPRESSED ||
            (words = erts_decode_ext_size_public(cursor, entry->arguments.size)) < 0) {
            status = BEAM_INVALID_TERM;
            goto done;
        }
        erts_init_empty_process(&parent);
        erts_proc_lock(&parent, ERTS_PROC_LOCK_MAIN);
        parent_initialized = 1;
        if (words) erts_factory_proc_prealloc_init(&factory, &parent, words);
        else erts_factory_dummy_init(&factory);
        arguments = erts_decode_ext(&factory, &cursor, 0);
        if (is_non_value(arguments)) {
            erts_factory_undo(&factory);
            status = BEAM_INVALID_TERM;
            goto done;
        }
        erts_factory_close(&factory);
        arity = erts_list_length(arguments);
        if (cursor != entry->arguments.data + entry->arguments.size || arity < 0 || arity > MAX_ARG) {
            status = BEAM_INVALID_TERM;
            goto done;
        }
        module = erts_atom_put((const byte *)entry->module, strlen(entry->module), ERTS_ATOM_ENC_UTF8, 0);
        function = erts_atom_put((const byte *)entry->function, strlen(entry->function), ERTS_ATOM_ENC_UTF8, 0);
        if (is_non_value(module) || is_non_value(function)) {
            status = BEAM_INVALID_ARGUMENT;
            goto done;
        }
        if (!erts_find_function(module, function, arity, erts_staging_code_ix())) {
            status = BEAM_NOT_FOUND;
            goto done;
        }
    }
    /* Native infrastructure starts before workers. Process policy remains
     * deferred until entry commit; setup can still fail and return to C. */
    erl_sys_late_init();
    erts_end_staging_code_ix();
    erts_commit_staging_code_ix();
    erts_initialized = 1;
    if (entry) {
        ErlSpawnOpts so;
        Process *root;
        ERTS_SET_DEFAULT_SPAWN_OPTS(&so);
        erts_init_process_id = erl_spawn_system_process(&parent, module, function, arguments, &so);
        ASSERT(is_internal_pid(erts_init_process_id));
        /* Create the actual root before any other process. Keep its supplied
         * entry suspended until composition activation is acknowledged. */
        root = erts_pid2proc(NULL, 0, erts_init_process_id, ERTS_PROC_LOCK_MAIN);
        erts_suspend(root, ERTS_PROC_LOCK_MAIN, NULL);
        erts_proc_unlock(root, ERTS_PROC_LOCK_MAIN);
    } else {
        erts_init_process_id = erl_first_process_bare();
    }
    if (parent_initialized) {
        erts_proc_unlock(&parent, ERTS_PROC_LOCK_MAIN);
        erts_cleanup_empty_process(&parent);
        parent_initialized = 0;
    }
    ASSERT(erts_init_process_id != ERTS_INVALID_PID);
    start_system_processes();
    erts_start_async();
    erts_start_trace_dispatcher();
    erts_start_schedulers();
#ifdef ERTS_ENABLE_LOCK_COUNT
    erts_lcnt_post_startup();
#endif
done:
    if (parent_initialized) {
        erts_proc_unlock(&parent, ERTS_PROC_LOCK_MAIN);
        erts_cleanup_empty_process(&parent);
    }
    bare_leave_native(&host_fenv);
    return status;
}

void
erts_bare_reset_init(void)
{
    ASSERT(erts_bare_is_stopped());
    erts_initialized = 0;
    erts_init_process_id = ERTS_INVALID_PID;
    program = NULL;
    boot_argc = 0;
    boot_argv = NULL;
    erts_halt_flush_timeout = -1;
    modified_sched_thread_suggested_stack_size = 0;
    erts_debug_flags = 0;
    erts_pd_initial_size = 8;
    erts_no_crash_dump = 0;
    erts_no_line_info = 0;
#ifdef BEAMASM
    erts_coverage_mode = ERTS_COV_NONE;
    erts_jit_asm_dump = 0;
#endif
#ifdef DEBUG
    verbose = 0;
#endif
}


__decl_noreturn void erts_thr_fatal_error(int err, const char *what)
{
    const char *errstr = err ? strerror(err) : NULL;
    if (err == ENOMEM) {
        erts_exit(ERTS_DUMP_EXIT, "Failed to %s: %s%s(%d)\n",
                  what,
                  errstr ? errstr : "",
                  errstr ? " " : "",
                  err);
    } else {
        erts_fprintf(stderr,
                     "Failed to %s: %s%s(%d)\n",
                     what,
                     errstr ? errstr : "",
                     errstr ? " " : "",
                     err);
        abort();
    }
}


static void
system_cleanup(int flush)
{
    /*
     * Make sure only one thread exits the runtime system.
     */
    if (erts_atomic_inc_read_nob(&exiting) != 1) {
	/*
	 * Another thread is currently exiting the system;
	 * wait for it to do its job.
	 */
	if (erts_thr_progress_is_managed_thread()) {
	    /*
	     * The exiting thread might be waiting for
	     * us to block; need to update status...
	     */
	    erts_thr_progress_active(erts_thr_prgr_data(NULL), 0);
	    erts_thr_progress_prepare_wait(erts_thr_prgr_data(NULL));
	}
	/* Wait forever... */
	while (1)
	    erts_milli_sleep(10000000);
    }

    /* No cleanup wanted if ...
     * 1. we are about to do an abnormal exit
     * 2. we haven't finished initializing, or
     * 3. another thread than the main thread is performing the exit
     *    (in threaded non smp case).
     */

    if (!flush || !erts_initialized)
	return;

    /*
     * We only flush as a result of calling erts_halt() (which in turn
     * is called from the erlang:halt() BIF when flushing is enabled);
     * otherwise, flushing wont work properly. If erts_halt() has
     * been called, 'erts_halt_code' won't equal INT_MIN...
     */
    ASSERT(erts_halt_code != INT_MIN);

    /*
     * Nif on-halt handlers may have been added after we initiated
     * a halt. If so, make sure that these late added handlers are
     * executed as well..
     */
    erts_nif_execute_on_halt();

#ifdef ERTS_ENABLE_LOCK_CHECK
    erts_lc_check_exact(NULL, 0);
#endif

    erts_exit_flush_async();

    /*
     * Wait for all NIF calls with delayed halt functionality
     * enabled to complete before we continue...
     */
    erts_nif_wait_calls();
}

static int erts_exit_code;

static __decl_noreturn void __noreturn
erts_exit_vv(int n, int flush, const char *fmt, va_list args1, va_list args2)
{
    system_cleanup(flush);

    if (fmt != NULL && *fmt != '\0')
	erl_error(fmt, args2);	/* Print error message. */

    erts_exit_code = n;

    /* Produce an Erlang crash dump if error */
    if (((n == ERTS_ERROR_EXIT && erts_no_crash_dump == 0) || n == ERTS_DUMP_EXIT)
	&& erts_initialized) {
	erl_crash_dump_v((char*) NULL, 0, fmt, args1);
    }

    erts_exit_epilogue(flush);
}

__decl_noreturn void __noreturn erts_exit_epilogue(int flush)
{
    int n = erts_exit_code;

    sys_tty_reset(n);

#ifdef ERTS_GCOV
    flush = !0; /* otherwise we get no results... */
#endif

    if (n == ERTS_INTR_EXIT)
	(void) (flush ? exit(0) : _exit(0));
    else if (n == ERTS_DUMP_EXIT)
	ERTS_EXIT_AFTER_DUMP(1);
    else if (n == ERTS_ERROR_EXIT || n == ERTS_ABORT_EXIT)
        abort();
    (void) (flush ? exit(n) : _exit(n));
}

/* Exit without flushing */
void beam_native_fail(beam_native_failure_mode mode, const char *fmt, ...)
{
    int reason;
    va_list args1, args2;
    switch (mode) {
    case BEAM_NATIVE_FAILURE_ABORT: reason = ERTS_ABORT_EXIT; break;
    case BEAM_NATIVE_FAILURE_DUMP: reason = ERTS_DUMP_EXIT; break;
    default: reason = ERTS_ERROR_EXIT; break;
    }
    va_start(args1, fmt);
    va_start(args2, fmt);
    erts_exit_vv(reason, 0, fmt, args1, args2);
    va_end(args2);
    va_end(args1);
}

__decl_noreturn void __noreturn erts_exit(int n, const char *fmt, ...)
{
    va_list args1, args2;
    va_start(args1, fmt);
    va_start(args2, fmt);
    erts_exit_vv(n, 0, fmt, args1, args2);
    va_end(args2);
    va_end(args1);
}

/*
 * Exit after flushing. This is a continuation of erts_halt() and wont
 * work properly if called by its own without proper initialization
 * as made in erts_halt().
 */
__decl_noreturn void __noreturn erts_flush_exit(int n, char *fmt, ...)
{
    va_list args1, args2;
    va_start(args1, fmt);
    va_start(args2, fmt);
    erts_exit_vv(n, 1, fmt, args1, args2);
    va_end(args2);
    va_end(args1);
}

void erts_halt_flush_timeout_callback(void *arg)
{
    _exit(ERTS_HALT_FLUSH_TIMEOUT_EXIT_CODE);
}
