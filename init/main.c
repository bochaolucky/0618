/*
 *  linux/init/main.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  GK 2/5/95  -  Changed to support mounting root fs via NFS
 *  Added initrd & change_root: Werner Almesberger & Hans Lermen, Feb '96
 *  Moan early if gcc is old, avoiding bogus kernels - Paul Gortmaker, May '96
 *  Simplified starting of init:  Michael A. Griffith <grif@acm.org>
 */

#define DEBUG		/* Enable initcall_debug */

#include <linux/types.h>
#include <linux/extable.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/binfmts.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/stackprotector.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/bootmem.h>
#include <linux/acpi.h>
#include <linux/console.h>
#include <linux/nmi.h>
#include <linux/percpu.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/kernel_stat.h>
#include <linux/start_kernel.h>
#include <linux/security.h>
#include <linux/smp.h>
#include <linux/profile.h>
#include <linux/rcupdate.h>
#include <linux/moduleparam.h>
#include <linux/kallsyms.h>
#include <linux/writeback.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/cgroup.h>
#include <linux/efi.h>
#include <linux/tick.h>
#include <linux/interrupt.h>
#include <linux/taskstats_kern.h>
#include <linux/delayacct.h>
#include <linux/unistd.h>
#include <linux/rmap.h>
#include <linux/mempolicy.h>
#include <linux/key.h>
#include <linux/buffer_head.h>
#include <linux/page_ext.h>
#include <linux/debug_locks.h>
#include <linux/debugobjects.h>
#include <linux/lockdep.h>
#include <linux/kmemleak.h>
#include <linux/pid_namespace.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/init.h>
#include <linux/signal.h>
#include <linux/idr.h>
#include <linux/kgdb.h>
#include <linux/ftrace.h>
#include <linux/async.h>
#include <linux/sfi.h>
#include <linux/shmem_fs.h>
#include <linux/slab.h>
#include <linux/perf_event.h>
#include <linux/ptrace.h>
#include <linux/pti.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/sched_clock.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/context_tracking.h>
#include <linux/random.h>
#include <linux/list.h>
#include <linux/integrity.h>
#include <linux/proc_ns.h>
#include <linux/io.h>
#include <linux/cache.h>
#include <linux/rodata_test.h>

#include <asm/io.h>
#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/cacheflush.h>

static int kernel_init(void *);

extern void init_IRQ(void);
extern void radix_tree_init(void);

/*
 * Debug helper: via this flag we know that we are in 'early bootup code'
 * where only the boot processor is running with IRQ disabled.  This means
 * two things - IRQ must not be enabled before the flag is cleared and some
 * operations which are not allowed with IRQ disabled are allowed while the
 * flag is set.
 */
bool early_boot_irqs_disabled __read_mostly;

enum system_states system_state __read_mostly;
EXPORT_SYMBOL(system_state);

/*
 * Boot command-line arguments
 */
#define MAX_INIT_ARGS CONFIG_INIT_ENV_ARG_LIMIT
#define MAX_INIT_ENVS CONFIG_INIT_ENV_ARG_LIMIT

extern void time_init(void);
/* Default late time init is NULL. archs can override this later. */
void (*__initdata late_time_init)(void);

/* Untouched command line saved by arch-specific code. */
char __initdata boot_command_line[COMMAND_LINE_SIZE];
/* Untouched saved command line (eg. for /proc) */
char *saved_command_line;
/* Command line for parameter parsing */
static char *static_command_line;
/* Command line for per-initcall parameter parsing */
static char *initcall_command_line;

static char *execute_command;
static char *ramdisk_execute_command;

/*
 * Used to generate warnings if static_key manipulation functions are used
 * before jump_label_init is called.
 */
bool static_key_initialized __read_mostly;
EXPORT_SYMBOL_GPL(static_key_initialized);

/*
 * If set, this is an indication to the drivers that reset the underlying
 * device before going ahead with the initialization otherwise driver might
 * rely on the BIOS and skip the reset operation.
 *
 * This is useful if kernel is booting in an unreliable environment.
 * For ex. kdump situation where previous kernel has crashed, BIOS has been
 * skipped and devices will be in unknown state.
 */
unsigned int reset_devices;
EXPORT_SYMBOL(reset_devices);

static int __init set_reset_devices(char *str)
{
	reset_devices = 1;
	return 1;
}

__setup("reset_devices", set_reset_devices);

static const char *argv_init[MAX_INIT_ARGS+2] = { "init", NULL, };
const char *envp_init[MAX_INIT_ENVS+2] = { "HOME=/", "TERM=linux", NULL, };
static const char *panic_later, *panic_param;

extern const struct obs_kernel_param __setup_start[], __setup_end[];

static bool __init obsolete_checksetup(char *line)
{
	const struct obs_kernel_param *p;
	bool had_early_param = false;

	p = __setup_start;
	do {
		int n = strlen(p->str);
		if (parameqn(line, p->str, n)) {
			if (p->early) {
				/* Already done in parse_early_param?
				 * (Needs exact match on param part).
				 * Keep iterating, as we can have early
				 * params and __setups of same names 8( */
				if (line[n] == '\0' || line[n] == '=')
					had_early_param = true;
			} else if (!p->setup_func) {
				pr_warn("Parameter %s is obsolete, ignored\n",
					p->str);
				return true;
			} else if (p->setup_func(line + n))
				return true;
		}
		p++;
	} while (p < __setup_end);

	return had_early_param;
}

