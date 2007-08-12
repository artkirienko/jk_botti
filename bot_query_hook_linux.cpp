//
// JK_Botti - be more human!
//
// bot_query_hook_linux.cpp
//

#ifndef _WIN32

#ifndef __USE_GNU
#define __USE_GNU
#endif

#include "asm_string.h"

#include <sys/mman.h>
#include <asm/page.h>
#define PAGE_ALIGN(addr) (((addr)+PAGE_SIZE-1)&PAGE_MASK)
#include <pthread.h>

#include <memory.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "bot_query_hook.h"

//
// Linux work, based on my "Linux code for dynamic linkents" from metamod-p
//

// 0xe9 is opcode for our function forwarder
#define JMP_SIZE 1

//pointer size on x86-32: 4 bytes
#define PTR_SIZE sizeof(void*)

//constructs new jmp forwarder
#define construct_jmp_instruction(x, place, target) { \
	((unsigned char *)(x))[0] = 0xe9; \
	*(unsigned long *)((char *)(x) + 1) = ((unsigned long)(target)) - (((unsigned long)(place)) + 5); \
}
	
//opcode + sizeof pointer
#define BYTES_SIZE (JMP_SIZE + PTR_SIZE)

typedef ssize_t (*sendto_func)(int socket, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);

static bool is_sendto_hook_setup = false;

//pointer to original sendto
static sendto_func sendto_original;

//contains jmp to replacement_sendto @sendto_original
static unsigned char sendto_new_bytes[BYTES_SIZE];

//contains original bytes of sendto
static unsigned char sendto_old_bytes[BYTES_SIZE];

//Mutex for our protection
static pthread_mutex_t mutex_replacement_sendto = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

//restores old sendto
inline void restore_original_sendto(void)
{
	//Copy old sendto bytes back
	memcpy((void*)sendto_original, sendto_old_bytes, BYTES_SIZE);
}

//resets new sendto
inline void reset_sendto_hook(void)
{
	//Copy new sendto bytes back
	memcpy((void*)sendto_original, sendto_new_bytes, BYTES_SIZE);
}

// Replacement sendto function
static ssize_t __replacement_sendto(int socket, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len)
{
	static int is_original_restored = 0;
	int was_original_restored = is_original_restored;
	
	//Lock before modifing original sendto
	pthread_mutex_lock(&mutex_replacement_sendto);
	
	//restore old sendto
	if(!is_original_restored)
	{
		restore_original_sendto();
		
		is_original_restored = 1;
	}
	
	ssize_t ret = sendto_hook(socket, message, length, flags, dest_addr, dest_len);
	
	//reset hook
	if(!was_original_restored)
	{
		//reset sendto hook
		reset_sendto_hook();
		
		is_original_restored = 0;
	}
	
	//unlock
	pthread_mutex_unlock(&mutex_replacement_sendto);
	
	return(ret);
}

//
ssize_t call_original_sendto(int socket, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len)
{
	return (*sendto_original)(socket, message, length, flags, dest_addr, dest_len);
}

//
bool hook_sendto_function(void)
{
	if(is_sendto_hook_setup)
		return(true);
	
	is_sendto_hook_setup = false;
	
	//metamod-p p31 parses elf structures, we find function easier&better way:
	void * sym_ptr = (void*)&sendto;
	while(*(unsigned short*)sym_ptr == 0x25ff) {
		sym_ptr = **(void***)((char *)sym_ptr + 2);
	}
	
	sendto_original = (sendto_func)sym_ptr;
	
	//Backup old bytes of "sendto" function
	memcpy(sendto_old_bytes, (void*)sendto_original, BYTES_SIZE);
	
	//Construct new bytes: "jmp offset[replacement_sendto] @ sendto_original"
	construct_jmp_instruction((void*)&sendto_new_bytes[0], (void*)sendto_original, (void*)&__replacement_sendto);
	
	//Check if bytes overlap page border.	
	unsigned long start_of_page = PAGE_ALIGN((long)sendto_original) - PAGE_SIZE;
	unsigned long size_of_pages = 0;
	
	if((unsigned long)sendto_original + BYTES_SIZE > PAGE_ALIGN((unsigned long)sendto_original))
	{
		//bytes are located on two pages
		size_of_pages = PAGE_SIZE*2;
	}
	else
	{
		//bytes are located entirely on one page.
		size_of_pages = PAGE_SIZE;
	}
	
	//Remove PROT_READ restriction
	if(mprotect((void*)start_of_page, size_of_pages, PROT_READ|PROT_WRITE|PROT_EXEC))
	{
		UTIL_ConsolePrintf("Couldn't initialize sendto hook, mprotect failed: %i.  Exiting...\n", errno);
		return(false);
	}
	
	//Write our own jmp-forwarder on "sendto"
	reset_sendto_hook();
	
	is_sendto_hook_setup = true;
	
	//done
	return(true);
}

//
bool unhook_sendto_function(void)
{
	if(!is_sendto_hook_setup)
		return(true);
	
	//Lock before modifing original sendto
	pthread_mutex_lock(&mutex_replacement_sendto);
	
	//reset sendto hook
	restore_original_sendto();
	
	//unlock
	pthread_mutex_unlock(&mutex_replacement_sendto);
	
	is_sendto_hook_setup = false;
	
	return(true);
}

#endif /*!_WIN32*/