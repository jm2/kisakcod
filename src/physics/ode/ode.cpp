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

#ifdef _MSC_VER
#pragma warning(disable:4291)  // for VC++, no complaints about "no matching operator delete found"
#endif

// this source file is mostly concerned with the data structures, not the
// numerics.

#include "objects.h"
#include <ode/ode.h>
#include "joint.h"
#include <ode/odemath.h>
#include <ode/matrix.h>
// #include "step.h" REM
#include "quickstep.h"
#include "util.h"
// #include <ode/memory.h> REM
#include <ode/error.h>

// ADD: for memcpy
#include <string.h>

#include <new>

// misc defines
#define ALLOCA dALLOCA16

//****************************************************************************
// utility

static inline void initObject (dObject *obj, dxWorld *w)
{
  obj->world = w;
  obj->next = 0;
  obj->tome = 0;
  obj->userdata = 0;
  obj->tag = 0;
}


// add an object `obj' to the list who's head pointer is pointed to by `first'.

static inline void addObjectToList (dObject *obj, dObject **first)
{
  obj->next = *first;
  obj->tome = first;
  if (*first) (*first)->tome = &obj->next;
  (*first) = obj;
}


// remove the object from the linked list

static inline void removeObjectFromList (dObject *obj)
{
  if (obj->next) obj->next->tome = obj->tome;
  *(obj->tome) = obj->next;
  // safeguard
  obj->next = 0;
  obj->tome = 0;
}


// remove the joint from neighbour lists of all connected bodies

static void removeJointReferencesFromAttachedBodies (dxJoint *j)
{
  for (int i=0; i<2; i++) {
    dxBody *body = j->node[i].body;
    if (body) {
      dxJointNode *n = body->firstjoint;
      dxJointNode *last = 0;
      while (n) {
	if (n->joint == j) {
	  if (last) last->next = n->next;
	  else body->firstjoint = n->next;
	  break;
	}
	last = n;
	n = n->next;
      }
    }
  }
  j->node[0].body = 0;
  j->node[0].next = 0;
  j->node[1].body = 0;
  j->node[1].next = 0;
}

//****************************************************************************
// debugging

// see if an object list loops on itself (if so, it's bad).

static int listHasLoops (dObject *first)
{
  if (first==0 || first->next==0) return 0;
  dObject *a=first,*b=first->next;
  int skip=0;
  while (b) {
    if (a==b) return 1;
    b = b->next;
    if (skip) a = a->next;
    skip ^= 1;
  }
  return 0;
}


// check the validity of the world data structures

static void checkWorld (dxWorld *w)
{
  dxBody *b;
  dxJoint *j;

  // check there are no loops
  if (listHasLoops (w->firstbody)) dDebug (0,"body list has loops");
  if (listHasLoops (w->firstjoint)) dDebug (0,"joint list has loops");

  // check lists are well formed (check `tome' pointers)
  for (b=w->firstbody; b; b=(dxBody*)b->next) {
    if (b->next && b->next->tome != &b->next)
      dDebug (0,"bad tome pointer in body list");
  }
  for (j=w->firstjoint; j; j=(dxJoint*)j->next) {
    if (j->next && j->next->tome != &j->next)
      dDebug (0,"bad tome pointer in joint list");
  }

  // check counts
  int n = 0;
  for (b=w->firstbody; b; b=(dxBody*)b->next) n++;
  if (w->nb != n) dDebug (0,"body count incorrect");
  n = 0;
  for (j=w->firstjoint; j; j=(dxJoint*)j->next) n++;
  if (w->nj != n) dDebug (0,"joint count incorrect");

  // set all tag values to a known value
  static int count = 0;
  count++;
  for (b=w->firstbody; b; b=(dxBody*)b->next) b->tag = count;
  for (j=w->firstjoint; j; j=(dxJoint*)j->next) j->tag = count;

  // check all body/joint world pointers are ok
  for (b=w->firstbody; b; b=(dxBody*)b->next) if (b->world != w)
    dDebug (0,"bad world pointer in body list");
  for (j=w->firstjoint; j; j=(dxJoint*)j->next) if (j->world != w)
    dDebug (0,"bad world pointer in joint list");

  /*
  // check for half-connected joints - actually now these are valid
  for (j=w->firstjoint; j; j=(dxJoint*)j->next) {
    if (j->node[0].body || j->node[1].body) {
      if (!(j->node[0].body && j->node[1].body))
	dDebug (0,"half connected joint found");
    }
  }
  */

  // check that every joint node appears in the joint lists of both bodies it
  // attaches
  for (j=w->firstjoint; j; j=(dxJoint*)j->next) {
    for (int i=0; i<2; i++) {
      if (j->node[i].body) {
	int ok = 0;
	for (dxJointNode *n=j->node[i].body->firstjoint; n; n=n->next) {
	  if (n->joint == j) ok = 1;
	}
	if (ok==0) dDebug (0,"joint not in joint list of attached body");
      }
    }
  }

  // check all body joint lists (correct body ptrs)
  for (b=w->firstbody; b; b=(dxBody*)b->next) {
    for (dxJointNode *n=b->firstjoint; n; n=n->next) {
      if (&n->joint->node[0] == n) {
	if (n->joint->node[1].body != b)
	  dDebug (0,"bad body pointer in joint node of body list (1)");
      }
      else {
	if (n->joint->node[0].body != b)
	  dDebug (0,"bad body pointer in joint node of body list (2)");
      }
      if (n->joint->tag != count) dDebug (0,"bad joint node pointer in body");
    }
  }

  // check all body pointers in joints, check they are distinct
  for (j=w->firstjoint; j; j=(dxJoint*)j->next) {
    if (j->node[0].body && (j->node[0].body == j->node[1].body))
      dDebug (0,"non-distinct body pointers in joint");
    if ((j->node[0].body && j->node[0].body->tag != count) ||
	(j->node[1].body && j->node[1].body->tag != count))
      dDebug (0,"bad body pointer in joint");
  }
}


void dWorldCheck (dxWorld *w)
{
  checkWorld (w);
}

//****************************************************************************
// body


void dBodySetData (dBodyID b, void *data)
{
  dAASSERT (b);
  b->userdata = data;
}


void *dBodyGetData (dBodyID b)
{
  dAASSERT (b);
  return b->userdata;
}


void dBodySetPosition (dBodyID b, dReal x, dReal y, dReal z)
{
  dAASSERT (b);
  b->info.pos[0] = x;
  b->info.pos[1] = y;
  b->info.pos[2] = z;

  // notify all attached geoms that this body has moved
  for (dxGeom *geom = b->geom; geom; geom = dGeomGetBodyNext (geom))
    dGeomMoved (geom);
}


void dBodySetRotation (dBodyID b, const dMatrix3 R)
{
  dAASSERT (b && R);
  dQuaternion q;
  dRtoQ (R,q);
  dNormalize4 (q);
  b->info.q[0] = q[0];
  b->info.q[1] = q[1];
  b->info.q[2] = q[2];
  b->info.q[3] = q[3];
  dQtoR (b->info.q,b->info.R);

  // notify all attached geoms that this body has moved
  for (dxGeom *geom = b->geom; geom; geom = dGeomGetBodyNext (geom))
    dGeomMoved (geom);
}


void dBodySetQuaternion (dBodyID b, const dQuaternion q)
{
  dAASSERT (b && q);
  b->info.q[0] = q[0];
  b->info.q[1] = q[1];
  b->info.q[2] = q[2];
  b->info.q[3] = q[3];
  dNormalize4 (b->info.q);
  dQtoR (b->info.q,b->info.R);

  // notify all attached geoms that this body has moved
  for (dxGeom *geom = b->geom; geom; geom = dGeomGetBodyNext (geom))
    dGeomMoved (geom);
}


void dBodySetLinearVel  (dBodyID b, dReal x, dReal y, dReal z)
{
  dAASSERT (b);
  b->info.lvel[0] = x;
  b->info.lvel[1] = y;
  b->info.lvel[2] = z;
}


void dBodySetAngularVel (dBodyID b, dReal x, dReal y, dReal z)
{
  dAASSERT (b);
  b->info.avel[0] = x;
  b->info.avel[1] = y;
  b->info.avel[2] = z;
}


const dReal * dBodyGetPosition (dBodyID b)
{
  dAASSERT (b);
  return b->info.pos;
}


const dReal * dBodyGetRotation (dBodyID b)
{
  dAASSERT (b);
  return b->info.R;
}


const dReal * dBodyGetQuaternion (dBodyID b)
{
  dAASSERT (b);
  return b->info.q;
}


const dReal * dBodyGetLinearVel (dBodyID b)
{
  dAASSERT (b);
  return b->info.lvel;
}


const dReal * dBodyGetAngularVel (dBodyID b)
{
  dAASSERT (b);
  return b->info.avel;
}


void dBodySetMass (dBodyID b, const dMass *mass)
{
  dAASSERT (b && mass);
  memcpy (&b->mass,mass,sizeof(dMass));
  if (dInvertPDMatrix (b->mass.I,b->invI,3)==0) {
    dDEBUGMSG ("inertia must be positive definite");
    dRSetIdentity (b->invI);
  }
  b->invMass = dRecip(b->mass.mass);
}


void dBodyGetMass (dBodyID b, dMass *mass)
{
  dAASSERT (b && mass);
  memcpy (mass,&b->mass,sizeof(dMass));
}


void dBodyAddForce (dBodyID b, dReal fx, dReal fy, dReal fz)
{
  dAASSERT (b);
  b->facc[0] += fx;
  b->facc[1] += fy;
  b->facc[2] += fz;
}


void dBodyAddTorque (dBodyID b, dReal fx, dReal fy, dReal fz)
{
  dAASSERT (b);
  b->tacc[0] += fx;
  b->tacc[1] += fy;
  b->tacc[2] += fz;
}


