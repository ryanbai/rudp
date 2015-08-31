/**
 * @file
 * Dynamic pool memory manager
 *
 * lwIP has dedicated pools for many structures (netconn, protocol control blocks,
 * packet buffers, ...). All these pools are managed here.
 */

/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 */
#include "opt.h"
#include "memp.h"
#include "pbuf.h"
#include "tcp_impl.h"
#include "sys.h"
#include "stats.h"
#include <string.h>

struct memp {
  struct memp *next;
};

/** This array holds the first free element of each pool.
 *  Elements form a linked list. */
static struct memp *memp_tab[MEMP_MAX];

static const u16_t memp_sizes[MEMP_MAX] = {
	LWIP_MEM_ALIGN_SIZE(sizeof(struct tcp_pcb)),
	LWIP_MEM_ALIGN_SIZE(sizeof(struct tcp_pcb_listen)),
	LWIP_MEM_ALIGN_SIZE(sizeof(struct tcp_seg)),
	LWIP_MEM_ALIGN_SIZE(sizeof(struct pbuf)),
	LWIP_MEM_ALIGN_SIZE(sizeof(struct pbuf) + PBUF_POOL_BUFSIZE),
};

/** This array holds the number of elements in each pool. */
static const u16_t memp_num[MEMP_MAX] = {
		MEMP_NUM_TCP_PCB,
		MEMP_NUM_TCP_PCB_LISTEN,
		MEMP_NUM_TCP_SEG,
		MEMP_NUM_PBUF,
		PBUF_POOL_SIZE,
};

//
static u8_t *memp_memory = NULL;

/**
 * Initialize this module.
 * 
 * Carves out memp_memory into linked lists for each pool-type.
 */
void
memp_init(void)
{
	// 统计数据
	u16_t i = 0;
	for (i = 0; i < MEMP_MAX; ++i) {
		MEMP_STATS_AVAIL(used, i, 0);
		MEMP_STATS_AVAIL(max, i, 0);
		MEMP_STATS_AVAIL(err, i, 0);
		MEMP_STATS_AVAIL(avail, i, memp_num[i]);
	}

	// 分配空间
	static u32_t mem_length = MEM_ALIGNMENT - 1;
	for (i = 0; i < MEMP_MAX; ++i)
	{
		mem_length += memp_num[i] * memp_sizes[i];
	}
	memp_memory = malloc(mem_length);

	// 空间挂载到memp_tab对应的slot里
	struct memp *memp = (struct memp *)LWIP_MEM_ALIGN(memp_memory);
	/* for every pool: */
	for (i = 0; i < MEMP_MAX; ++i) {
		memp_tab[i] = NULL;
		/* create a linked list of memp elements */
		u16_t j = 0;
		for (j = 0; j < memp_num[i]; ++j) {
			memp->next = memp_tab[i];
			memp_tab[i] = memp;
			memp = (struct memp *)(void *)((u8_t *)memp + memp_sizes[i]);
		}
	}
}

/**
 * Get an element from a specific pool.
 *
 * @param type the pool to get an element from
 *
 * the debug version has two more parameters:
 * @param file file name calling this function
 * @param line number of line where this function is called
 *
 * @return a pointer to the allocated memory or a NULL pointer on error
 */
void * memp_malloc(memp_t type)
{
	if (type >= MEMP_MAX)
	{
		return NULL;
	}

	struct memp *memp = memp_tab[type];
	if (memp != NULL) {
		memp_tab[type] = memp->next;
		MEMP_STATS_INC_USED(used, type);
		LWIP_ASSERT("memp_malloc: memp properly aligned",
                ((mem_ptr_t)memp % MEM_ALIGNMENT) == 0);
		memp = (struct memp*)(void *)((u8_t*)memp);
	} else {
		LWIP_DEBUGF(MEMP_DEBUG | LWIP_DBG_LEVEL_SERIOUS, ("memp_malloc: out of memory in pool %s\n", memp_desc[type]));
		MEMP_STATS_INC(err, type);
	}

	return memp;
}

/**
 * Put an element back into its pool.
 *
 * @param type the pool where to put mem
 * @param mem the memp element to free
 */
void
memp_free(memp_t type, void *mem)
{
	if (mem == NULL) {
		return;
	}

	LWIP_ASSERT("memp_free: mem properly aligned",
                ((mem_ptr_t)mem % MEM_ALIGNMENT) == 0);

	struct memp *memp = (struct memp *)(void *)((u8_t*)mem);
	memp->next = memp_tab[type];
	memp_tab[type] = memp;

	MEMP_STATS_DEC(used, type);
}

/**
 * clean memory when finish
 */
void memp_fini(void)
{
	if (memp_memory)
	{
		free (memp_memory);
		memp_memory = NULL;
	}
}
