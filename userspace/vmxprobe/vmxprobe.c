/* Which standard AltiVec instructions does the Xenon's VMX128 unit execute? Each runs in a child. */
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#define T(name, insn) { name, #insn }
static void run(const char *name, void (*f)(void))
{
	pid_t p = fork();
	if (!p) { f(); _exit(0); }
	int st; waitpid(p, &st, 0);
	printf("%-12s %s\n", name, WIFSIGNALED(st) ? (WTERMSIG(st) == 4 ? "SIGILL (missing)" : "signal") : "ok");
}
#define F(n, s) static void n(void) { __asm__ volatile(s ::: "v0","v1","v2","v3"); }
F(f_vaddfp,   "vaddfp 0,1,2")
F(f_vperm,    "vperm 0,1,2,3")
F(f_vmaddfp,  "vmaddfp 0,1,2,3")
F(f_vmsumubm, "vmsumubm 0,1,2,3")
F(f_vmsumshm, "vmsumshm 0,1,2,3")
F(f_vmsumuhm, "vmsumuhm 0,1,2,3")
F(f_vmhaddshs,"vmhaddshs 0,1,2,3")
F(f_vmladduhm,"vmladduhm 0,1,2,3")
F(f_vmuleub,  "vmuleub 0,1,2")
F(f_vmulouh,  "vmulouh 0,1,2")
F(f_vsum4ubs, "vsum4ubs 0,1,2")
F(f_vavgub,   "vavgub 0,1,2")
F(f_vpkuhus,  "vpkuhus 0,1,2")
F(f_vmrghb,   "vmrghb 0,1,2")
F(f_lvx,      "li 3,0; lvx 0,1,3")
F(f_mfvscr,   "mfvscr 0")
int main(void)
{
	run("vaddfp", f_vaddfp); run("vperm", f_vperm); run("vmaddfp", f_vmaddfp);
	run("vmsumubm", f_vmsumubm); run("vmsumshm", f_vmsumshm); run("vmsumuhm", f_vmsumuhm);
	run("vmhaddshs", f_vmhaddshs); run("vmladduhm", f_vmladduhm); run("vmuleub", f_vmuleub);
	run("vmulouh", f_vmulouh); run("vsum4ubs", f_vsum4ubs); run("vavgub", f_vavgub);
	run("vpkuhus", f_vpkuhus); run("vmrghb", f_vmrghb); run("lvx", f_lvx); run("mfvscr", f_mfvscr);
	return 0;
}