void dBodyAddRelForce (dBodyID b, dReal fx, dReal fy, dReal fz)
{
  dAASSERT (b);
  dVector3 t1,t2;
  t1[0] = fx;
  t1[1] = fy;
  t1[2] = fz;
  t1[3] = 0;
  dMULTIPLY0_331 (t2,b->info.R,t1);
  b->facc[0] += t2[0];
  b->facc[1] += t2[1];
  b->facc[2] += t2[2];
}


void dBodyAddRelTorque (dBodyID b, dReal fx, dReal fy, dReal fz)
{
  dAASSERT (b);
  dVector3 t1,t2;
  t1[0] = fx;
  t1[1] = fy;
  t1[2] = fz;
  t1[3] = 0;
  dMULTIPLY0_331 (t2,b->info.R,t1);
  b->tacc[0] += t2[0];
  b->tacc[1] += t2[1];
  b->tacc[2] += t2[2];
}


void dBodyAddForceAtPos (dBodyID b, dReal fx, dReal fy, dReal fz,
			 dReal px, dReal py, dReal pz)
{
  dAASSERT (b);
  b->facc[0] += fx;
  b->facc[1] += fy;
  b->facc[2] += fz;
  dVector3 f,q;
  f[0] = fx;
  f[1] = fy;
  f[2] = fz;
  q[0] = px - b->info.pos[0];
  q[1] = py - b->info.pos[1];
  q[2] = pz - b->info.pos[2];
  dCROSS (b->tacc,+=,q,f);
}


void dBodyAddForceAtRelPos (dBodyID b, dReal fx, dReal fy, dReal fz,
			    dReal px, dReal py, dReal pz)
{
  dAASSERT (b);
  dVector3 prel,f,p;
  f[0] = fx;
  f[1] = fy;
  f[2] = fz;
  f[3] = 0;
  prel[0] = px;
  prel[1] = py;
  prel[2] = pz;
  prel[3] = 0;
  dMULTIPLY0_331 (p,b->info.R,prel);
  b->facc[0] += f[0];
  b->facc[1] += f[1];
  b->facc[2] += f[2];
  dCROSS (b->tacc,+=,p,f);
}


void dBodyAddRelForceAtPos (dBodyID b, dReal fx, dReal fy, dReal fz,
			    dReal px, dReal py, dReal pz)
{
  dAASSERT (b);
  dVector3 frel,f;
  frel[0] = fx;
  frel[1] = fy;
  frel[2] = fz;
  frel[3] = 0;
  dMULTIPLY0_331 (f,b->info.R,frel);
  b->facc[0] += f[0];
  b->facc[1] += f[1];
  b->facc[2] += f[2];
  dVector3 q;
  q[0] = px - b->info.pos[0];
  q[1] = py - b->info.pos[1];
  q[2] = pz - b->info.pos[2];
  dCROSS (b->tacc,+=,q,f);
}


void dBodyAddRelForceAtRelPos (dBodyID b, dReal fx, dReal fy, dReal fz,
			       dReal px, dReal py, dReal pz)
{
  dAASSERT (b);
  dVector3 frel,prel,f,p;
  frel[0] = fx;
  frel[1] = fy;
  frel[2] = fz;
  frel[3] = 0;
  prel[0] = px;
  prel[1] = py;
  prel[2] = pz;
  prel[3] = 0;
  dMULTIPLY0_331 (f,b->info.R,frel);
  dMULTIPLY0_331 (p,b->info.R,prel);
  b->facc[0] += f[0];
  b->facc[1] += f[1];
  b->facc[2] += f[2];
  dCROSS (b->tacc,+=,p,f);
}


const dReal * dBodyGetForce (dBodyID b)
{
  dAASSERT (b);
  return b->facc;
}


const dReal * dBodyGetTorque (dBodyID b)
{
  dAASSERT (b);
  return b->tacc;
}


void dBodySetForce (dBodyID b, dReal x, dReal y, dReal z)
{
  dAASSERT (b);
  b->facc[0] = x;
  b->facc[1] = y;
  b->facc[2] = z;
}


void dBodySetTorque (dBodyID b, dReal x, dReal y, dReal z)
{
  dAASSERT (b);
  b->tacc[0] = x;
  b->tacc[1] = y;
  b->tacc[2] = z;
}


void dBodyGetRelPointPos (dBodyID b, dReal px, dReal py, dReal pz,
			  dVector3 result)
{
  dAASSERT (b);
  dVector3 prel,p;
  prel[0] = px;
  prel[1] = py;
  prel[2] = pz;
  prel[3] = 0;
  dMULTIPLY0_331 (p,b->info.R,prel);
  result[0] = p[0] + b->info.pos[0];
  result[1] = p[1] + b->info.pos[1];
  result[2] = p[2] + b->info.pos[2];
}


void dBodyGetRelPointVel (dBodyID b, dReal px, dReal py, dReal pz,
			  dVector3 result)
{
  dAASSERT (b);
  dVector3 prel,p;
  prel[0] = px;
  prel[1] = py;
  prel[2] = pz;
  prel[3] = 0;
  dMULTIPLY0_331 (p,b->info.R,prel);
  result[0] = b->info.lvel[0];
  result[1] = b->info.lvel[1];
  result[2] = b->info.lvel[2];
  dCROSS (result,+=,b->info.avel,p);
}


void dBodyGetPointVel (dBodyID b, dReal px, dReal py, dReal pz,
		       dVector3 result)
{
  dAASSERT (b);
  dVector3 p;
  p[0] = px - b->info.pos[0];
  p[1] = py - b->info.pos[1];
  p[2] = pz - b->info.pos[2];
  p[3] = 0;
  result[0] = b->info.lvel[0];
  result[1] = b->info.lvel[1];
  result[2] = b->info.lvel[2];
  dCROSS (result,+=,b->info.avel,p);
}


void dBodyGetPosRelPoint (dBodyID b, dReal px, dReal py, dReal pz,
			  dVector3 result)
{
  dAASSERT (b);
  dVector3 prel;
  prel[0] = px - b->info.pos[0];
  prel[1] = py - b->info.pos[1];
  prel[2] = pz - b->info.pos[2];
  prel[3] = 0;
  dMULTIPLY1_331 (result,b->info.R,prel);
}


void dBodyVectorToWorld (dBodyID b, dReal px, dReal py, dReal pz,
			 dVector3 result)
{
  dAASSERT (b);
  dVector3 p;
  p[0] = px;
  p[1] = py;
  p[2] = pz;
  p[3] = 0;
  dMULTIPLY0_331 (result,b->info.R,p);
}


void dBodyVectorFromWorld (dBodyID b, dReal px, dReal py, dReal pz,
			   dVector3 result)
{
  dAASSERT (b);
  dVector3 p;
  p[0] = px;
  p[1] = py;
  p[2] = pz;
  p[3] = 0;
  dMULTIPLY1_331 (result,b->info.R,p);
}


void dBodySetFiniteRotationMode (dBodyID b, int mode)
{
  dAASSERT (b);
  b->flags &= ~(dxBodyFlagFiniteRotation | dxBodyFlagFiniteRotationAxis);
  if (mode) {
    b->flags |= dxBodyFlagFiniteRotation;
    if (b->finite_rot_axis[0] != 0 || b->finite_rot_axis[1] != 0 ||
	b->finite_rot_axis[2] != 0) {
      b->flags |= dxBodyFlagFiniteRotationAxis;
    }
  }
}


void dBodySetFiniteRotationAxis (dBodyID b, dReal x, dReal y, dReal z)
{
  dAASSERT (b);
  b->finite_rot_axis[0] = x;
  b->finite_rot_axis[1] = y;
  b->finite_rot_axis[2] = z;
  if (x != 0 || y != 0 || z != 0) {
    dNormalize3 (b->finite_rot_axis);
    b->flags |= dxBodyFlagFiniteRotationAxis;
  }
  else {
    b->flags &= ~dxBodyFlagFiniteRotationAxis;
  }
}


int dBodyGetFiniteRotationMode (dBodyID b)
{
  dAASSERT (b);
  return ((b->flags & dxBodyFlagFiniteRotation) != 0);
}


void dBodyGetFiniteRotationAxis (dBodyID b, dVector3 result)
{
  dAASSERT (b);
  result[0] = b->finite_rot_axis[0];
  result[1] = b->finite_rot_axis[1];
  result[2] = b->finite_rot_axis[2];
}


int dBodyGetNumJoints (dBodyID b)
{
  dAASSERT (b);
  int count=0;
  for (dxJointNode *n=b->firstjoint; n; n=n->next, count++);
  return count;
}


dJointID dBodyGetJoint (dBodyID b, int index)
{
  dAASSERT (b);
  int i=0;
  for (dxJointNode *n=b->firstjoint; n; n=n->next, i++) {
    if (i == index) return n->joint;
  }
  return 0;
}


void dBodyEnable (dBodyID b)
{
  dAASSERT (b);
  b->flags &= ~dxBodyDisabled;
  b->adis_stepsleft = b->adis.idle_steps;
  b->adis_timeleft = b->adis.idle_time;
}


void dBodyDisable (dBodyID b)
{
  dAASSERT (b);
  b->flags |= dxBodyDisabled;
}


int dBodyIsEnabled (dBodyID b)
{
  dAASSERT (b);
  return ((b->flags & dxBodyDisabled) == 0);
}


void dBodySetGravityMode (dBodyID b, int mode)
{
  dAASSERT (b);
  if (mode) b->flags &= ~dxBodyNoGravity;
  else b->flags |= dxBodyNoGravity;
}


int dBodyGetGravityMode (dBodyID b)
{
  dAASSERT (b);
  return ((b->flags & dxBodyNoGravity) == 0);
}


// body auto-disable functions

dReal dBodyGetAutoDisableLinearThreshold (dBodyID b)
{
	dAASSERT(b);
	return dSqrt (b->adis.linear_threshold);
}