/*
 * This should be approx 2 Bo*oMips to start (note initial shift), and will
 * still work even if initially too large, it will just take slightly longer
 */
unsigned long loops_per_jiffy = (1<<12);
EXPORT_SYMBOL(loops_per_jiffy);

static int __init debug_kernel(char *str)
{
	console_loglevel = CONSOLE_LOGLEVEL_DEBUG;
	return 0;
}

static int __init quiet_kernel(char *str)
{
	console_loglevel = CONSOLE_LOGLEVEL_QUIET;
	return 0;
}

early_param("debug", debug_kernel);
early_param("quiet", quiet_kernel);

static int __init loglevel(char *str)
{
	int newlevel;

	/*
	 * Only update loglevel value when a correct setting was passed,
	 * to prevent blind crashes (when loglevel being set to 0) that
	 * are quite hard to debug
	 */
	if (get_option(&str, &newlevel)) {
		console_loglevel = newlevel;
		return 0;
	}

	return -EINVAL;
}

early_param("loglevel", loglevel);

/* Change NUL term back to "=", to make "param" the whole string. */
static int __init repair_env_string(char *param, char *val,
				    const char *unused, void *arg)
{
	if (val) {
		/* param=val or param="val"? */
		if (val == param+strlen(param)+1)
			val[-1] = '=';
		else if (val == param+strlen(param)+2) {
			val[-2] = '=';
			memmove(val-1, val, strlen(val)+1);
			val--;
		} else
			BUG();
	}
	return 0;
}

/* Anything after -- gets handed straight to init. */
static int __init set_init_arg(char *param, char *val,
			       const char *unused, void *arg)
{
	unsigned int i;

	if (panic_later)
		return 0;

	repair_env_string(param, val, unused, NULL);

	for (i = 0; argv_init[i]; i++) {
		if (i == MAX_INIT_ARGS) {
			panic_later = "init";
			panic_param = param;
			return 0;
		}
	}
	argv_init[i] = param;
	return 0;
}

/*
 * Unknown boot options get handed to init, unless they look like
 * unused parameters (modprobe will find them in /proc/cmdline).
 */
static int __init unknown_bootoption(char *param, char *val,
				     const char *unused, void *arg)
{
	repair_env_string(param, val, unused, NULL);

	/* Handle obsolete-style parameters */
	if (obsolete_checksetup(param))
		return 0;

	/* Unused module parameter. */
	if (strchr(param, '.') && (!val || strchr(param, '.') < val))
		return 0;

	if (panic_later)
		return 0;

	if (val) {
		/* Environment option */
		unsigned int i;
		for (i = 0; envp_init[i]; i++) {
			if (i == MAX_INIT_ENVS) {
				panic_later = "env";
				panic_param = param;
			}
			if (!strncmp(param, envp_init[i], val - param))
				break;
		}
		envp_init[i] = param;
	} else {
		/* Command line option */
		unsigned int i;
		for (i = 0; argv_init[i]; i++) {
			if (i == MAX_INIT_ARGS) {
				panic_later = "init";
				panic_param = param;
			}
		}
		argv_init[i] = param;
	}
	return 0;
}

static int __init init_setup(char *str)
{
	unsigned int i;

	execute_command = str;
	/*
	 * In case LILO is going to boot us with default command line,
	 * it prepends "auto" before the whole cmdline which makes
	 * the shell think it should execute a script with such name.
	 * So we ignore all arguments entered _before_ init=... [MJ]
	 */
	for (i = 1; i < MAX_INIT_ARGS; i++)
		argv_init[i] = NULL;
	return 1;
}
__setup("init=", init_setup);

static int __init rdinit_setup(char *str)
{
	unsigned int i;

	ramdisk_execute_command = str;
	/* See "auto" comment in init_setup */
	for (i = 1; i < MAX_INIT_ARGS; i++)
		argv_init[i] = NULL;
	return 1;
}
__setup("rdinit=", rdinit_setup);

#ifndef CONFIG_SMP
static const unsigned int setup_max_cpus = NR_CPUS;
static inline void setup_nr_cpu_ids(void) { }
static inline void smp_prepare_cpus(unsigned int maxcpus) { }
#endif

/*
 * We need to store the untouched command line for future reference.
 * We also need to store the touched command line since the parameter
 * parsing is performed in place, and we should allow a component to
 * store reference of name/value for future reference.
 */
static void __init setup_command_line(char *command_line)
{
	saved_command_line =
		memblock_virt_alloc(strlen(boot_command_line) + 1, 0);
	initcall_command_line =
		memblock_virt_alloc(strlen(boot_command_line) + 1, 0);
	static_command_line = memblock_virt_alloc(strlen(command_line) + 1, 0);
	strcpy(saved_command_line, boot_command_line);
	strcpy(static_command_line, command_line);
}

/*
 * We need to finalize in a non-__init function or else race conditions
 * between the root thread and the init thread may cause start_kernel to
 * be reaped by free_initmem before the root thread has proceeded to
 * cpu_idle.
 *
 * gcc-3.4 accidentally inlines this function, so use noinline.
 */

static __initdata DECLARE_COMPLETION(kthreadd_done);

