/*
 * Scheduler timeline capture through the Zephyr tracing user hooks
 *
 * "fg trace start" arms a one-shot capture of context switches and
 * interrupts into RAM; "fg trace dump" prints it as CSV lines
 * (TR,<us>,<event>,<name>) that scripts/trace2perfetto.py turns into a
 * Chrome/Perfetto timeline. Hooks run in scheduler and ISR context, so
 * recording is a single atomic slot reservation and a few stores.
 */
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

enum trace_ev {
	EV_IN = 'I',    /* thread switched in */
	EV_OUT = 'O',   /* thread switched out */
	EV_ISR = 'E',   /* interrupt entry */
	EV_ISR_X = 'X', /* interrupt exit */
};

struct trace_rec {
	uint32_t cyc;
	const struct k_thread *thread;
	uint8_t ev;
};

static struct trace_rec buf[CONFIG_FG_TRACE_EVENTS];
static atomic_t head;
static atomic_t armed;
static uint32_t start_cyc;
static uint32_t isr_enter_cyc;
static uint32_t isr_max_cyc;

/*
 * The hooks also fire before the scheduler starts, where k_current_get()
 * is not allowed, so the current thread is read only once a capture is
 * armed from the shell.
 */
static inline void record(uint8_t ev, bool with_thread)
{
	if (!atomic_get(&armed)) {
		return;
	}
	atomic_val_t i = atomic_inc(&head);

	if (i >= CONFIG_FG_TRACE_EVENTS) {
		atomic_clear(&armed);
		return;
	}
	buf[i] = (struct trace_rec){
		.cyc = k_cycle_get_32(),
		.thread = with_thread ? k_current_get() : NULL,
		.ev = ev,
	};
}

void sys_trace_thread_switched_in_user(void)
{
	record(EV_IN, true);
}

void sys_trace_thread_switched_out_user(void)
{
	record(EV_OUT, true);
}

void sys_trace_isr_enter_user(void)
{
	isr_enter_cyc = k_cycle_get_32();
	record(EV_ISR, false);
}

void sys_trace_isr_exit_user(void)
{
	uint32_t d = k_cycle_get_32() - isr_enter_cyc;

	if (d > isr_max_cyc) {
		isr_max_cyc = d;
	}
	record(EV_ISR_X, false);
}

static int cmd_start(const struct shell *sh, size_t argc, char **argv)
{
	atomic_clear(&armed);
	atomic_set(&head, 0);
	isr_max_cyc = 0;
	start_cyc = k_cycle_get_32();
	atomic_set(&armed, 1);
	shell_print(sh, "capturing up to %d events", CONFIG_FG_TRACE_EVENTS);
	return 0;
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv)
{
	atomic_clear(&armed);
	shell_print(sh, "%d events captured, longest ISR %u us",
		    (int)MIN(atomic_get(&head), CONFIG_FG_TRACE_EVENTS),
		    k_cyc_to_us_ceil32(isr_max_cyc));
	return 0;
}

static int cmd_dump(const struct shell *sh, size_t argc, char **argv)
{
	int n = MIN(atomic_get(&head), CONFIG_FG_TRACE_EVENTS);

	atomic_clear(&armed);
	shell_print(sh, "TR,us,event,name");
	for (int i = 0; i < n; i++) {
		const struct trace_rec *r = &buf[i];
		const char *name = "isr";

		if (r->thread) {
			name = k_thread_name_get((struct k_thread *)r->thread);
			if (!name || !name[0]) {
				name = "?";
			}
		}
		shell_print(sh, "TR,%u,%c,%s", k_cyc_to_us_floor32(r->cyc - start_cyc), r->ev,
			    name);
	}
	shell_print(sh, "TR,end,%d", n);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(trace_cmds,
	SHELL_CMD(start, NULL, "Start a one-shot capture", cmd_start),
	SHELL_CMD(stop, NULL, "Stop the capture", cmd_stop),
	SHELL_CMD(dump, NULL, "Print the capture as CSV", cmd_dump),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((fg), trace, &trace_cmds, "Scheduler timeline (Zephyr tracing)", NULL, 1, 0);