void dBodySetAutoDisableLinearThreshold (dBodyID b, dReal linear_threshold)
{
	dAASSERT(b);
	b->adis.linear_threshold = linear_threshold * linear_threshold;
}


dReal dBodyGetAutoDisableAngularThreshold (dBodyID b)
{
	dAASSERT(b);
	return dSqrt (b->adis.angular_threshold);
}


void dBodySetAutoDisableAngularThreshold (dBodyID b, dReal angular_threshold)
{
	dAASSERT(b);
	b->adis.angular_threshold = angular_threshold * angular_threshold;
}


int dBodyGetAutoDisableSteps (dBodyID b)
{
	dAASSERT(b);
	return b->adis.idle_steps;
}


void dBodySetAutoDisableSteps (dBodyID b, int steps)
{
	dAASSERT(b);
	b->adis.idle_steps = steps;
}


dReal dBodyGetAutoDisableTime (dBodyID b)
{
	dAASSERT(b);
	return b->adis.idle_time;
}


void dBodySetAutoDisableTime (dBodyID b, dReal time)
{
	dAASSERT(b);
	b->adis.idle_time = time;
}


int dBodyGetAutoDisableFlag (dBodyID b)
{
	dAASSERT(b);
	return ((b->flags & dxBodyAutoDisable) != 0);
}


void dBodySetAutoDisableFlag (dBodyID b, int do_auto_disable)
{
	dAASSERT(b);
	if (!do_auto_disable) b->flags &= ~dxBodyAutoDisable;
	else b->flags |= dxBodyAutoDisable;
}


void dBodySetAutoDisableDefaults (dBodyID b)
{
	dAASSERT(b);
	dWorldID w = b->world;
	dAASSERT(w);
	b->adis = w->adis;
	dBodySetAutoDisableFlag (b, w->adis_flag);
}

//****************************************************************************
// joints

#if 0
static void dJointInit (dxWorld *w, dxJoint *j)
{
  dIASSERT (w && j);
  initObject (j,w);
  j->vtable = 0;
  j->flags = 0;
  j->node[0].joint = j;
  j->node[0].body = 0;
  j->node[0].next = 0;
  j->node[1].joint = j;
  j->node[1].body = 0;
  j->node[1].next = 0;
  dSetZero (j->lambda,6);
  addObjectToList (j,(dObject **) &w->firstjoint);
  w->nj++;
}
#endif

// MOD
void jointInit(dxJoint* joint);
void ODE_InitJoint(dxWorld* world, dxJoint* joint, dxJointTypeNum typenum)
{
    dAASSERT(world);
    dAASSERT(joint);

    joint->world = world;
    joint->next = 0;
    joint->tome = 0;
    joint->userdata = 0;
    joint->tag = 0;
    joint->typenum = dJointTypeNone;
    joint->flags = 0;

    joint->node[0].joint = joint;
    joint->node[0].body = 0;
    joint->node[0].next = 0;

    joint->node[1].joint = joint;
    joint->node[1].body = 0;
    joint->node[1].next = 0;

    addObjectToList(joint, (dObject **) &world->firstjoint);
    ++world->nj;
    joint->typenum = typenum;

    jointInit(joint);
}

dxJoint* __cdecl createJointInPlace(dxWorld* world, dxJoint* joint, dxJointTypeNum typenum)
{
    dAASSERT(world);
    dAASSERT(joint);

    if (world->nj >= 4096)
        return 0;
    
    ODE_InitJoint(world, joint, typenum);
    return joint;
}

// REM
#if 0
static dxJoint *createJoint (dWorldID w, dJointGroupID group,
			     dxJoint::Vtable *vtable)
{
  dIASSERT (w && vtable);
  dxJoint *j;
  if (group) {
    j = (dxJoint*) group->stack.alloc (vtable->size);
    group->num++;
  }
  else j = (dxJoint*) dAlloc (vtable->size);
  dJointInit (w,j);
  j->vtable = vtable;
  if (group) j->flags |= dJOINT_INGROUP;
  if (vtable->init) vtable->init (j);
  j->feedback = 0;
  return j;
}
#endif

dxJoint * dJointCreateBall (dWorldID w, dxJointBall* joint)
{
  dAASSERT (w);
  return createJointInPlace(w, joint, dJointTypeBall);
}


dxJoint * dJointCreateHinge (dWorldID w, dxJointHinge* joint)
{
  dAASSERT (w);
  return createJointInPlace(w, joint, dJointTypeHinge);
}


dxJoint * dJointCreateSlider (dWorldID w, dxJointSlider* joint)
{
  dAASSERT (w);
  return createJointInPlace(w, joint, dJointTypeSlider);
}


dxJoint * dJointCreateContact (dWorldID w, dJointGroupID group,
    const dSurfaceParameters *surfParms,
    const dContactGeom *c)
{
    dAASSERT(w);
  dAASSERT(c);
  dAASSERT(surfParms);
  dAASSERT(group);

  dxJointContact* joint; // [esp+8h] [ebp-4h]

  if (w->nj >= ODE_WORLD_MAX_JOINT_COUNT || group->num >= ODE_WORLD_MAX_JOINT_COUNT)
      return 0;
  joint = &group->joints[group->num++];
  ODE_InitJoint(w, joint, dJointTypeContact);
  joint->flags |= dJOINT_INGROUP;
  memcpy(&joint->contact.surface, surfParms, sizeof(joint->contact.surface));
  memcpy(&joint->contact.geom, c, sizeof(joint->contact.geom));
  return joint;
}

// REM
#if 0
dxJoint * dJointCreateHinge2 (dWorldID w, dxJointHinge2* joint)
{
  dAASSERT (w);
  return createJointInPlace(w, joint, dJointTypeHinge2);
}
#endif

dxJoint * dJointCreateUniversal (dWorldID w, dxJointUniversal* joint)
{
  dAASSERT (w);
  return createJointInPlace(w, joint, dJointTypeUniversal);
}


dxJoint * dJointCreateFixed (dWorldID w, dxJointFixed* joint)
{
  dAASSERT (w);
  return createJointInPlace(w, joint, dJointTypeFixed);
}

// DEL
#if 0
dxJoint * dJointCreateNull (dWorldID w, dxJointBall* joint)
{
  dAASSERT (w);
  return createJoint (w,group,&__dnull_vtable);
}
#endif

dxJoint * dJointCreateAMotor (dWorldID w, dxJointAMotor* joint)
{
  dAASSERT (w);
  return createJointInPlace(w, joint, dJointTypeAMotor);
}


void dJointDestroy (dxJoint *j)
{
  dAASSERT (j);

  if (!(j->flags & dJOINT_INGROUP))
  {
      removeJointReferencesFromAttachedBodies(j);
      removeObjectFromList(j);
      --j->world->nj;
  }
}


dJointGroupID dJointGroupCreate (int max_size)
{
  // not any more ... dUASSERT (max_size > 0,"max size must be > 0");
  dxJointGroup *group = new dxJointGroup;
  group->num = 0;
  return group;
}


void dJointGroupDestroy (dJointGroupID group)
{
  dAASSERT (group);
  dJointGroupEmpty (group);
  // delete group;
}


void dJointAttach (dxJoint *joint, dxBody *body1, dxBody *body2)
{
  // check arguments
  dUASSERT (joint,"bad joint argument");
  dUASSERT (body1 == 0 || body1 != body2,"can't have body1==body2");
  dxWorld *world = joint->world;
  dUASSERT ( (!body1 || body1->world == world) &&
	     (!body2 || body2->world == world),
	     "joint and bodies must be in same world");

  // check if the joint can not be attached to just one body
  dUASSERT (!((joint->flags & dJOINT_TWOBODIES) &&
	      ((body1 != 0) ^ (body2 != 0))),
	    "joint can not be attached to just one body");

  // remove any existing body attachments
  if (joint->node[0].body || joint->node[1].body) {
    removeJointReferencesFromAttachedBodies (joint);
  }

  // if a body is zero, make sure that it is body2, so 0 --> node[1].body
  if (body1==0) {
    body1 = body2;
    body2 = 0;
    joint->flags |= dJOINT_REVERSE;
  }
  else {
    joint->flags &= (~dJOINT_REVERSE);
  }

  // attach to new bodies
  joint->node[0].body = body1;
  joint->node[1].body = body2;
  if (body1) {
    joint->node[1].next = body1->firstjoint;
    body1->firstjoint = &joint->node[1];
  }
  else joint->node[1].next = 0;
  if (body2) {
    joint->node[0].next = body2->firstjoint;
    body2->firstjoint = &joint->node[0];
  }
  else {
    joint->node[0].next = 0;
  }
}


void dJointSetData (dxJoint *joint, void *data)
{
  dAASSERT (joint);
  joint->userdata = data;
}


void *dJointGetData (dxJoint *joint)
{
  dAASSERT (joint);
  return joint->userdata;
}


int dJointGetType (dxJoint *joint)
{
  dAASSERT (joint);
  return joint->typenum;
}


dBodyID dJointGetBody (dxJoint *joint, int index)
{
  dAASSERT (joint);
  if (index == 0 || index == 1) {
    if (joint->flags & dJOINT_REVERSE) return joint->node[1-index].body;
    else return joint->node[index].body;
  }
  else return 0;
}

// DEL
#if 0
void dJointSetFeedback (dxJoint *joint, dJointFeedback *f)
{
  dAASSERT (joint);
  joint->feedback = f;
}

dJointFeedback *dJointGetFeedback (dxJoint *joint)
{
  dAASSERT (joint);
  return joint->feedback;
}
#endif

int dAreConnected (dBodyID b1, dBodyID b2)
{
  dAASSERT (b1 && b2);
  // look through b1's neighbour list for b2
  for (dxJointNode *n=b1->firstjoint; n; n=n->next) {
    if (n->body == b2) return 1;
  }
  return 0;
}