static noinline void __ref rest_init(void)
{
	struct task_struct *tsk;
	int pid;

	pr_info("enter rest_init...\n");
	rcu_scheduler_starting();
	//pr_info("rest_init||rcu_scheduler_starting\n");
	/*
	 * We need to spawn init first so that it obtains pid 1, however
	 * the init task will end up wanting to create kthreads, which, if
	 * we schedule it before we create kthreadd, will OOPS.
	 */
	pid = kernel_thread(kernel_init, NULL, CLONE_FS);
	pr_info("rest_init||kernel_init PID:%d...\n",(int)pid);
	/*
	 * Pin init on the boot CPU. Task migration is not properly working
	 * until sched_init_smp() has been run. It will set the allowed
	 * CPUs for init to the non isolated CPUs.
	 */
	rcu_read_lock();
	//pr_info("rest_init||rcu_read_lock\n");   
	tsk = find_task_by_pid_ns(pid, &init_pid_ns);
	//pr_info("rest_init||tsk:%d\n",tsk);
	set_cpus_allowed_ptr(tsk, cpumask_of(smp_processor_id()));
	//pr_info("rest_init||set_cpus_allowed_ptr\n");
	rcu_read_unlock();
	//pr_info("rest_init||rcu_read_unlock\n");
	numa_default_policy();
	//pr_info("rest_init||numa_default_policy\n");
	pid = kernel_thread(kthreadd, NULL, CLONE_FS | CLONE_FILES);
	pr_info("rest_init||kthreadd PID:%d...\n",(int)pid);
	rcu_read_lock();
	//pr_info("rest_init||rcu_read_lock\n");
	kthreadd_task = find_task_by_pid_ns(pid, &init_pid_ns);
	//pr_info("rest_init||kthreadd_task:%d...\n",(int)kthreadd_task);
	rcu_read_unlock();
	//pr_info("rest_init||cu_read_unlock\n");

	/*
	 * Enable might_sleep() and smp_processor_id() checks.
	 * They cannot be enabled earlier because with CONFIG_PRREMPT=y
	 * kernel_thread() would trigger might_sleep() splats. With
	 * CONFIG_PREEMPT_VOLUNTARY=y the init task might have scheduled
	 * already, but it's stuck on the kthreadd_done completion.
	 */
	system_state = SYSTEM_SCHEDULING;

	complete(&kthreadd_done);
	//pr_info("rest_init||complete\n");

	/*
	 * The boot idle thread must execute schedule()
	 * at least once to get things moving:
	 */
	schedule_preempt_disabled();
	pr_info("rest_init||schedule_preempt_disabled\n");
	/* Call into cpu_idle with preempt disabled */
	cpu_startup_entry(CPUHP_ONLINE);
	pr_info("rest_init|DONE|cpu_startup_entry\n");
}

/* Check for early params. */
static int __init do_early_param(char *param, char *val,
				 const char *unused, void *arg)
{
	const struct obs_kernel_param *p;

	for (p = __setup_start; p < __setup_end; p++) {
		if ((p->early && parameq(param, p->str)) ||
		    (strcmp(param, "console") == 0 &&
		     strcmp(p->str, "earlycon") == 0)
		) {
			if (p->setup_func(val) != 0)
				pr_warn("Malformed early option '%s'\n", param);
		}
	}
	/* We accept everything at this stage. */
	return 0;
}

void __init parse_early_options(char *cmdline)
{
	parse_args("early options", cmdline, NULL, 0, 0, 0, NULL,
		   do_early_param);
}

/* Arch code calls this early on, or if not, just before other parsing. */
void __init parse_early_param(void)
{
	static int done __initdata;
	static char tmp_cmdline[COMMAND_LINE_SIZE] __initdata;

	if (done)
		return;

	/* All fall through to do_early_param. */
	strlcpy(tmp_cmdline, boot_command_line, COMMAND_LINE_SIZE);
	parse_early_options(tmp_cmdline);
	done = 1;
}

void __init __weak arch_post_acpi_subsys_init(void) { }

void __init __weak smp_setup_processor_id(void)
{
}

# if THREAD_SIZE >= PAGE_SIZE
void __init __weak thread_stack_cache_init(void)
{
}
#endif

/*
 * Set up kernel memory allocators
 */
static void __init mm_init(void)
{
	/*
	 * page_ext requires contiguous pages,
	 * bigger than MAX_ORDER unless SPARSEMEM.
	 */
	page_ext_init_flatmem();
	//pr_info("page_ext_init_flatmem\n");
	mem_init();
	//pr_info("mem_init\n");
	kmem_cache_init();
	//pr_info("kmem_cache_init\n");

	
	pgtable_init();
	//pr_info("pgtable_init\n");
	vmalloc_init();
	//pr_info("vmalloc_init\n");
	ioremap_huge_init();
	//pr_info("ioremap_huge_init\n");
	/* Should be run before the first non-init thread is created */
	init_espfix_bsp();
	//pr_info("init_espfix_bsp\n");
	/* Should be run after espfix64 is set up. */
	pti_init();
	//pr_info("pti_init\n");
}

