/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef _IOSS_VERSION_H_
#define _IOSS_VERSION_H_

#define IOSS_VER_MAJOR 1
#define IOSS_VER_MINOR 0
#define IOSS_VER_PATCH 0

#define IOSS_VER ( \
			(IOSS_VER_MAJOR << 24) | \
			(IOSS_VER_MINOR << 16) | \
			(IOSS_VER_PATCH << 0) \
		)

#endif /* _IOSS_VERSION_H_ */