int dAreConnectedExcluding (dBodyID b1, dBodyID b2, int joint_type)
{
  dAASSERT (b1 && b2);
  // look through b1's neighbour list for b2
  for (dxJointNode *n=b1->firstjoint; n; n=n->next) {
    if (dJointGetType (n->joint) != joint_type && n->body == b2) return 1;
  }
  return 0;
}

//****************************************************************************
// world



void dWorldSetGravity (dWorldID w, dReal x, dReal y, dReal z)
{
  dAASSERT (w);
  w->stepInfo.gravity[0] = x;
  w->stepInfo.gravity[1] = y;
  w->stepInfo.gravity[2] = z;
}


void dWorldGetGravity (dWorldID w, dVector3 g)
{
  dAASSERT (w);
  g[0] = w->stepInfo.gravity[0];
  g[1] = w->stepInfo.gravity[1];
  g[2] = w->stepInfo.gravity[2];
}


void dWorldSetERP (dWorldID w, dReal erp)
{
  dAASSERT (w);
  w->stepInfo.global_erp = erp;
}


dReal dWorldGetERP (dWorldID w)
{
  dAASSERT (w);
  return w->stepInfo.global_erp;
}


void dWorldSetCFM (dWorldID w, dReal cfm)
{
  dAASSERT (w);
  w->stepInfo.global_cfm = cfm;
}


dReal dWorldGetCFM (dWorldID w)
{
  dAASSERT (w);
  return w->stepInfo.global_cfm;
}

// REM
#if 0
void dWorldStep (dWorldID w, dReal stepsize)
{
  dUASSERT (w,"bad world argument");
  dUASSERT (stepsize > 0,"stepsize must be > 0");
  dxProcessIslands (w,stepsize,&dInternalStepIsland);
}
#endif

void dWorldQuickStep (dWorldID w, dReal stepsize)
{
  dUASSERT (w,"bad world argument");
  dUASSERT (stepsize > 0,"stepsize must be > 0");
  //dxProcessIslands (w,stepsize,&dxQuickStepper);
  dxProcessIslands(w, stepsize);
}


void dWorldImpulseToForce (dWorldID w, dReal stepsize,
			   dReal ix, dReal iy, dReal iz,
			   dVector3 force)
{
  dAASSERT (w);
  stepsize = dRecip(stepsize);
  force[0] = stepsize * ix;
  force[1] = stepsize * iy;
  force[2] = stepsize * iz;
  // @@@ force[3] = 0;
}


// world auto-disable functions

dReal dWorldGetAutoDisableLinearThreshold (dWorldID w)
{
	dAASSERT(w);
	return dSqrt (w->adis.linear_threshold);
}


void dWorldSetAutoDisableLinearThreshold (dWorldID w, dReal linear_threshold)
{
	dAASSERT(w);
	w->adis.linear_threshold = linear_threshold * linear_threshold;
}


dReal dWorldGetAutoDisableAngularThreshold (dWorldID w)
{
	dAASSERT(w);
	return dSqrt (w->adis.angular_threshold);
}


void dWorldSetAutoDisableAngularThreshold (dWorldID w, dReal angular_threshold)
{
	dAASSERT(w);
	w->adis.angular_threshold = angular_threshold * angular_threshold;
}


int dWorldGetAutoDisableSteps (dWorldID w)
{
	dAASSERT(w);
	return w->adis.idle_steps;
}


void dWorldSetAutoDisableSteps (dWorldID w, int steps)
{
	dAASSERT(w);
	w->adis.idle_steps = steps;
}


dReal dWorldGetAutoDisableTime (dWorldID w)
{
	dAASSERT(w);
	return w->adis.idle_time;
}


void dWorldSetAutoDisableTime (dWorldID w, dReal time)
{
	dAASSERT(w);
	w->adis.idle_time = time;
}


int dWorldGetAutoDisableFlag (dWorldID w)
{
	dAASSERT(w);
	return w->adis_flag;
}


void dWorldSetAutoDisableFlag (dWorldID w, int do_auto_disable)
{
	dAASSERT(w);
	w->adis_flag = (do_auto_disable != 0);
}


void dWorldSetQuickStepNumIterations (dWorldID w, int num)
{
	dAASSERT(w);
	w->stepInfo.qs.num_iterations = num;
}


int dWorldGetQuickStepNumIterations (dWorldID w)
{
	dAASSERT(w);
	return w->stepInfo.qs.num_iterations;
}


void dWorldSetQuickStepW (dWorldID w, dReal param)
{
	dAASSERT(w);
	w->stepInfo.qs.w = param;
}


dReal dWorldGetQuickStepW (dWorldID w)
{
	dAASSERT(w);
	return w->stepInfo.qs.w;
}


void dWorldSetContactMaxCorrectingVel (dWorldID w, dReal vel)
{
	dAASSERT(w);
	w->stepInfo.contactp.max_vel = vel;
}


dReal dWorldGetContactMaxCorrectingVel (dWorldID w)
{
	dAASSERT(w);
	return w->stepInfo.contactp.max_vel;
}


void dWorldSetContactSurfaceLayer (dWorldID w, dReal depth)
{
	dAASSERT(w);
	w->stepInfo.contactp.min_depth = depth;
}


dReal dWorldGetContactSurfaceLayer (dWorldID w)
{
	dAASSERT(w);
	return w->stepInfo.contactp.min_depth;
}

//****************************************************************************
// testing
#if 0

#define NUM 100

#define DO(x)


extern "C" void dTestDataStructures()
{
    int i;
    DO(printf("testDynamicsStuff()\n"));

    dBodyID body[NUM];
    int nb = 0;
    dJointID joint[NUM];
    int nj = 0;

    for (i = 0; i < NUM; i++) body[i] = 0;
    for (i = 0; i < NUM; i++) joint[i] = 0;

    DO(printf("creating world\n"));
    dWorldID w = dWorldCreate();
    checkWorld(w);

    for (;;) {
        if (nb < NUM && dRandReal() > 0.5) {
            DO(printf("creating body\n"));
            body[nb] = dBodyCreate(w);
            DO(printf("\t--> %p\n", body[nb]));
            nb++;
            checkWorld(w);
            DO(printf("%d BODIES, %d JOINTS\n", nb, nj));
        }
        if (nj < NUM && nb > 2 && dRandReal() > 0.5) {
            dBodyID b1 = body[dRand() % nb];
            dBodyID b2 = body[dRand() % nb];
            if (b1 != b2) {
                DO(printf("creating joint, attaching to %p,%p\n", b1, b2));
                joint[nj] = dJointCreateBall(w, 0);
                DO(printf("\t-->%p\n", joint[nj]));
                checkWorld(w);
                dJointAttach(joint[nj], b1, b2);
                nj++;
                checkWorld(w);
                DO(printf("%d BODIES, %d JOINTS\n", nb, nj));
            }
        }
        if (nj > 0 && nb > 2 && dRandReal() > 0.5) {
            dBodyID b1 = body[dRand() % nb];
            dBodyID b2 = body[dRand() % nb];
            if (b1 != b2) {
                int k = dRand() % nj;
                DO(printf("reattaching joint %p\n", joint[k]));
                dJointAttach(joint[k], b1, b2);
                checkWorld(w);
                DO(printf("%d BODIES, %d JOINTS\n", nb, nj));
            }
        }
        if (nb > 0 && dRandReal() > 0.5) {
            int k = dRand() % nb;
            DO(printf("destroying body %p\n", body[k]));
            dBodyDestroy(body[k]);
            checkWorld(w);
            for (; k < (NUM - 1); k++) body[k] = body[k + 1];
            nb--;
            DO(printf("%d BODIES, %d JOINTS\n", nb, nj));
        }
        if (nj > 0 && dRandReal() > 0.5) {
            int k = dRand() % nj;
            DO(printf("destroying joint %p\n", joint[k]));
            dJointDestroy(joint[k]);
            checkWorld(w);
            for (; k < (NUM - 1); k++) joint[k] = joint[k + 1];
            nj--;
            DO(printf("%d BODIES, %d JOINTS\n", nb, nj));
        }
    }

    /*
    printf ("creating world\n");
    dWorldID w = dWorldCreate();
    checkWorld (w);
    printf ("creating body\n");
    dBodyID b1 = dBodyCreate (w);
    checkWorld (w);
    printf ("creating body\n");
    dBodyID b2 = dBodyCreate (w);
    checkWorld (w);
    printf ("creating joint\n");
    dJointID j = dJointCreateBall (w);
    checkWorld (w);
    printf ("attaching joint\n");
    dJointAttach (j,b1,b2);
    checkWorld (w);
    printf ("destroying joint\n");
    dJointDestroy (j);
    checkWorld (w);
    printf ("destroying body\n");
    dBodyDestroy (b1);
    checkWorld (w);
    printf ("destroying body\n");
    dBodyDestroy (b2);
    checkWorld (w);
    printf ("destroying world\n");
    dWorldDestroy (w);
    */
}
#endif

// LWSS ADD
#include "odeext.h"

#include <universal/pool_allocator.h>
#include <win32/win_local.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

odeGlob_t odeGlob;

namespace
{
poolslotstate_t odeBodyPoolSlotState[512]{};
poolslotstate_t odeGeomPoolSlotState[ODE_GEOM_POOL_COUNT]{};
poolcontrol_t odeBodyPoolControl =
    Pool_ControlFor(odeBodyPoolSlotState);
poolcontrol_t odeGeomPoolControl =
    Pool_ControlFor(odeGeomPoolSlotState);
}

poolstorage_t ODE_BodyPoolStorage() noexcept
{
    return Pool_StorageFor(odeGlob.bodies, odeBodyPoolControl);
}