asmlinkage __visible void __init start_kernel(void)
{
	char *command_line;
	char *after_dashes;
	pr_info("!!!!!SOC2018, DEBUG!!!!!\r\n");
	pr_info("!!!!!SOC2018, DEBUG!!!!!\r\n");
	//pr_info("set_task_stack_end_magic start executing.\n");
	set_task_stack_end_magic(&init_task);
	//pr_info("set_task_stack_end_magic success.\n");
	//pr_info("smp_setup_processor_id start executing.\n");
	smp_setup_processor_id();
	//pr_info("smp_setup_processor_id success.\n");
	//pr_info("debug_objects_early_init start executing.\n");
	debug_objects_early_init();
	pr_info("debug_objects_early_init success.\n");
	//pr_info("cgroup_init_early start executing.\n");
	cgroup_init_early();
	pr_info("cgroup_init_early success.\n");
	//pr_info("local_irq_disable start executing.\n");
	local_irq_disable();
	pr_info("local_irq_disable success.\n");
	early_boot_irqs_disabled = true;

	/*
	 * Interrupts are still disabled. Do necessary setups, then
	 * enable them.
	 */
	//pr_info("boot_cpu_init start executing.\n");
	boot_cpu_init();
	pr_info("boot_cpu_init success.\n");
	//pr_info("page_address_init start executing.\n");
	page_address_init();
	pr_info("page_address_init success.\n");
	//pr_info("pr_notice start executing.\n");
	pr_notice("%s", linux_banner);
	//pr_info("pr_notice success.\n");
	setup_arch(&command_line);
	pr_info("setup_arch success.\n");
	//pr_info("command_line:%s",&command_line);
	mm_init_cpumask(&init_mm);
	//pr_info("mm_init_cpumask success.\n");
	setup_command_line(command_line);
	//pr_info("setup_command_line success.\n");
	setup_nr_cpu_ids();
	//pr_info("setup_nr_cpu_ids success.\n");
	setup_per_cpu_areas();
	//pr_info("setup_per_cpu_areas success.\n");
	smp_prepare_boot_cpu();	/* arch-specific boot-cpu hooks */
	//pr_info("smp_prepare_boot_cpu success.\n");
	boot_cpu_hotplug_init();
	//pr_info("boot_cpu_hotplug_init success.\n");

	build_all_zonelists(NULL);
	pr_info("build_all_zonelists success.\n");
	page_alloc_init();
	pr_info("page_alloc_init success.\n");

	pr_notice("Kernel command line: %s\n", boot_command_line);
	/* parameters may set static keys */
	jump_label_init();
	//pr_info("jump_label_init success.\n");
	parse_early_param();
	//pr_info("jump_label_init success.\n");
	after_dashes = parse_args("Booting kernel",
				  static_command_line, __start___param,
				  __stop___param - __start___param,
				  -1, -1, NULL, &unknown_bootoption);
	//pr_info("after_dashes success.\n");
	if (!IS_ERR_OR_NULL(after_dashes))
		parse_args("Setting init args", after_dashes, NULL, 0, -1, -1,
			   NULL, set_init_arg);
	//pr_info("IS_ERR_OR_NULL success.\n");
	/*
	 * These use large bootmem allocations and must precede
	 * kmem_cache_init()
	 */
	setup_log_buf(0);
	//pr_info("setup_log_bufsuccess.\n");
	pidhash_init();
	//pr_info("pidhash_init success.\n");
	vfs_caches_init_early();
	pr_info("vfs_caches_init_early success.\n");
	sort_main_extable();
	//pr_info("sort_main_extable success.\n");
	trap_init();
	//pr_info("trap_init success.\n");//到这里了
	mm_init();
	pr_info("mm_init success.\n");
	ftrace_init();
	//pr_info("ftrace_init success.\n");

	/* trace_printk can be enabled here */
	early_trace_init();
	//pr_info("early_trace_init success.\n");

	/*
	 * Set up the scheduler prior starting any interrupts (such as the
	 * timer interrupt). Full topology setup happens at smp_init()
	 * time - but meanwhile we still have a functioning scheduler.
	 */
	sched_init();
	/*
	 * Disable preemption - early bootup scheduling is extremely
	 * fragile until we cpu_idle() for the first time.
	 */
	preempt_disable();
	//pr_info("preempt_disable success.\n");
	if (WARN(!irqs_disabled(),
		 "Interrupts were enabled *very* early, fixing it\n"))
		local_irq_disable();
	//pr_info("if (WARN(!irqs_disabled(), success.\n");
	radix_tree_init();
	//pr_info("radix_tree_init success.\n");
	/*
	 * Allow workqueue creation and work item queueing/cancelling
	 * early.  Work item execution depends on kthreads and starts after
	 * workqueue_init().
	 */
	workqueue_init_early();
	//pr_info("workqueue_init_earlysuccess.\n");
	rcu_init();
	//pr_info("rcu_init success.\n");
	/* Trace events are available after this */
	trace_init();
	//pr_info("trace_init success.\n");
	context_tracking_init();
	//pr_info("context_tracking_init success.\n");
	/* init some links before init_ISA_irqs() */
	early_irq_init();
	//pr_info("early_irq_init success.\n");
	init_IRQ();
	pr_info("init_IRQ success.\n");
	tick_init();
	pr_info("tick_init success.\n");
	rcu_init_nohz();
	//pr_info("rcu_init_nohz success.\n");
	init_timers();
	pr_info("init_timers success.\n");
	hrtimers_init();
	//pr_info("hrtimers_init success.\n");
	softirq_init();
	//pr_info("softirq_init success.\n");
	timekeeping_init();
	//pr_info("timekeeping_init success.\n");
	time_init();
	pr_info("time_init success.\n");

	/*
	 * For best initial stack canary entropy, prepare it after:
	 * - setup_arch() for any UEFI RNG entropy and boot cmdline access
	 * - timekeeping_init() for ktime entropy used in random_init()
	 * - time_init() for making random_get_entropy() work on some platforms
	 * - random_init() to initialize the RNG from from early entropy sources
	 */
	random_init(command_line);
	//pr_info("random_init success.\n");
	boot_init_stack_canary();
	//pr_info("boot_init_stack_canary success.\n");

	sched_clock_postinit();
	//pr_info("sched_clock_postinit success.\n");
	printk_safe_init();
	pr_info("printk_safe_init success.\n");
	perf_event_init();
	//pr_info("perf_event_init success.\n");
	profile_init();
	//pr_info("profile_init success.\n");
	call_function_init();
	//pr_info("call_function_init success.\n");
	WARN(!irqs_disabled(), "Interrupts were enabled early\n");
	early_boot_irqs_disabled = false;
	local_irq_enable();
	//pr_info("local_irq_enable success.\n");

	kmem_cache_init_late();
	//pr_info("kmem_cache_init_late success.\n");

	/*
	 * HACK ALERT! This is early. We're enabling the console before
	 * we've done PCI setups etc, and console_init() must be aware of
	 * this. But we do want output early, in case something goes wrong.
	 */
	console_init();
	pr_info("console_init success.\n");
	if (panic_later)
		panic("Too many boot %s vars at `%s'", panic_later,
		      panic_param);

	lockdep_info();
	//pr_info("lockdep_info success.\n");

	/*
	 * Need to run this when irqs are enabled, because it wants
	 * to self-test [hard/soft]-irqs on/off lock inversion bugs
	 * too:
	 */
	locking_selftest();
	//pr_info("locking_selftest success.\n");

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start && !initrd_below_start_ok &&
	    page_to_pfn(virt_to_page((void *)initrd_start)) < min_low_pfn) {
		pr_crit("initrd overwritten (0x%08lx < 0x%08lx) - disabling it.\n",
		    page_to_pfn(virt_to_page((void *)initrd_start)),
		    min_low_pfn);
		initrd_start = 0;
	    pr_info("initrd_start  success.\n");
	}
