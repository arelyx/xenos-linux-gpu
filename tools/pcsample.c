/* pcsample PID SECONDS: sample every thread's program counter every 10 ms
 * with ptrace, print "lib offset" per sample (lib from /proc/PID/maps).
 * A flat profile for a kernel without perf events. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <asm/ptrace.h>

struct map { unsigned long lo, hi, off; char name[160]; };
static struct map maps[2048]; static int nmaps;

static void load_maps(int pid)
{
	char p[64], line[512];
	snprintf(p, sizeof(p), "/proc/%d/maps", pid);
	FILE *f = fopen(p, "r");
	nmaps = 0;
	while (f && fgets(line, sizeof(line), f) && nmaps < 2048) {
		struct map *m = &maps[nmaps];
		char perms[8]; m->name[0] = 0;
		if (sscanf(line, "%lx-%lx %7s %lx %*s %*s %159s", &m->lo, &m->hi, perms, &m->off, m->name) >= 4 &&
		    perms[2] == 'x')
			nmaps++;
	}
	if (f) fclose(f);
}

int main(int argc, char **argv)
{
	int pid = atoi(argv[1]), secs = argc > 2 ? atoi(argv[2]) : 10;
	load_maps(pid);
	struct timespec t0, now; clock_gettime(CLOCK_MONOTONIC, &t0);
	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (now.tv_sec - t0.tv_sec >= secs) break;
		char td[64]; snprintf(td, sizeof(td), "/proc/%d/task", pid);
		DIR *d = opendir(td); if (!d) break;
		struct dirent *e;
		while ((e = readdir(d))) {
			int tid = atoi(e->d_name); if (tid <= 0) continue;
			if (ptrace(PTRACE_SEIZE, tid, 0, 0)) continue;
			if (ptrace(PTRACE_INTERRUPT, tid, 0, 0)) { ptrace(PTRACE_DETACH, tid, 0, 0); continue; }
			int st; waitpid(tid, &st, __WALL);
			struct pt_regs r;
			if (ptrace(PTRACE_GETREGS, tid, 0, &r) == 0) {
				unsigned long pc = r.nip;
				const char *name = "?"; unsigned long off = pc;
				for (int i = 0; i < nmaps; i++)
					if (pc >= maps[i].lo && pc < maps[i].hi) { name = maps[i].name; off = pc - maps[i].lo + maps[i].off; break; }
				printf("%d %s 0x%lx\n", tid == pid, name, off);
			}
			ptrace(PTRACE_DETACH, tid, 0, 0);
		}
		closedir(d);
		usleep(10000);
	}
	return 0;
}