poolstorage_t ODE_GeomPoolStorage() noexcept
{
    return Pool_StorageFor(
        odeGlob.geoms,
        sizeof(dxGeomTransform),
        ODE_GEOM_POOL_COUNT,
        odeGeomPoolControl);
}

void __cdecl ODE_LeakCheck()
{
    const poolstorage_t bodyStorage = ODE_BodyPoolStorage();
    const poolstorage_t geomStorage = ODE_GeomPoolStorage();
    const bool bodyPoolValid = Pool_ValidateFull(
        bodyStorage, &odeGlob.bodyPool);
    const poolcountresult_t bodyFreeCount = Pool_GetFreeCount(
        bodyStorage, &odeGlob.bodyPool);
    if (!bodyPoolValid || !bodyFreeCount.valid
        || bodyFreeCount.count != ARRAY_COUNT(odeGlob.bodies))
    {
        MyAssertHandler(
            __FILE__,
            __LINE__,
            0,
            "body pool free count = %zu",
            bodyFreeCount.count);
    }

    const bool geomPoolValid = Pool_ValidateFull(
        geomStorage, &odeGlob.geomPool);
    const poolcountresult_t geomFreeCount = Pool_GetFreeCount(
        geomStorage, &odeGlob.geomPool);
    if (!geomPoolValid || !geomFreeCount.valid
        || geomFreeCount.count != ODE_GEOM_POOL_COUNT)
    {
        bool is_free[ODE_GEOM_POOL_COUNT] = {};
        bool freeListValid = geomPoolValid && geomFreeCount.valid;
        std::size_t traversedFreeCount = 0;
        void *freeNode = odeGlob.geomPool.firstFree;
        while (freeListValid && freeNode)
        {
            if (traversedFreeCount >= ODE_GEOM_POOL_COUNT)
            {
                freeListValid = false;
                break;
            }

            const poolindexresult_t slotIndex = Pool_GetSlotIndex(
                geomStorage, freeNode);
            if (!slotIndex.valid
                || slotIndex.index >= ODE_GEOM_POOL_COUNT
                || is_free[slotIndex.index])
            {
                freeListValid = false;
                break;
            }

            is_free[slotIndex.index] = true;
            ++traversedFreeCount;
            const poolnextresult_t nextFree = Pool_NextFree(
                geomStorage, freeNode);
            if (!nextFree.valid)
            {
                freeListValid = false;
                break;
            }
            freeNode = nextFree.next;
        }
        if (traversedFreeCount != geomFreeCount.count)
            freeListValid = false;

        if (!freeListValid)
            fprintf(stderr, "ODE geom pool free list is corrupt\n");

        for (std::size_t i = 0;
             freeListValid && i < ODE_GEOM_POOL_COUNT;
             ++i)
        {
            if (is_free[i])
                continue;

            const auto *const geom = reinterpret_cast<const dxGeom *>(
                odeGlob.geoms + i * sizeof(dxGeomTransform));
            fprintf(stderr, "[%zu] type = %d\n", i, geom->type);
        }

        iassert(freeListValid);
        iassert(0);
    }
}

dxUserGeom *__cdecl Phys_GetWorldGeom()
{
    return &odeGlob.worldGeom;
}

bool __cdecl ODE_Init()
{
    const poolstorage_t bodyStorage = ODE_BodyPoolStorage();
    const poolstorage_t geomStorage = ODE_GeomPoolStorage();
    const bool bodyInvalidated = Pool_Invalidate(
        bodyStorage, &odeGlob.bodyPool);
    const bool geomInvalidated = Pool_Invalidate(
        geomStorage, &odeGlob.geomPool);
    if (!bodyInvalidated || !geomInvalidated)
        return false;
    if (!Pool_Init(bodyStorage, &odeGlob.bodyPool))
        return false;
    if (Pool_Init(geomStorage, &odeGlob.geomPool))
        return true;

    (void)Pool_Invalidate(bodyStorage, &odeGlob.bodyPool);
    (void)Pool_Invalidate(geomStorage, &odeGlob.geomPool);
    return false;
}

// MOD
#include <xanim/dobj.h>
#include <physics/phys_local.h>

static bool ODE_NoReportWorldBodyListIsValid(
    dxWorld *world,
    dxBody *target = nullptr) noexcept;
static bool ODE_NoReportGlobalBodyListsAreValid() noexcept;
static bool ODE_NoReportBodyAllocationCandidateHasNoAliases(
    dxBody *candidate) noexcept;

static dxBody *ODE_InitializeAllocatedBody(
    dxWorld *const world,
    dxBody *const body) noexcept
{
    initObject(body, world);
    body->firstjoint = 0;
    body->flags = 0;
    body->geom = 0;
    body->mass.mass = 1;
    std::memset(body->mass.c, 0, sizeof(body->mass.c));
    std::memset(body->mass.I, 0, sizeof(body->mass.I));
    body->mass.I[0] = 1;
    body->mass.I[5] = 1;
    body->mass.I[10] = 1;
    std::memset(body->invI, 0, sizeof(body->invI));
    body->invI[0] = 1;
    body->invI[5] = 1;
    body->invI[10] = 1;
    body->invMass = 1;
    std::memset(&body->info, 0, sizeof(body->info));
    body->info.q[0] = 1;
    body->info.R[0] = 1;
    body->info.R[5] = 1;
    body->info.R[10] = 1;
    std::memset(body->facc, 0, sizeof(body->facc));
    std::memset(body->tacc, 0, sizeof(body->tacc));
    std::memset(body->finite_rot_axis, 0, sizeof(body->finite_rot_axis));
    body->adis = world->adis;
    if (world->adis_flag)
        body->flags |= dxBodyAutoDisable;
    body->adis_stepsleft = body->adis.idle_steps;
    body->adis_timeleft = body->adis.idle_time;
    addObjectToList(body, reinterpret_cast<dObject **>(&world->firstbody));
    ++world->nb;
    return body;
}

dxBody *dBodyCreate(dxWorld *w)
{
    dAASSERT(w);
#ifdef USE_POOL_ALLOCATOR
    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    dxBody *b = static_cast<dxBody *>(
        Pool_Alloc(ODE_BodyPoolStorage(), &odeGlob.bodyPool));
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
#else
    dxBody *b = new (std::nothrow) dxBody;
#endif

    if (!b)
        return nullptr;
    return ODE_InitializeAllocatedBody(w, b);
}

poolmutationstatus_t ODE_TryBodyCreateNoReport(
    dxWorld *const world,
    dxBody **const outBody) noexcept
{
    if (!outBody)
        return poolmutationstatus_t::InvalidState;
    *outBody = nullptr;
    if (!world || !ODE_NoReportGlobalBodyListsAreValid()
        || !ODE_NoReportWorldBodyListIsValid(world))
        return poolmutationstatus_t::InvalidState;

#ifdef USE_POOL_ALLOCATOR
    auto *const allocationCandidate =
        static_cast<dxBody *>(odeGlob.bodyPool.firstFree);
    if (!allocationCandidate)
        return poolmutationstatus_t::Unavailable;
    if (!ODE_NoReportBodyAllocationCandidateHasNoAliases(
            allocationCandidate))
    {
        return poolmutationstatus_t::InvalidState;
    }
    const poolallocresult_t allocation = Pool_TryAllocNoReport(
        ODE_BodyPoolStorage(), &odeGlob.bodyPool);
    if (allocation.status != poolmutationstatus_t::Success)
        return allocation.status;
    auto *const body = static_cast<dxBody *>(allocation.item);
#else
    auto *const body = new (std::nothrow) dxBody;
    if (!body)
        return poolmutationstatus_t::Unavailable;
#endif
    *outBody = ODE_InitializeAllocatedBody(world, body);
    return poolmutationstatus_t::Success;
}

dxJointGroup *__cdecl dGetContactJointGroup(PhysWorld worldIndex)
{
    odeGlob.contactsGroup[worldIndex].num = 0;
    return &odeGlob.contactsGroup[worldIndex];
}

dxSimpleSpace *__cdecl dGetSimpleSpace(PhysWorld worldIndex)
{
    if (&odeGlob.space[worldIndex])
    {
        //dxSimpleSpace::dxSimpleSpace(&odeGlob.space[worldIndex], 0);
        odeGlob.space[worldIndex].ReInit();
    }
    return &odeGlob.space[worldIndex];
}

dxWorld* dWorldCreate(PhysWorld worldIndex)
{
    dxWorld *w = &odeGlob.world[worldIndex];
    w->firstbody = 0;
    w->firstjoint = 0;
    w->nb = 0;
    w->nj = 0;
    dSetZero(w->stepInfo.gravity, 4);
    w->stepInfo.global_erp = REAL(0.2);
#if defined(dSINGLE)
    w->stepInfo.global_cfm = 1e-5f;
#elif defined(dDOUBLE)
    w->global_cfm = 1e-10;
#else
#error dSINGLE or dDOUBLE must be defined
#endif

    w->adis.linear_threshold = REAL(0.001) * REAL(0.001);	// (magnitude squared)
    w->adis.angular_threshold = REAL(0.001) * REAL(0.001);	// (magnitude squared)
    w->adis.idle_steps = 10;
    w->adis.idle_time = 0;
    w->adis_flag = 0;

    w->stepInfo.qs.num_iterations = 20;
    w->stepInfo.qs.w = REAL(1.3);

    w->stepInfo.holdrand = 0x89ABCDEF;

    w->stepInfo.contactp.max_vel = dInfinity;
    w->stepInfo.contactp.min_depth = 0;

    return w;
}

// LWSS END

// MOD

void dJointGroupEmpty(dJointGroupID group)
{
    int i;

    dAASSERT(group);
    dAASSERT(group->num < ODE_WORLD_MAX_JOINT_COUNT);

    for (i = group->num - 1; i >= 0; --i)
    {
        if (group->joints[i].world)
        {
            removeJointReferencesFromAttachedBodies(&group->joints[i]);
            removeObjectFromList(&group->joints[i]);
            --group->joints[i].world->nj;
        }
    }

    group->num = 0;
}