#endif
	kmemleak_init();
	//pr_info("kmemleak_init success.\n");
	debug_objects_mem_init();
	//pr_info("debug_objects_mem_init success.\n");
	setup_per_cpu_pageset();
	//pr_info("setup_per_cpu_pageset success.\n");
	numa_policy_init();
	//pr_info("numa_policy_init success.\n");
	if (late_time_init)
		late_time_init();
	//pr_info("if (late_time_init) success.\n");
	calibrate_delay();
	pr_info("calibrate_delay success.\n");

	arch_cpu_finalize_init();
	//pr_info("arch_cpu_finalize_init success.\n");
	pidmap_init();
	//pr_info("pidmap_init success.\n");
	anon_vma_init();
	//pr_info("anon_vma_init success.\n");
	acpi_early_init();
	//pr_info("acpi_early_init success.\n");
#ifdef CONFIG_X86
	if (efi_enabled(EFI_RUNTIME_SERVICES))
		efi_enter_virtual_mode();
	pr_info("if (efi_enabled(EFI_RUNTIME_SERVICES) success.\n");
#endif
	thread_stack_cache_init();
	//pr_info("thread_stack_cache_init success.\n");
	cred_init();
	//pr_info("cred_init success.\n");
	fork_init();
	pr_info("fork_init success.\n");
	proc_caches_init();
	//pr_info("proc_caches_init success.\n");
	buffer_init();
	//pr_info("abuffer_init success.\n");
	key_init();
	//pr_info("key_init success.\n");
	security_init();
	//pr_info("security_init success.\n");
	dbg_late_init();
	//pr_info("dbg_late_init success.\n");
	vfs_caches_init();
	pr_info("vfs_caches_init success.\n");
	pagecache_init();
	pr_info("pagecache_initsuccess.\n");
	signals_init();
	//pr_info("signals_init success.\n");
	proc_root_init();
	//pr_info("proc_root_initsuccess.\n");
	nsfs_init();
	//pr_info("nsfs_init success.\n");
	cpuset_init();
	//pr_info("cpuset_init success.\n");
	cgroup_init();
	//pr_info("cgroup_init success.\n");
	taskstats_init_early();
	//pr_info("taskstats_init_early success.\n");
	delayacct_init();
	//pr_info("delayacct_init success.\n");
	acpi_subsystem_init();
	//pr_info("acpi_subsystem_init success.\n");
	arch_post_acpi_subsys_init();
	//pr_info("arch_post_acpi_subsys_init success.\n");
	sfi_init_late();
	//pr_info("sfi_init_late success.\n");

	if (efi_enabled(EFI_RUNTIME_SERVICES)) {
		efi_free_boot_services();
		//pr_info("efi_free_boot_services success.\n");
	}

	/* Do the rest non-__init'ed, we're now alive */
	pr_info("rest_init start.....\n");
	rest_init();
	//pr_info("rest_init success.\n");

	prevent_tail_call_optimization();
}

