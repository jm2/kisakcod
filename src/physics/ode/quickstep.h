/*************************************************************************
 *                                                                       *
 * Open Dynamics Engine, Copyright (C) 2001,2002 Russell L. Smith.       *
 * All rights reserved.  Email: russ@q12.org   Web: www.q12.org          *
 *                                                                       *
 * This library is free software; you can redistribute it and/or         *
 * modify it under the terms of EITHER:                                  *
 *   (1) The GNU Lesser General Public License as published by the Free  *
 *       Software Foundation; either version 2.1 of the License, or (at  *
 *       your option) any later version. The text of the GNU Lesser      *
 *       General Public License is included with this library in the     *
 *       file LICENSE.TXT.                                               *
 *   (2) The BSD-style license that is included with this library in     *
 *       the file LICENSE-BSD.TXT.                                       *
 *                                                                       *
 * This library is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the files    *
 * LICENSE.TXT and LICENSE-BSD.TXT for more details.                     *
 *                                                                       *
 *************************************************************************/

#ifndef _ODE_QUICK_STEP_H_
#define _ODE_QUICK_STEP_H_

#include <ode/common.h>

// Fixed island caps. dxQuickStepper's per-island arrays are sized from these
// numbers (dxWorld::sd.order and QuickstepData::rowData hold
// ISLAND_MAX_JOINT_COUNT * 6 constraint rows), so dxProcessIslands must never
// hand the stepper a larger island.
#define ISLAND_MAX_JOINT_COUNT 74
#define ISLAND_MAX_BODY_COUNT 14

//void dxQuickStepper (dxWorld *world, dxBody * const *body, int nb,
//		     dxJoint * const *_joint, int nj, dReal stepsize);
void dxQuickStepper(dxWorld *world,
	dxBody **body,
	int bodyCount,
	dxJoint **joint,
	int jointCount,
	float stepsize);


#endif
