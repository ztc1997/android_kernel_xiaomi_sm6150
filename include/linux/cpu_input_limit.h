/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018-2019 Sultan Alsawaf <sultan@kerneltoast.com>.
 */
#ifndef _CPU_INPUT_LIMIT_H_
#define _CPU_INPUT_LIMIT_H_

#ifdef CONFIG_CPU_INPUT_LIMIT
void cpu_input_limit_kick(void);
#else
static inline void cpu_input_limit_kick(void)
{
}
#endif

#endif /* _CPU_INPUT_LIMIT_H_ */