/* Call all constructor functions linked into the kernel. */
static void __init do_ctors(void)
{
#ifdef CONFIG_CONSTRUCTORS
	ctor_fn_t *fn = (ctor_fn_t *) __ctors_start;

	for (; fn < (ctor_fn_t *) __ctors_end; fn++)
		(*fn)();
#endif
}

bool initcall_debug;
core_param(initcall_debug, initcall_debug, bool, 0644);

#ifdef CONFIG_KALLSYMS
struct blacklist_entry {
	struct list_head next;
	char *buf;
};

static __initdata_or_module LIST_HEAD(blacklisted_initcalls);

static int __init initcall_blacklist(char *str)
{

	char *str_entry;
	struct blacklist_entry *entry;

	/* str argument is a comma-separated list of functions */
	do {
		str_entry = strsep(&str, ",");
		if (str_entry) {
			//pr_debug("blacklisting initcall %s\n", str_entry);
			entry = alloc_bootmem(sizeof(*entry));
			entry->buf = alloc_bootmem(strlen(str_entry) + 1);
			strcpy(entry->buf, str_entry);
			list_add(&entry->next, &blacklisted_initcalls);
		}
	} while (str_entry);

	return 1;
}

static bool __init_or_module initcall_blacklisted(initcall_t fn)
{
	//return FALSE;
	struct blacklist_entry *entry;
	char fn_name[KSYM_SYMBOL_LEN];
	unsigned long addr;

	if (list_empty(&blacklisted_initcalls))
		return false;

	addr = (unsigned long) dereference_function_descriptor(fn);
	sprint_symbol_no_offset(fn_name, addr);

	/*
	 * fn will be "function_name [module_name]" where [module_name] is not
	 * displayed for built-in init functions.  Strip off the [module_name].
	 */
	strreplace(fn_name, ' ', '\0');

	list_for_each_entry(entry, &blacklisted_initcalls, next) {
		if (!strcmp(fn_name, entry->buf)) {
			pr_debug("initcall %s blacklisted\n", fn_name);
			return true;
		}
	}

	return false;
}
#else
static int __init initcall_blacklist(char *str)
{
	pr_warn("initcall_blacklist requires CONFIG_KALLSYMS\n");
	return 0;
}

static bool __init_or_module initcall_blacklisted(initcall_t fn)
{
	return false;
}
#endif
__setup("initcall_blacklist=", initcall_blacklist);

static int __init_or_module do_one_initcall_debug(initcall_t fn)
{
	ktime_t calltime, delta, rettime;
	unsigned long long duration;
	int ret;

	printk(KERN_DEBUG "calling  %pF @ %i\n", fn, task_pid_nr(current));
	calltime = ktime_get();
	ret = fn();
	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long) ktime_to_ns(delta) >> 10;
	printk(KERN_DEBUG "initcall %pF returned %d after %lld usecs\n",
		 fn, ret, duration);

	return ret;
}

int __init_or_module do_one_initcall(initcall_t fn)
{
	int count = preempt_count();
	int ret;
	char msgbuf[64];

	if (initcall_blacklisted(fn))
		return -EPERM;

	if (initcall_debug)
		ret = do_one_initcall_debug(fn);
	else
		ret = fn();

	msgbuf[0] = 0;

	if (preempt_count() != count) {
		sprintf(msgbuf, "preemption imbalance ");
		preempt_count_set(count);
	}
	if (irqs_disabled()) {
		strlcat(msgbuf, "disabled interrupts ", sizeof(msgbuf));
		local_irq_enable();
	}
	WARN(msgbuf[0], "initcall %pF returned with %s\n", fn, msgbuf);

	add_latent_entropy();
	return ret;
}


extern initcall_t __initcall_start[];
extern initcall_t __initcall0_start[];
extern initcall_t __initcall1_start[];
extern initcall_t __initcall2_start[];
extern initcall_t __initcall3_start[];
extern initcall_t __initcall4_start[];
extern initcall_t __initcall5_start[];
extern initcall_t __initcall6_start[];
extern initcall_t __initcall7_start[];
extern initcall_t __initcall_end[];

static initcall_t *initcall_levels[] __initdata = {
	__initcall0_start,
	__initcall1_start,
	__initcall2_start,
	__initcall3_start,
	__initcall4_start,
	__initcall5_start,
	__initcall6_start,
	__initcall7_start,
	__initcall_end,
};

/* Keep these in sync with initcalls in include/linux/init.h */
static char *initcall_level_names[] __initdata = {
	"early",
	"core",
	"postcore",
	"arch",
	"subsys",
	"fs",
	"device",
	"late",
};

static void __init do_initcall_level(int level)
{
	initcall_t *fn;

	strcpy(initcall_command_line, saved_command_line);
	parse_args(initcall_level_names[level],
		   initcall_command_line, __start___param,
		   __stop___param - __start___param,
		   level, level,
		   NULL, &repair_env_string);

	for (fn = initcall_levels[level]; fn < initcall_levels[level+1]; fn++)
		do_one_initcall(*fn);
}

static void __init do_initcalls(void)
{
	int level;

	for (level = 0; level < ARRAY_SIZE(initcall_levels) - 1; level++)
		do_initcall_level(level);
}

/*
 * Ok, the machine is now initialized. None of the devices
 * have been touched yet, but the CPU subsystem is up and
 * running, and memory and process management works.
 *
 * Now we can finally start doing some real work..
 */
