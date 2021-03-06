//Created by Skrilax_CZ (skrilax@gmail.com),
//based on work done by Pradeep Padala (p_padala@yahoo.com)

//enhanced by Epsylon3 for XDA CyanogenDefy

#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/user.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <sys/linux-syscalls.h>

#define INIT_COMPAT 179
#define VERSION "v1.0.0-179"

union u
{
	long val;
	char chars[sizeof(long)];
};

void getdata(pid_t child, long addr, char *str, int len)
{
	char *laddr;
	int i, j;
	union u data;

	i = 0;
	j = len / sizeof(long);
	laddr = str;
	while(i < j)
	{
		data.val = ptrace(PTRACE_PEEKDATA, child, (void*)(addr + i * 4), NULL);
		memcpy(laddr, data.chars, sizeof(long));
		++i;
		laddr += sizeof(long);
	}

	j = len % sizeof(long);

	if(j != 0)
	{
		data.val = ptrace(PTRACE_PEEKDATA, child, (void*)(addr + i * 4), NULL);
		memcpy(laddr, data.chars, j);
	}

	str[len] = '\0';
}

void putdata(pid_t child, long addr, char *str, int len)
{
	char *laddr;
	int i, j;
	union u data;

	i = 0;
	j = len / sizeof(long);
	laddr = str;
	while(i < j)
	{
		memcpy(data.chars, laddr, sizeof(long));
		ptrace(PTRACE_POKEDATA, child, (void*)(addr + i * 4), (void*)(data.val));
		++i;
		laddr += sizeof(long);
	}

	j = len % sizeof(long);
	if(j != 0)
	{
		memcpy(data.chars, laddr, j);
		ptrace(PTRACE_POKEDATA, child, (void*)(addr + i * 4), (void*)(data.val));
	}
}

long get_free_address(pid_t pid)
{
	FILE *fp;
	char filename[30];
	char line[85];
	long addr;
	char str[20];
	sprintf(filename, "/proc/%d/maps", pid);
	fp = fopen(filename, "r");

	if(fp == NULL)
		exit(1);

	while(fgets(line, 85, fp) != NULL)
	{
		sscanf(line, "%lx-%*8x %*s %*s %s", &addr, str);
		if(strcmp(str, "00:00") == 0)
			break;
	}

	fclose(fp);
	return addr;
}

void get_base_image_address(pid_t pid, long* address, long* size)
{
	FILE *fp;
	char filename[30];
	char line[85];
	char str[20];
	
	*address = 0;
	*size = 0;

	long start_address = 0;
	long end_address = 0;
	
	sprintf(filename, "/proc/%d/maps", pid);
	fp = fopen(filename, "r");
	
	if(fp == NULL)
		exit(1);

	if(fgets(line, 85, fp) != NULL)
	{
		sscanf(line, "%lx-%lx %*s %*s %s", &start_address, &end_address, str);
		*address = start_address;
		*size = end_address - start_address;
	}

	fclose(fp);
}

long find_syscall(char* data, long data_size, long NR_syscall)
{
	long addr = 0;
	char NR = NR_syscall - __NR_SYSCALL_BASE;

	char execve_code[] = {
		0x90, 0x00, 0x2D, 0xE9, //STMFD  SP!, {R4,R7}
		0x0B, 0x70, 0xA0, 0xE3, //MOV    R7,  #0x0B (__NR_execve) sys/linux-syscalls.h
		0x00, 0x00, 0x00, 0xEF, //SVC    0
		0x90, 0x00, 0xBD, 0xE8  //LDMFD  SP!, {R4,R7}
	};
	
	execve_code[4] = NR;
	
	long d, c = 0;

	//now look for the instructions
	while (c < data_size - (long) sizeof(execve_code))
	{
		int found = 1;

		for(d = 0; d < (long) sizeof(execve_code); d++)
		{
			if (data[c+d] != execve_code[d])
			{
				found = 0;
				break;
			}
		}

		if (found)
		{
			addr = c;
			break;
		}

		c+=4; //ARM aligned mode
	}

	return addr;
}