void dWorldDestroy(dxWorld* w)
{
    // delete all bodies and joints
    dAASSERT(w);
    
    dxJoint* joint; // [esp+0h] [ebp-10h]
    dxBody* b; // [esp+4h] [ebp-Ch]
    dxJoint* nextj; // [esp+8h] [ebp-8h]
    dxBody* nextb; // [esp+Ch] [ebp-4h]

    b = w->firstbody;
#ifdef USE_POOL_ALLOCATOR
    bool bodyPoolFreesValid = true;
#endif
    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    while (b)
    {
        nextb = (dxBody*)b->next;
#ifdef USE_POOL_ALLOCATOR
        const bool bodyFreed = Pool_Free(
            ODE_BodyPoolStorage(), &odeGlob.bodyPool, b);
        if (!bodyFreed)
            bodyPoolFreesValid = false;
#else
        delete b;
#endif
        b = nextb;
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
#ifdef USE_POOL_ALLOCATOR
    if (!bodyPoolFreesValid)
        MyAssertHandler(
            __FILE__,
            __LINE__,
            0,
            "%s",
            "body pool frees succeeded");
#endif
    for (joint = w->firstjoint; joint; joint = nextj)
    {
        nextj = (dxJoint*)joint->next;
        if (joint->flags & dJOINT_INGROUP)
        {
            joint->world = 0;
            joint->node[0].body = 0;
            joint->node[0].next = 0;
            joint->node[1].body = 0;
            joint->node[1].next = 0;
            dMessage(0, "warning: destroying world containing grouped joints");
        }
    }
}


static bool ODE_NoReportWorldBodyListIsValid(
    dxWorld *const world,
    dxBody *const target) noexcept
{
    bool knownWorld = false;
    for (int index = 0; index < 3; ++index)
    {
        if (world == &odeGlob.world[index])
        {
            knownWorld = true;
            break;
        }
    }
    if (!knownWorld || world->nb < 0 || world->nb > 512
        || Pool_TryValidateFullNoReport(
               ODE_BodyPoolStorage(), &odeGlob.bodyPool)
            != poolmutationstatus_t::Success)
    {
        return false;
    }

    bool foundTarget = false;
    dObject **expectedBacklink =
        reinterpret_cast<dObject **>(&world->firstbody);
    dxBody *candidate = world->firstbody;
    int visited = 0;
    while (candidate)
    {
        if (visited >= world->nb || visited >= 512
            || Pool_TryValidateAllocatedNoReport(
                   ODE_BodyPoolStorage(), &odeGlob.bodyPool, candidate)
                != poolmutationstatus_t::Success
            || candidate->world != world
            || candidate->tome != expectedBacklink)
        {
            return false;
        }
        if (candidate == target)
            foundTarget = true;
        expectedBacklink = &candidate->next;
        candidate = static_cast<dxBody *>(candidate->next);
        ++visited;
    }
    return visited == world->nb && (!target || foundTarget);
}

template <typename JointType, int Count>
static bool ODE_NoReportTryGetJointArrayIndex(
    const PhysStaticArray<JointType, Count> &array,
    const dxJoint *const joint,
    std::size_t *const outIndex) noexcept
{
    const std::uintptr_t begin =
        reinterpret_cast<std::uintptr_t>(&array.entries[0]);
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(joint);
    const std::uintptr_t end = begin + sizeof(array.entries);
    if (address < begin || address >= end
        || (address - begin) % sizeof(JointType) != 0)
    {
        return false;
    }
    *outIndex = static_cast<std::size_t>(address - begin)
        / sizeof(JointType);
    return true;
}

constexpr std::size_t ODE_NO_REPORT_JOINT_HASH_SIZE = 8192;
static_assert(
    ODE_NO_REPORT_JOINT_HASH_SIZE
        >= static_cast<std::size_t>(ODE_WORLD_MAX_JOINT_COUNT) * 2,
    "silent joint hash must remain below 50 percent occupancy");
static_assert(
    (ODE_NO_REPORT_JOINT_HASH_SIZE
        & (ODE_NO_REPORT_JOINT_HASH_SIZE - 1)) == 0,
    "silent joint hash size must be a power of two");

struct ODENoReportJointValidationWorkspace
{
    bool hingeAllocated[192];
    bool ballAllocated[160];
    bool aMotorAllocated[160];
    bool bodyInWorld[512];
    dxJoint *jointKeys[ODE_NO_REPORT_JOINT_HASH_SIZE];
    std::uint16_t jointValues[ODE_NO_REPORT_JOINT_HASH_SIZE];
    std::uint8_t nodeMembership[ODE_WORLD_MAX_JOINT_COUNT][2];
};

// ODE_TryBodyDestroyNoReport requires the documented CRITSECT_PHYSICS lock.
// Keeping this bounded scratch state out of the legacy thread's small stack
// also avoids the prior repeated-list validation blowup at the 4096-joint
// limit.
static ODENoReportJointValidationWorkspace odeNoReportJointWorkspace{};

template <typename JointType, int Count>
static bool ODE_NoReportBuildJointAllocationMask(
    const PhysStaticArray<JointType, Count> &array,
    bool (&allocated)[Count]) noexcept
{
    for (bool &slotAllocated : allocated)
        slotAllocated = true;
    int freeIndex = array.freeEntry;
    while (freeIndex != -1)
    {
        if (freeIndex < 0 || freeIndex >= Count || !allocated[freeIndex])
            return false;
        allocated[freeIndex] = false;
        int nextFree = -1;
        std::memcpy(&nextFree, &array.entries[freeIndex], sizeof(nextFree));
        freeIndex = nextFree;
    }
    return true;
}

template <typename JointType, int Count>
static bool ODE_NoReportJointIsAllocatedArrayElement(
    const PhysStaticArray<JointType, Count> &array,
    const bool (&allocated)[Count],
    const dxJoint *const joint) noexcept
{
    std::size_t index = 0;
    if (!ODE_NoReportTryGetJointArrayIndex(array, joint, &index))
        return false;
    return allocated[index];
}

static bool ODE_NoReportContactJointIsActive(
    const dxJoint *const joint) noexcept
{
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(joint);
    for (int worldIndex = 0; worldIndex < 3; ++worldIndex)
    {
        const dxJointGroup &group = odeGlob.contactsGroup[worldIndex];
        if (group.num < 0 || group.num > ODE_WORLD_MAX_JOINT_COUNT)
            return false;
        const std::uintptr_t begin =
            reinterpret_cast<std::uintptr_t>(&group.joints[0]);
        const std::uintptr_t end = begin
            + static_cast<std::size_t>(group.num) * sizeof(group.joints[0]);
        if (address >= begin && address < end
            && (address - begin) % sizeof(group.joints[0]) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool ODE_NoReportJointPointerIsKnown(
    const ODENoReportJointValidationWorkspace &workspace,
    const dxJoint *const joint) noexcept
{
    return joint
        && (ODE_NoReportJointIsAllocatedArrayElement(
                physGlob.hingeArray, workspace.hingeAllocated, joint)
            || ODE_NoReportJointIsAllocatedArrayElement(
                physGlob.ballArray, workspace.ballAllocated, joint)
            || ODE_NoReportJointIsAllocatedArrayElement(
                physGlob.aMotorArray, workspace.aMotorAllocated, joint)
            || ODE_NoReportContactJointIsActive(joint));
}

static std::size_t ODE_NoReportJointHash(
    const dxJoint *const joint) noexcept
{
    return (reinterpret_cast<std::uintptr_t>(joint) >> 4)
        & (ODE_NO_REPORT_JOINT_HASH_SIZE - 1);
}

static bool ODE_NoReportInsertJoint(
    ODENoReportJointValidationWorkspace &workspace,
    dxJoint *const joint,
    const std::uint16_t worldListIndex) noexcept
{
    std::size_t hash = ODE_NoReportJointHash(joint);
    for (std::size_t probe = 0;
         probe < ODE_NO_REPORT_JOINT_HASH_SIZE;
         ++probe)
    {
        dxJoint *&key = workspace.jointKeys[hash];
        if (!key)
        {
            key = joint;
            workspace.jointValues[hash] = worldListIndex;
            return true;
        }
        if (key == joint)
            return false;
        hash = (hash + 1) & (ODE_NO_REPORT_JOINT_HASH_SIZE - 1);
    }
    return false;
}

static int ODE_NoReportFindJointIndex(
    const ODENoReportJointValidationWorkspace &workspace,
    const dxJoint *const joint) noexcept
{
    std::size_t hash = ODE_NoReportJointHash(joint);
    for (std::size_t probe = 0;
         probe < ODE_NO_REPORT_JOINT_HASH_SIZE;
         ++probe)
    {
        dxJoint *const key = workspace.jointKeys[hash];
        if (!key)
            return -1;
        if (key == joint)
            return workspace.jointValues[hash];
        hash = (hash + 1) & (ODE_NO_REPORT_JOINT_HASH_SIZE - 1);
    }
    return -1;
}

template <typename JointType, int Count>
static bool ODE_NoReportResolveJointArrayNode(
    const PhysStaticArray<JointType, Count> &array,
    const bool (&allocated)[Count],
    const dxJointNode *const node,
    dxJoint **const outJoint,
    int *const outNodeIndex) noexcept
{
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(node);
    for (int nodeIndex = 0; nodeIndex < 2; ++nodeIndex)
    {
        const std::uintptr_t firstNode = reinterpret_cast<std::uintptr_t>(
            &array.entries[0].node[nodeIndex]);
        if (address < firstNode)
            continue;
        const std::uintptr_t delta = address - firstNode;
        if (delta % sizeof(JointType) != 0)
            continue;
        const std::size_t entryIndex =
            static_cast<std::size_t>(delta / sizeof(JointType));
        if (entryIndex >= static_cast<std::size_t>(Count))
            continue;
        auto *const joint = const_cast<JointType *>(
            &array.entries[entryIndex]);
        if (!allocated[entryIndex])
            return false;
        *outJoint = joint;
        *outNodeIndex = nodeIndex;
        return true;
    }
    return false;
}

static bool ODE_NoReportResolveKnownJointNode(
    const ODENoReportJointValidationWorkspace &workspace,
    const dxJointNode *const node,
    dxJoint **const outJoint,
    int *const outNodeIndex) noexcept
{
    if (!node || !outJoint || !outNodeIndex)
        return false;
    if (ODE_NoReportResolveJointArrayNode(
            physGlob.hingeArray,
            workspace.hingeAllocated,
            node,
            outJoint,
            outNodeIndex)
        || ODE_NoReportResolveJointArrayNode(
            physGlob.ballArray,
            workspace.ballAllocated,
            node,
            outJoint,
            outNodeIndex)
        || ODE_NoReportResolveJointArrayNode(
            physGlob.aMotorArray,
            workspace.aMotorAllocated,
            node,
            outJoint,
            outNodeIndex))
    {
        return true;
    }

    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(node);
    for (int worldIndex = 0; worldIndex < 3; ++worldIndex)
    {
        const dxJointGroup &group = odeGlob.contactsGroup[worldIndex];
        if (group.num < 0 || group.num > ODE_WORLD_MAX_JOINT_COUNT)
            return false;
        for (int nodeIndex = 0; nodeIndex < 2; ++nodeIndex)
        {
            const std::uintptr_t firstNode =
                reinterpret_cast<std::uintptr_t>(
                    &group.joints[0].node[nodeIndex]);
            if (address < firstNode)
                continue;
            const std::uintptr_t delta = address - firstNode;
            if (delta % sizeof(group.joints[0]) != 0)
                continue;
            const std::size_t entryIndex =
                static_cast<std::size_t>(delta / sizeof(group.joints[0]));
            if (entryIndex >= static_cast<std::size_t>(group.num))
                continue;
            *outJoint = const_cast<dxJointContact *>(
                &group.joints[entryIndex]);
            *outNodeIndex = nodeIndex;
            return true;
        }
    }
    return false;
}

static bool ODE_NoReportTryGetBodyPoolIndex(
    const dxBody *const body,
    std::size_t *const outIndex) noexcept
{
    if (!body || !outIndex)
        return false;
    const poolstorage_t storage = ODE_BodyPoolStorage();
    if (!storage.base || !storage.control
        || storage.itemSize != sizeof(dxBody) || storage.itemCount != 512)
    {
        return false;
    }
    const std::uintptr_t begin =
        reinterpret_cast<std::uintptr_t>(storage.base);
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(body);
    const std::size_t byteCount = storage.itemSize * storage.itemCount;
    if (address < begin || address >= begin + byteCount
        || (address - begin) % storage.itemSize != 0)
    {
        return false;
    }
    *outIndex = static_cast<std::size_t>(address - begin)
        / storage.itemSize;
    return storage.control->slotState[*outIndex] == POOL_SLOT_ALLOCATED;
}

static bool ODE_NoReportGlobalBodyListsAreValid() noexcept
{
    const poolstorage_t storage = ODE_BodyPoolStorage();
    if (storage.itemCount != 512
        || Pool_TryValidateFullNoReport(storage, &odeGlob.bodyPool)
            != poolmutationstatus_t::Success)
    {
        return false;
    }

    bool bodySeen[512]{};
    const std::uintptr_t begin =
        reinterpret_cast<std::uintptr_t>(storage.base);
    for (int worldIndex = 0; worldIndex < 3; ++worldIndex)
    {
        dxWorld *const world = &odeGlob.world[worldIndex];
        if (!ODE_NoReportWorldBodyListIsValid(world))
            return false;
        for (dxBody *body = world->firstbody; body;
             body = static_cast<dxBody *>(body->next))
        {
            const std::uintptr_t address =
                reinterpret_cast<std::uintptr_t>(body);
            const std::size_t bodyIndex = static_cast<std::size_t>(
                (address - begin) / storage.itemSize);
            if (bodyIndex >= storage.itemCount || bodySeen[bodyIndex])
                return false;
            bodySeen[bodyIndex] = true;
        }
    }
    for (std::size_t index = 0; index < storage.itemCount; ++index)
    {
        const bool allocated = storage.control->slotState[index]
            == POOL_SLOT_ALLOCATED;
        if (allocated != bodySeen[index])
            return false;
    }
    return true;
}

static bool ODE_NoReportBodyAllocationCandidateHasNoAliases(
    dxBody *const candidate) noexcept
{
    const poolstorage_t bodyStorage = ODE_BodyPoolStorage();
    const std::uintptr_t bodyBegin =
        reinterpret_cast<std::uintptr_t>(bodyStorage.base);
    const std::uintptr_t candidateAddress =
        reinterpret_cast<std::uintptr_t>(candidate);
    const std::size_t bodyByteCount =
        bodyStorage.itemSize * bodyStorage.itemCount;
    if (!candidate || candidate != odeGlob.bodyPool.firstFree
        || candidateAddress < bodyBegin
        || candidateAddress >= bodyBegin + bodyByteCount
        || (candidateAddress - bodyBegin) % bodyStorage.itemSize != 0)
    {
        return false;
    }
    const std::size_t candidateIndex =
        static_cast<std::size_t>(candidateAddress - bodyBegin)
        / bodyStorage.itemSize;
    if (bodyStorage.control->headIndex != candidateIndex
        || bodyStorage.control->slotState[candidateIndex]
            == POOL_SLOT_ALLOCATED
        || !ODE_TryValidateGlobalGeomListsNoReport())
    {
        return false;
    }

    const poolstorage_t geomStorage = ODE_GeomPoolStorage();
    const auto *const geomBytes =
        static_cast<const unsigned char *>(geomStorage.base);
    for (std::size_t index = 0; index < geomStorage.itemCount; ++index)
    {
        if (geomStorage.control->slotState[index] != POOL_SLOT_ALLOCATED)
            continue;
        const auto *const geom = reinterpret_cast<const dxGeom *>(
            geomBytes + index * geomStorage.itemSize);
        if (geom->body == candidate
            || geom->pos == candidate->info.pos
            || geom->R == candidate->info.R)
        {
            return false;
        }
    }

    bool hingeAllocated[192];
    bool ballAllocated[160];
    bool aMotorAllocated[160];
    if (!ODE_NoReportBuildJointAllocationMask(
            physGlob.hingeArray, hingeAllocated)
        || !ODE_NoReportBuildJointAllocationMask(
            physGlob.ballArray, ballAllocated)
        || !ODE_NoReportBuildJointAllocationMask(
            physGlob.aMotorArray, aMotorAllocated))
    {
        return false;
    }
    const auto aliasesCandidate = [candidate](const dxJoint &joint) {
        return joint.node[0].body == candidate
            || joint.node[1].body == candidate;
    };
    for (std::size_t index = 0; index < 192; ++index)
    {
        if (hingeAllocated[index]
            && aliasesCandidate(physGlob.hingeArray.entries[index]))
        {
            return false;
        }
    }
    for (std::size_t index = 0; index < 160; ++index)
    {
        if ((ballAllocated[index]
                && aliasesCandidate(physGlob.ballArray.entries[index]))
            || (aMotorAllocated[index]
                && aliasesCandidate(physGlob.aMotorArray.entries[index])))
        {
            return false;
        }
    }
    for (int worldIndex = 0; worldIndex < 3; ++worldIndex)
    {
        const dxJointGroup &group = odeGlob.contactsGroup[worldIndex];
        if (group.num < 0 || group.num > ODE_WORLD_MAX_JOINT_COUNT)
            return false;
        for (int index = 0; index < group.num; ++index)
        {
            if (aliasesCandidate(group.joints[index]))
                return false;
        }
    }
    return true;
}

static bool ODE_NoReportIndexWorldJoints(
    dxWorld *const world,
    ODENoReportJointValidationWorkspace &workspace) noexcept
{
    int visited = 0;
    dObject **expectedBacklink =
        reinterpret_cast<dObject **>(&world->firstjoint);
    dxJoint *joint = world->firstjoint;
    while (joint)
    {
        if (visited >= world->nj || visited >= ODE_WORLD_MAX_JOINT_COUNT
            || !ODE_NoReportJointPointerIsKnown(workspace, joint)
            || !ODE_NoReportInsertJoint(
                workspace, joint, static_cast<std::uint16_t>(visited))
            || joint->world != world || joint->tome != expectedBacklink)
        {
            return false;
        }
        expectedBacklink = &joint->next;
        joint = static_cast<dxJoint *>(joint->next);
        ++visited;
    }
    return visited == world->nj;
}

static bool ODE_NoReportIndexBodyJointList(
    dxBody *const body,
    ODENoReportJointValidationWorkspace &workspace,
    std::size_t *const totalNodeCount) noexcept
{
    std::size_t visited = 0;
    dxJointNode *node = body->firstjoint;
    while (node)
    {
        dxJoint *joint = nullptr;
        int nodeIndex = -1;
        if (visited >= static_cast<std::size_t>(body->world->nj)
            || visited >= ODE_WORLD_MAX_JOINT_COUNT
            || !ODE_NoReportResolveKnownJointNode(
                workspace, node, &joint, &nodeIndex)
            || node->joint != joint || joint->world != body->world)
        {
            return false;
        }

        const int jointIndex = ODE_NoReportFindJointIndex(workspace, joint);
        if (jointIndex < 0
            || workspace.nodeMembership[jointIndex][nodeIndex] != 0
            || *totalNodeCount
                >= static_cast<std::size_t>(body->world->nj) * 2)
        {
            return false;
        }
        workspace.nodeMembership[jointIndex][nodeIndex] = 1;
        ++*totalNodeCount;

        const int oppositeIndex = 1 - nodeIndex;
        if (joint->node[oppositeIndex].body != body)
            return false;
        if (node->body)
        {
            std::size_t otherBodyIndex = 0;
            if (node->body == body
                || !ODE_NoReportTryGetBodyPoolIndex(
                    node->body, &otherBodyIndex)
                || !workspace.bodyInWorld[otherBodyIndex])
            {
                return false;
            }
        }
        node = node->next;
        ++visited;
    }
    return true;
}

static bool ODE_NoReportBodyHasValidJointList(
    dxBody *const body) noexcept
{
    dxWorld *const world = body->world;
    if (!world || world->nj < 0 || world->nj > ODE_WORLD_MAX_JOINT_COUNT
        || !ODE_NoReportWorldBodyListIsValid(world))
    {
        return false;
    }

    auto &workspace = odeNoReportJointWorkspace;
    std::memset(&workspace, 0, sizeof(workspace));
    if (!ODE_NoReportBuildJointAllocationMask(
            physGlob.hingeArray, workspace.hingeAllocated)
        || !ODE_NoReportBuildJointAllocationMask(
            physGlob.ballArray, workspace.ballAllocated)
        || !ODE_NoReportBuildJointAllocationMask(
            physGlob.aMotorArray, workspace.aMotorAllocated))
    {
        return false;
    }
    for (int worldIndex = 0; worldIndex < 3; ++worldIndex)
    {
        if (odeGlob.contactsGroup[worldIndex].num < 0
            || odeGlob.contactsGroup[worldIndex].num
                > ODE_WORLD_MAX_JOINT_COUNT)
        {
            return false;
        }
    }
    if (!ODE_NoReportIndexWorldJoints(world, workspace))
        return false;

    for (dxBody *candidateBody = world->firstbody; candidateBody;
         candidateBody = static_cast<dxBody *>(candidateBody->next))
    {
        std::size_t bodyIndex = 0;
        if (!ODE_NoReportTryGetBodyPoolIndex(candidateBody, &bodyIndex)
            || workspace.bodyInWorld[bodyIndex])
        {
            return false;
        }
        workspace.bodyInWorld[bodyIndex] = true;
    }

    std::size_t totalNodeCount = 0;
    for (dxBody *candidateBody = world->firstbody; candidateBody;
         candidateBody = static_cast<dxBody *>(candidateBody->next))
    {
        if (!ODE_NoReportIndexBodyJointList(
                candidateBody, workspace, &totalNodeCount))
        {
            return false;
        }
    }

    for (dxJoint *joint = world->firstjoint; joint;
         joint = static_cast<dxJoint *>(joint->next))
    {
        if (joint->node[0].joint != joint
            || joint->node[1].joint != joint
            || (joint->node[0].body
                && joint->node[0].body == joint->node[1].body))
        {
            return false;
        }
        const int jointIndex = ODE_NoReportFindJointIndex(workspace, joint);
        if (jointIndex < 0)
            return false;
        for (int nodeIndex = 0; nodeIndex < 2; ++nodeIndex)
        {
            dxBody *const attachedBody = joint->node[nodeIndex].body;
            const std::uint8_t expectedMembership = attachedBody ? 1 : 0;
            if (workspace.nodeMembership[jointIndex][1 - nodeIndex]
                != expectedMembership)
            {
                return false;
            }
            if (attachedBody)
            {
                std::size_t bodyIndex = 0;
                if (!ODE_NoReportTryGetBodyPoolIndex(
                        attachedBody, &bodyIndex)
                    || !workspace.bodyInWorld[bodyIndex])
                {
                    return false;
                }
            }
        }
    }

    const auto aliasesTarget = [body, &workspace](const dxJoint &joint) {
        return ODE_NoReportFindJointIndex(workspace, &joint) < 0
            && (joint.node[0].body == body || joint.node[1].body == body);
    };
    for (std::size_t index = 0; index < 192; ++index)
    {
        if (workspace.hingeAllocated[index]
            && aliasesTarget(physGlob.hingeArray.entries[index]))
            return false;
    }
    for (std::size_t index = 0; index < 160; ++index)
    {
        if ((workspace.ballAllocated[index]
                && aliasesTarget(physGlob.ballArray.entries[index]))
            || (workspace.aMotorAllocated[index]
                && aliasesTarget(physGlob.aMotorArray.entries[index])))
        {
            return false;
        }
    }
    for (int worldIndex = 0; worldIndex < 3; ++worldIndex)
    {
        const dxJointGroup &group = odeGlob.contactsGroup[worldIndex];
        for (int index = 0; index < group.num; ++index)
        {
            if (aliasesTarget(group.joints[index]))
                return false;
        }
    }
    return true;
}

static bool ODE_NoReportBodyCleanupOwnsGeom(
    const dxBody *const body,
    const dxGeom *const target) noexcept
{
    for (dxGeom *root = body->geom; root; root = root->body_next)
    {
        dxGeom *geom = root;
        for (std::size_t depth = 0;
             geom && depth <= 8;
             ++depth)
        {
            if (geom == target)
                return true;
            if (geom->type != dGeomTransformClass)
                break;
            auto *const transform = static_cast<dxGeomTransform *>(geom);
            if (!transform->cleanup)
                break;
            geom = transform->obj;
        }
    }
    return false;
}

static bool ODE_ValidateBodyDestroyNoReport(
    dxBody *const body) noexcept
{
    if (!body
        || Pool_TryValidateAllocatedNoReport(
               ODE_BodyPoolStorage(), &odeGlob.bodyPool, body)
            != poolmutationstatus_t::Success
        || !ODE_NoReportWorldBodyListIsValid(body->world, body)
        || !ODE_NoReportBodyHasValidJointList(body)
        || !ODE_TryValidateGlobalGeomListsNoReport())
    {
        return false;
    }

    dxGeom *geom = body->geom;
    for (std::size_t visited = 0; geom; ++visited)
    {
        if (visited >= ODE_GEOM_POOL_COUNT
            || !ODE_TryValidateGeomDestructNoReport(geom)
            || geom->body != body)
        {
            return false;
        }
        geom = geom->body_next;
    }

    const poolstorage_t geomStorage = ODE_GeomPoolStorage();
    const auto *const geomBytes =
        static_cast<const unsigned char *>(geomStorage.base);
    for (std::size_t index = 0; index < geomStorage.itemCount; ++index)
    {
        if (geomStorage.control->slotState[index] != POOL_SLOT_ALLOCATED)
            continue;
        const auto *const candidate = reinterpret_cast<const dxGeom *>(
            geomBytes + index * geomStorage.itemSize);
        if ((candidate->body == body
                || candidate->pos == body->info.pos
                || candidate->R == body->info.R)
            && !ODE_NoReportBodyCleanupOwnsGeom(body, candidate))
        {
            return false;
        }
    }
    return true;
}

bool ODE_TryValidateBodyDestroyNoReport(
    dxBody *const body) noexcept
{
    return ODE_ValidateBodyDestroyNoReport(body);
}

void dBodyDestroy(dxBody* b)
{
    dAASSERT(b);

    // all geoms that link to this body must be notified that the body is about
    // to disappear. note that the call to dGeomSetBody(geom,0) will result in
    // dGeomGetBodyNext() returning 0 for the body, so we must get the next body
    // before setting the body to 0.
    dxGeom* next_geom = nullptr;
    for (dxGeom* geom = b->geom; geom; geom = next_geom) {
        next_geom = dGeomGetBodyNext(geom);
        ODE_GeomDestruct(geom);
    }

    // detach all neighbouring joints, then delete this body.
    dxJointNode* n = b->firstjoint;
    while (n) {
        // sneaky trick to speed up removal of joint references (black magic)
        n->joint->node[(n == n->joint->node)].body = 0;

        dxJointNode* next = n->next;
        n->next = 0;
        removeJointReferencesFromAttachedBodies(n->joint);
        n = next;
    }
    removeObjectFromList(b);

    dAASSERT(b->world);
    dAASSERT(b->world->nb);

    b->world->nb--;

#ifdef USE_POOL_ALLOCATOR
    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    const bool bodyFreed = Pool_Free(
        ODE_BodyPoolStorage(), &odeGlob.bodyPool, b);
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
    if (!bodyFreed)
        MyAssertHandler(
            __FILE__,
            __LINE__,
            0,
            "%s",
            "body pool free succeeded");
#else
    delete b;
#endif
}

odebodycleanupstatus_t ODE_TryBodyDestroyNoReport(
    dxBody *const body) noexcept
{
    if (!ODE_ValidateBodyDestroyNoReport(body))
        return odebodycleanupstatus_t::InvalidArgument;

    while (body->geom)
    {
        if (ODE_TryGeomDestructNoReport(body->geom)
            != odegeomcleanupstatus_t::Success)
        {
            return odebodycleanupstatus_t::GeometryCleanupFailed;
        }
    }

    dxJointNode *node = body->firstjoint;
    while (node)
    {
        node->joint->node[(node == node->joint->node)].body = nullptr;
        dxJointNode *const next = node->next;
        node->next = nullptr;
        removeJointReferencesFromAttachedBodies(node->joint);
        node = next;
    }
    removeObjectFromList(body);
    --body->world->nb;

#ifdef USE_POOL_ALLOCATOR
    return Pool_TryFreeNoReport(
               ODE_BodyPoolStorage(), &odeGlob.bodyPool, body)
            == poolmutationstatus_t::Success
        ? odebodycleanupstatus_t::Success
        : odebodycleanupstatus_t::BodyPoolStateInvalid;
#else
    delete body;
    return odebodycleanupstatus_t::Success;
#endif
}