static void __init do_basic_setup(void)
{
	//unsigned long phy_srliu = 0xA0000100;
	//unsigned long size_srliu = 100;
	//void __iomem *virt_srliu;
	//virt_srliu = ioremap(phy_srliu,size_srliu);
	//pr_info("LIU|virt_srliu1:%p...\r\n", (void *)virt_srliu);
	pr_info("LIU|do_basic_setup...\r\n");
	cpuset_init_smp();
	pr_info("LIU|-->|done|cpuset_init_smp\r\n");
	shmem_init();
	pr_info("LIU|-->|done|shmem_init\r\n");
	//virt_srliu = ioremap(phy_srliu,size_srliu);
	//pr_info("LIU|virt_srliu2:%p...\r\n", (void *)virt_srliu);
	driver_init();
	pr_info("LIU|-->|done|driver_init\r\n");
	//virt_srliu = ioremap(phy_srliu,size_srliu);
	//pr_info("LIU|virt_srliu3:%p...\r\n", (void *)virt_srliu);
	init_irq_proc();
	do_ctors();
	usermodehelper_enable();
	do_initcalls();
	pr_info("LIU|-->|done|do_initcalls\r\n");
}

static void __init do_pre_smp_initcalls(void)
{
	initcall_t *fn;

	for (fn = __initcall_start; fn < __initcall0_start; fn++)
		do_one_initcall(*fn);
}

/*
 * This function requests modules which should be loaded by default and is
 * called twice right after initrd is mounted and right before init is
 * exec'd.  If such modules are on either initrd or rootfs, they will be
 * loaded before control is passed to userland.
 */
void __init load_default_modules(void)
{
	load_default_elevator_module();
}

static int run_init_process(const char *init_filename)
{
	argv_init[0] = init_filename;
	pr_info("run_init_process||argv_init[0]\n");
	return do_execve(getname_kernel(init_filename),
		(const char __user *const __user *)argv_init,
		(const char __user *const __user *)envp_init);
}

static int try_to_run_init_process(const char *init_filename)
{
	int ret;

	ret = run_init_process(init_filename);

	if (ret && ret != -ENOENT) {
		pr_err("Starting init: %s exists but couldn't execute it (error %d)\n",
		       init_filename, ret);
	}

	return ret;
}

static noinline void __init kernel_init_freeable(void);

#if defined(CONFIG_STRICT_KERNEL_RWX) || defined(CONFIG_STRICT_MODULE_RWX)
bool rodata_enabled __ro_after_init = true;
static int __init set_debug_rodata(char *str)
{
	if (strtobool(str, &rodata_enabled))
		pr_warn("Invalid option string for rodata: '%s'\n", str);
	return 1;
}
__setup("rodata=", set_debug_rodata);
#endif

#ifdef CONFIG_STRICT_KERNEL_RWX
static void mark_readonly(void)
{
	if (rodata_enabled) {
		/*
		 * load_module() results in W+X mappings, which are cleaned up
		 * with call_rcu_sched().  Let's make sure that queued work is
		 * flushed so that we don't hit false positives looking for
		 * insecure pages which are W+X.
		 */
		rcu_barrier_sched();
		mark_rodata_ro();
		rodata_test();
	} else
		pr_info("Kernel memory protection disabled.\n");
}
#else
static inline void mark_readonly(void)
{
	pr_warn("This architecture does not have kernel memory protection.\n");
}
#endif

static int __ref kernel_init(void *unused)
{
	int ret;
	pr_info("enter kernel_init...\n");
	kernel_init_freeable();
	pr_info("kernel_init|DONE|kernel_init_freeable\n");
	/* need to finish all async __init code before freeing the memory */
	async_synchronize_full();
	//pr_info("kernel_init||async_synchronize_full\n");
	ftrace_free_init_mem();
	//pr_info("kernel_init||ftrace_free_init_mem\n");
	free_initmem();
	pr_info("kernel_init||free_initmem\n");
	mark_readonly();
	//pr_info("kernel_init||mark_readonly\n");
	system_state = SYSTEM_RUNNING;
	numa_default_policy();
	//pr_info("kernel_init||numa_default_policy\n");
	rcu_end_inkernel_boot();
	//pr_info("kernel_init||rcu_end_inkernel_boot\n");
	// while(1)
	// {
	// 	long long int i;

	// 	for(i=0;i<100000000;i++)
	// 	{
			
	// 	}

	// 	pr_info("kernel_init||while\n");
	// }
	if (ramdisk_execute_command) {
		ret = run_init_process(ramdisk_execute_command);
		pr_info("kernel_init||run_init_process\n");
		if (!ret)
			return 0;
		pr_err("Failed to execute %s (error %d)\n",
		       ramdisk_execute_command, ret);
	}

	/*
	 * We try each of these until one succeeds.
	 *
	 * The Bourne shell can be used instead of init if we are
	 * trying to recover a really broken machine.
	 */
	if (execute_command) {
		ret = run_init_process(execute_command);
		pr_info("kernel_init||second run_init_process\n");
		if (!ret)
			return 0;
		panic("Requested init %s failed (error %d).",
		      execute_command, ret);
	}
	if (!try_to_run_init_process("/sbin/init") ||
	    !try_to_run_init_process("/etc/init") ||
	    !try_to_run_init_process("/bin/init") ||
	    !try_to_run_init_process("/bin/sh")){
		pr_info("kernel_init||return 0\n");
		return 0;}

	panic("No working init found.  Try passing init= option to kernel. "
	      "See Linux Documentation/admin-guide/init.rst for guidance.");
}