int main(int argc, char** argv)
{
	struct pt_regs regs;
	char buff[512];
	char init_env[0x1C0];

        printf("2nd-init " VERSION "\n");
        printf(" by CyanogenDefy team, 2011\n");

	//read the enviromental variables of the init
	FILE* f = fopen("/proc/1/environ", "r");

	if (f == 0)
	{
		printf("Couldn't read /init enviromental variables.\n");
		return 2;
	}

	size_t sz = fread(init_env, 1, 0x1C0-1, f);
	init_env[sz] = 0;
	fclose(f);

	//init has pid always 1
	memset(&regs, 0, sizeof(regs));
	if (ptrace(PTRACE_ATTACH, 1, NULL, NULL))
	{
		printf("ERROR: Couldn't attach to /init.\n");
		//return 1;
	}

	//wait for interrupt
	wait(NULL);

	ptrace(PTRACE_GETREGS, 1, NULL, &regs);

	//check if PC is valid
	if (regs.ARM_pc == 0)
	{
		printf("ERROR: Could not get PC register value.\n");
		//return 1;
	}

	printf("/init PC is on: 0x%08lX.\n", regs.ARM_pc);

	//structure of init is (static executable!)
	//0x8000 image base (usually)
	//0xA0 ELF header size
	//=>start is on 0x80A0
	//ARM mode

	long free_address = get_free_address(1);
	printf("Address free for the injection: 0x%08lX.\n", free_address);

	//nah the space on heap will be bigger
	char injected_code[0x400];
	memset(injected_code, 0, sizeof(injected_code));

	//supposed to call
	//execve("/init", { "/init", NULL }, envp);

	//find execve inside init
	//===============================================================================
	long image_base;
	long image_size;
	get_base_image_address(1, &image_base, &image_size);

	if (image_base == 0 || image_size == 0)
	{
		printf("ERROR: Couldn't get the image base of /init.\n");
		printf("Detaching...\n");
		ptrace(PTRACE_DETACH, 1, NULL, NULL);
		return 1;
	}

	printf("image_base: 0x%08lX.\n", image_base);
	printf("image_size: 0x%08lX.\n", image_size);

	char* init_image = malloc(image_size+1);
	if (init_image == NULL) {
		printf("Unable to alloc buffer...\n");
		return 1;
	}
	getdata(1, image_base, init_image, image_size);


	long execve_address = find_syscall(init_image, image_size, __NR_execve);  // 0x4FA0 in 177 /init file = va 0xCFA0

	if (!execve_address)
	{
		printf("ERROR: Failed locating execve.\n");
		printf("Detaching...\n");
		ptrace(PTRACE_DETACH, 1, NULL, NULL);
		return 5;
	}
	execve_address += image_base;

	printf("execve located on: 0x%08lX.\n", execve_address);

// 3 different versions, for 3 different verison of /init (present in boot.smg)
#if (INIT_COMPAT == 177)
// for NORDIC 177-5 (also compatible with RTGB 343-11)
	long injected_code_address = execve_address + 0x1000; //BLX sub_wait4 ? : we need to find a reference
#endif
#if (INIT_COMPAT == 179)
// for CEE 179-2, execve = CFA0
//	long injected_code_address = execve_address + 0x0FE0; //BLX @ DF80
// for SEA 36-17, execve = CFA0 (also compatible with 179-2)
	long injected_code_address = execve_address + 0x0FE0 - 20; //start of bloc (BLX @ DF80)
#endif
#if (INIT_COMPAT == 234)
// for 234-134, execve = 81A0, injection at 0x1089C
	long injected_code_address = execve_address + 0x86FC;
#endif

	printf("Address for the injection: 0x%08lX.\n", injected_code_address);

//	long nr_wait4 = find_syscall(init_image, image_size, __NR_wait4);
//	nr_wait4 += image_base;
//	printf("Address of wait4 syscall: 0x%08lX.\n", nr_wait4);

	long * opcode = (long *)injected_code_address;
	printf("Opcode at injection Address : %08lX.\n", *opcode);

	//fill in the instructions
	//===============================================================================

	/* LDR R0="/init"
	 * LDR R1, &args #(args = { "/init", NULL })
	 * LDR R2, &env #(read em first using /proc)
	 * BL execve #if there is just branch and it fails, then I dunno what happens next, so branch with link
	 * some illegal instruction here (let's say zeroes :D)
	 */

	//this could be set directly to the current registers
	//but I find cleaner just using the code

	//LDR R0=PC-8+0x100 (HEX=0xE59F00F8); (pointer to "/init")
	//LDR R1=PC-8+0x108 (HEX=0xE59F10FC); (pointer to { "/init", NULL })
	//LDR R2=PC-8+0x120 (HEX=0xE59F2110); (pointer to env variables (char**) )
	//BL execve (HEX=0xEB000000 + ((#execve-PC)/4 & 0x00FFFFFF) )

	//on offset 0x100 create the pointers
	long instructions[4];
	instructions[0] = 0xE59F00F8;
	instructions[1] = 0xE59F10FC;
	instructions[2] = 0xE59F2110;
	instructions[3] = 0xEB000000 +
		( ((execve_address - (injected_code_address + 0x0C + 8) )/4) & 0x00FFFFFF );

	//copy them
	memcpy((void*)injected_code, &instructions[0], sizeof(long) * 4);

	//fill in the pointers
	//===============================================================================

	//map
	//0x100 - char* - argument filename and argp[0] - pointer to "/init" on 0x200
	//0x104 - null pointer - argp[1] (set by memset)
	//0x108 - char** - argument argp - pointer to the pointers on 0x100
	//0x120 - char** - argument envp - pointer to the pointers on 0x130
	//0x130 - envp[0] -
	//0x134 - envp[1]
	//0x138 - envp[2]
	//etc.

	//write the arguments
	long execve_arg_filename_target = injected_code_address + 0x200;
	long execve_arg_argp_target = injected_code_address + 0x100;
	long execve_arg_envp_target = injected_code_address + 0x130;

	memcpy(&(injected_code[0x100]), &execve_arg_filename_target, sizeof(long));
	memcpy(&(injected_code[0x108]), &execve_arg_argp_target, sizeof(long));
	memcpy(&(injected_code[0x120]), &execve_arg_envp_target, sizeof(long));

	//fill in the strings and envp
	//===============================================================================

	//"/init" goes to 0x200
	strcpy(&(injected_code[0x200]), "/init");

	//enviroment variables
	long current_envp_address_incr = 0x130;
	char* iter = init_env;
	int w = 0x220;

	while (*iter)
	{
		printf(" env %x: %s\n", w, iter);

		//an env. var is found, write its address and copy it
		long current_envp_string_address = injected_code_address + w;

		memcpy(&(injected_code[current_envp_address_incr]), &current_envp_string_address, sizeof(long));
		current_envp_address_incr += sizeof(long);
		strcpy(&(injected_code[w]), iter);
		int len = strlen(iter) + 1;
		iter += len;
		w += len;

		while (w%(sizeof(long)))
			w++;
	}

	//terminating null pointer is preset by memset

	//put the data
	putdata(1, injected_code_address, injected_code, 1024);
	printf("Jump to 0x%08lX.\n", injected_code_address);

	//set the PC
	regs.ARM_pc = injected_code_address;
	printf("Setting /init PC to: 0x%08lX.\n", injected_code_address);
	ptrace(PTRACE_SETREGS, 1, NULL, &regs);

	//fire it
	printf("Detaching...\n");
	//let some time to print output to console
	usleep(10000);
	ptrace(PTRACE_DETACH, 1, NULL, NULL);

	return 0;
}