static noinline void __init kernel_init_freeable(void)
{
	/*
	 * Wait until kthreadd is all set-up.
	 */
	struct console *con;

	for (con = console_drivers; con != NULL; con = con->next){
		if(con->flags & CON_ENABLED){
			pr_info("111ctive console: %s..., index:%d...\r\n", con->name, con->index);
		}
	}

	pr_info("LIU|kernel_init_freeable...\r\n");
	wait_for_completion(&kthreadd_done);
	//pr_info("kernel_init_freeable||wait_for_completion\n");

	/* Now the scheduler is fully set up and can do blocking allocations */
	gfp_allowed_mask = __GFP_BITS_MASK;

	/*
	 * init can allocate pages on any node
	 */
	set_mems_allowed(node_states[N_MEMORY]);
	pr_info("LIU|->|done|set_mems_allowed\n");

	cad_pid = get_pid(task_pid(current));
	pr_info("LIU|->|done|cad_pid:%p..\n",(void*)cad_pid);

	smp_prepare_cpus(setup_max_cpus);
	//pr_info("LIU|->|done|smp_prepare_cpus\n");

	workqueue_init();
	//pr_info("LIU|->|done|workqueue_init\n");


	init_mm_internals();
	//pr_info("LIU|->|done|init_mm_internals\n");

	do_pre_smp_initcalls();
	//pr_info("LIU|->|done|do_pre_smp_initcalls\n");
	lockup_detector_init();
	pr_info("LIU|->|done|lockup_detector_init\n");

	smp_init();
	pr_info("LIU|->|done|smp_init\n");
	sched_init_smp();
	pr_info("LIU|->|done|sched_init_smp\n");

	page_alloc_init_late();
	pr_info("LIU|->|done|page_alloc_init_late\n");
	/* Initialize page ext after all struct pages are initialized. */
	page_ext_init();
	pr_info("LIU|->|done|page_ext_init\n");
	do_basic_setup();
	pr_info("LIU|->|done|do_basic_setup\n");
	pr_info("LIU|->|done|onlymark.....\n");
	/* Open the /dev/console on the rootfs, this should never fail */

	//show_mem(0, NULL);

	for (con = console_drivers; con != NULL; con = con->next){
		if(con->flags & CON_ENABLED){
			pr_info("Main console:%s%s..\r\n", con->name, (con->flags & CON_CONSDEV)?"primary":"");
			pr_info("222Active console: %s..., index:%d...\r\n", con->name, con->index);
			pr_info("con:%p...\r\n", (void*)con);
			con->write(con, "test!\r\n", 7);
		}
	}
	

	int fd = sys_open((const char __user *) "/dev/console", O_RDWR, 0);
	pr_info("LIU|test fd:%d..\n", fd);
	if (fd < 0) {
		pr_err("Warning: unable to open an initial console.\n");
	} else {
		sys_write(fd, (const char __user *)"Hello, console!\n", 16);
	}

	// int fdliu = sys_open((const char __user *) "/dev/console", O_RDWR, 0);
	// if (fdliu < 0){
	// 	pr_err("Warning: unable to open an initial console.\n");
	// }
	// char *msg = "Hello console!\n";
	// pr_info("try sys_write....\n");
	// sys_write(fdliu, msg, strlen(msg));
	// int fdttys0 = sys_open((const char __user *) "/dev/ttyS0", O_RDWR, 0);
	// if (fdttys0 < 0) {
	// 	pr_info("cannot open ttyS0....\n");
	// }else{
	// 	pr_info("ttyS0 opended....\n");
	// 	sys_write(fdttys0, msg, strlen(msg));
	// }

	// if (sys_open((const char __user *) "/dev/console", O_RDWR, 0) < 0){
	// 	pr_err("Warning: unable to open an initial console.\n");
	// }

	//pr_info("sys_dup 1\n");
	int fd_out;
	fd_out = sys_dup(0);
	//(void) sys_dup(0);
	pr_info("LIU|test fd_out:%d..\n", fd_out);
	sys_write(fd_out, (const char __user *)"Hello, console!\n", 16);
	//pr_info("sys_dup 1 over");
	(void) sys_dup(0);
	//pr_info("sys_dup2 over\n");
	/*
	 * check if there is an early userspace init.  If yes, ledt it do all
	 * the work
	 */





	if (!ramdisk_execute_command){
		pr_info("ramdisk_execute_command = /init ");
		ramdisk_execute_command = "/init";}

	if (sys_access((const char __user *) ramdisk_execute_command, 0) != 0) {
		//pr_info("ramdisk_execute_command = NULL in");
		ramdisk_execute_command = NULL;
		pr_info("LIU|->done|prepare_namespace\n");
		prepare_namespace();
	}

	/*
	 * Ok, we have completed the initial bootup, and
	 * we're essentially up and running. Get rid of the
	 * initmem segments and start the user-mode stuff..
	 *
	 * rootfs is available now, try loading the public keys
	 * and default modules
	 */
    //pr_info("integrity_load_keys start");
	integrity_load_keys();
	//pr_info("kernel_init_freeable\n");
	load_default_modules();
	pr_info("LIU|->|done|load_default_modules\n");
}
