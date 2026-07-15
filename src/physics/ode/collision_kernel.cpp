/*************************************************************************
 *                                                                       *
 * Open Dynamics Engine, Copyright (C) 2001-2003 Russell L. Smith.       *
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

/*

core collision functions and data structures, plus part of the public API
for geometry objects

*/

#include <ode/common.h>
#include <ode/matrix.h>
#include <ode/rotation.h>
#include <ode/objects.h>
#include "collision_kernel.h"
#include "collision_util.h"
#include "collision_std.h"
#include "collision_transform.h"
#include "collision_trimesh_internal.h"

#ifdef _MSC_VER
#pragma warning(disable:4291)  // for VC++, no complaints about "no matching operator delete found"
#endif
#include <universal/assertive.h>

#include <cstring>
#include <cstdlib>
#include <new>

//****************************************************************************
// helper functions for dCollide()ing a space with another geom

// this struct records the parameters passed to dCollideSpaceGeom()

struct SpaceGeomColliderData {
  int flags;			// space left in contacts array
  dContactGeom *contact;
  int skip;
};


static void space_geom_collider (void *data, dxGeom *o1, dxGeom *o2)
{
  SpaceGeomColliderData *d = (SpaceGeomColliderData*) data;
  if (d->flags & NUMC_MASK) {
    int n = dCollide (o1,o2,d->flags,d->contact,d->skip);
    d->contact = CONTACT (d->contact,d->skip*n);
    d->flags -= n;
  }
}


static int dCollideSpaceGeom (dxGeom *o1, dxGeom *o2, int flags,
			      dContactGeom *contact, int skip)
{
  SpaceGeomColliderData data;
  data.flags = flags;
  data.contact = contact;
  data.skip = skip;
  dSpaceCollide2 (o1,o2,&data,&space_geom_collider);
  return (flags & NUMC_MASK) - (data.flags & NUMC_MASK);
}

//****************************************************************************
// dispatcher for the N^2 collider functions

// function pointers and modes for n^2 class collider functions

struct dColliderEntry {
  dColliderFn *fn;	// collider function, 0 = no function available
  int reverse;		// 1 = reverse o1 and o2
};
static dColliderEntry colliders[dGeomNumClasses][dGeomNumClasses];
static int colliders_initialized = 0;


// setCollider() will refuse to write over a collider entry once it has
// been written.

static void setCollider (int i, int j, dColliderFn *fn)
{
  if (colliders[i][j].fn == 0) {
    colliders[i][j].fn = fn;
    colliders[i][j].reverse = 0;
  }
  if (colliders[j][i].fn == 0) {
    colliders[j][i].fn = fn;
    colliders[j][i].reverse = 1;
  }
}


static void setAllColliders (int i, dColliderFn *fn)
{
  for (int j=0; j<dGeomNumClasses; j++) setCollider (i,j,fn);
}


static void initColliders()
{
  int i,j;

  if (colliders_initialized) return;
  colliders_initialized = 1;

  memset (colliders,0,sizeof(colliders));

  // setup space colliders
  for (i=dFirstSpaceClass; i <= dLastSpaceClass; i++) {
    for (j=0; j < dGeomNumClasses; j++) {
      setCollider (i,j,&dCollideSpaceGeom);
    }
  }

  setCollider (dSphereClass,dSphereClass,&dCollideSphereSphere);
  setCollider (dSphereClass,dBoxClass,&dCollideSphereBox);
  setCollider (dSphereClass,dPlaneClass,&dCollideSpherePlane);
  setCollider (dBoxClass,dBoxClass,&dCollideBoxBox);
  setCollider (dBoxClass,dPlaneClass,&dCollideBoxPlane);
  setCollider (dCCylinderClass,dSphereClass,&dCollideCCylinderSphere);
  setCollider (dCCylinderClass,dBoxClass,&dCollideCCylinderBox);
  setCollider (dCCylinderClass,dCCylinderClass,&dCollideCCylinderCCylinder);
  setCollider (dCCylinderClass,dPlaneClass,&dCollideCCylinderPlane);
  setCollider (dRayClass,dSphereClass,&dCollideRaySphere);
  setCollider (dRayClass,dBoxClass,&dCollideRayBox);
  setCollider (dRayClass,dCCylinderClass,&dCollideRayCCylinder);
  setCollider (dRayClass,dPlaneClass,&dCollideRayPlane);
#ifdef dTRIMESH_ENABLED
  setCollider (dTriMeshClass,dSphereClass,&dCollideSTL);
  setCollider (dTriMeshClass,dBoxClass,&dCollideBTL);
  setCollider (dTriMeshClass,dRayClass,&dCollideRTL);
  setCollider (dTriMeshClass,dTriMeshClass,&dCollideTTL);
  setCollider (dTriMeshClass,dCCylinderClass,&dCollideCCTL);
#endif
  setAllColliders (dGeomTransformClass,&dCollideTransform);
}


int dCollide (dxGeom *o1, dxGeom *o2, int flags, dContactGeom *contactArray, int skip)
{
    dxGeom **p_g2; // [esp+0h] [ebp-24h]
    dxGeom **p_g1; // [esp+4h] [ebp-20h]
    dxGeom *v8;
  dAASSERT(o1 && o2 && contactArray);
  dUASSERT(colliders_initialized,"colliders array not initialized");
  dUASSERT(o1->type >= 0 && o1->type < dGeomNumClasses,"bad o1 class number");
  dUASSERT(o2->type >= 0 && o2->type < dGeomNumClasses,"bad o2 class number");

  // no contacts if both geoms are the same
  if (o1 == o2) return 0;

  // no contacts if both geoms on the same body, and the body is not 0
  if (o1->body == o2->body && o1->body) return 0;

  dColliderEntry *ce = &colliders[o1->type][o2->type];
  int count = 0;
  if (ce->fn) {
    if (ce->reverse) {
      count = (*ce->fn) (o2,o1,flags,contactArray,skip);
      for (int i=0; i<count; i++) {
	dContactGeom *c = CONTACT(contactArray,skip*i);
	c->normal[0] = -c->normal[0];
	c->normal[1] = -c->normal[1];
	c->normal[2] = -c->normal[2];
    p_g2 = &c->g2;
    p_g1 = &c->g1;
    if (&c->g1 != &c->g2)
    {
        v8 = *p_g1;
        *p_g1 = *p_g2;
        *p_g2 = v8;
    }
      }
    }
    else {
      count = (*ce->fn) (o1,o2,flags,contactArray,skip);
    }
  }
  return count;
}

//****************************************************************************
// dxGeom

dxGeom::dxGeom (dSpaceID _space, int is_placeable, dxBody *new_body)
{
  initColliders();

  // setup body vars. invalid type of -1 must be changed by the constructor.
  type = -1;
  gflags = GEOM_DIRTY | GEOM_AABB_BAD | GEOM_ENABLED;
  if (is_placeable) gflags |= GEOM_PLACEABLE;
  data = 0;
  body = 0;
  body_next = 0;

  // MOD

  // setup space vars
  next = 0;
  tome = 0;
  parent_space = 0;
  dSetZero (aabb,6);
  category_bits = ~0;
  collide_bits = ~0;

  // put this geom in a space if required
  if (_space) dSpaceAdd (_space,this);

  // ADD
  if (is_placeable) {
      dUASSERT(new_body, "new_body");
      dGeomSetBody(this, new_body);
  }
  else {
      dUASSERT(!new_body, "!new_body");
      pos = 0;
      R = 0;
  }
}

dxGeom::dxGeom(
    const ode_no_report_init_t,
    const int is_placeable) noexcept
{
  initColliders();
  type = -1;
  gflags = GEOM_DIRTY | GEOM_AABB_BAD | GEOM_ENABLED;
  if (is_placeable) gflags |= GEOM_PLACEABLE;
  data = nullptr;
  body = nullptr;
  body_next = nullptr;
  pos = nullptr;
  R = nullptr;
  next = nullptr;
  tome = nullptr;
  parent_space = nullptr;
  for (dReal &bound : aabb) bound = 0;
  category_bits = ~0UL;
  collide_bits = ~0UL;
}


//dxGeom::~dxGeom()
//{
//    dUASSERT(false, "unsupported operation");
//  // if (parent_space) dSpaceRemove (parent_space,this);
//  // if ((gflags & GEOM_PLACEABLE) && !body) dFree (pos,sizeof(dxPosR));
//  // bodyRemove();
//}


int dxGeom::AABBTest (dxGeom *o, dReal aabb[6])
{
  return 1;
}


void dxGeom::bodyRemove()
{
  if (body) {
    // delete this geom from body list
    dxGeom **last = &body->geom, *g = body->geom;
    while (g) {
      if (g == this) {
	*last = g->body_next;
	break;
      }
      last = &g->body_next;
      g = g->body_next;
    }
    body = 0;
    body_next = 0;
  }
}

//****************************************************************************
// misc

dxGeom *dGeomGetBodyNext (dxGeom *geom)
{
  return geom->body_next;
}

//****************************************************************************
// public API for geometry objects

#define CHECK_NOT_LOCKED(space) \
  dUASSERT (!(space && space->lock_count), \
	    "invalid operation for geom in locked space");


/* void dGeomDestroy(dxGeom *g)
{
  dAASSERT (g);
  delete g;
}
*/


void dGeomSetData (dxGeom *g, void *data)
{
  dAASSERT (g);
  g->data = data;
}


void *dGeomGetData (dxGeom *g)
{
  dAASSERT (g);
  return g->data;
}


// MOD
void dGeomSetBody (dxGeom *g, dxBody *body)
{
  dAASSERT (g);
  dAASSERT (body);
  dUASSERT (g->gflags & GEOM_PLACEABLE,"geom must be placeable");
  CHECK_NOT_LOCKED (g->parent_space);

  g->pos = body->info.pos;
  g->R = body->info.R;
  dGeomMoved(g);
  if (g->body != body) {
      g->bodyRemove();
      g->bodyAdd(body);
  }
}


dBodyID dGeomGetBody (dxGeom *g)
{
  dAASSERT (g);
  return g->body;
}


void dGeomSetPosition (dxGeom *g, dReal x, dReal y, dReal z)
{
  dAASSERT (g);
  dUASSERT (g->gflags & GEOM_PLACEABLE,"geom must be placeable");
  CHECK_NOT_LOCKED (g->parent_space);
  if (g->body) {
    // this will call dGeomMoved (g), so we don't have to
    dBodySetPosition (g->body,x,y,z);
  }
  else {
    g->pos[0] = x;
    g->pos[1] = y;
    g->pos[2] = z;
    dGeomMoved (g);
  }
}


void dGeomSetRotation (dxGeom *g, const dMatrix3 R)
{
  dAASSERT (g && R);
  dUASSERT (g->gflags & GEOM_PLACEABLE,"geom must be placeable");
  CHECK_NOT_LOCKED (g->parent_space);
  if (g->body) {
    // this will call dGeomMoved (g), so we don't have to
    dBodySetRotation (g->body,R);
  }
  else {
    memcpy (g->R,R,sizeof(dMatrix3));
    dGeomMoved (g);
  }
}


void dGeomSetQuaternion (dxGeom *g, const dQuaternion quat)
{
  dAASSERT (g && quat);
  dUASSERT (g->gflags & GEOM_PLACEABLE,"geom must be placeable");
  CHECK_NOT_LOCKED (g->parent_space);
  if (g->body) {
    // this will call dGeomMoved (g), so we don't have to
    dBodySetQuaternion (g->body,quat);
  }
  else {
    dQtoR (quat, g->R);
    dGeomMoved (g);
  }
}


const dReal * dGeomGetPosition (dxGeom *g)
{
  dAASSERT (g);
  dUASSERT (g->gflags & GEOM_PLACEABLE,"geom must be placeable");
  return g->pos;
}


const dReal * dGeomGetRotation (dxGeom *g)
{
  dAASSERT (g);
  dUASSERT (g->gflags & GEOM_PLACEABLE,"geom must be placeable");
  return g->R;
}


void dGeomGetQuaternion (dxGeom *g, dQuaternion quat)
{
  dAASSERT (g);
  dUASSERT (g->gflags & GEOM_PLACEABLE,"geom must be placeable");
  if (g->body) {
    const dReal * body_quat = dBodyGetQuaternion (g->body);
    quat[0] = body_quat[0];
    quat[1] = body_quat[1];
    quat[2] = body_quat[2];
    quat[3] = body_quat[3];
  }
  else {
    dRtoQ (g->R, quat);
  }
}


void dGeomGetAABB (dxGeom *g, dReal aabb[6])
{
  dAASSERT (g);
  dAASSERT (aabb);
  g->recomputeAABB();
  memcpy (aabb,g->aabb,6 * sizeof(dReal));
}


int dGeomIsSpace (dxGeom *g)
{
  dAASSERT (g);
  return IS_SPACE(g);
}


dSpaceID dGeomGetSpace (dxGeom *g)
{
  dAASSERT (g);
  return g->parent_space;
}


int dGeomGetClass (dxGeom *g)
{
  dAASSERT (g);
  return g->type;
}


void dGeomSetCategoryBits (dxGeom *g, unsigned long bits)
{
  dAASSERT (g);
  CHECK_NOT_LOCKED (g->parent_space);
  g->category_bits = bits;
}


void dGeomSetCollideBits (dxGeom *g, unsigned long bits)
{
  dAASSERT (g);
  CHECK_NOT_LOCKED (g->parent_space);
  g->collide_bits = bits;
}


unsigned long dGeomGetCategoryBits (dxGeom *g)
{
  dAASSERT (g);
  return g->category_bits;
}


unsigned long dGeomGetCollideBits (dxGeom *g)
{
  dAASSERT (g);
  return g->collide_bits;
}


void dGeomEnable (dxGeom *g)
{
	dAASSERT (g);
	g->gflags |= GEOM_ENABLED;
}

void dGeomDisable (dxGeom *g)
{
	dAASSERT (g);
	g->gflags &= ~GEOM_ENABLED;
}

int dGeomIsEnabled (dxGeom *g)
{
	dAASSERT (g);
	return (g->gflags & GEOM_ENABLED) != 0;
}


//****************************************************************************
// C interface that lets the user make new classes. this interface is a lot
// more cumbersome than C++ subclassing, which is what is used internally
// in ODE. this API is mainly to support legacy code.

static int num_user_classes = 0;
static dGeomClass user_classes [dMaxUserClasses];


dxUserGeom::dxUserGeom(int class_num, dxSpace* space, dxBody* body) : 
    dxGeom (space, user_classes[class_num - dFirstUserClass].isPlaceable, body)
{
  iassert(class_num >= 11 && class_num <= 15);
  type = class_num;
  int size = user_classes[type-dFirstUserClass].bytes;
  dAASSERT(size <= sizeof(user_data));
  memset (user_data,0,size);
}

dxUserGeom::dxUserGeom(
    const ode_no_report_init_t init,
    const int class_num) noexcept
    : dxGeom(init, user_classes[class_num - dFirstUserClass].isPlaceable)
{
  type = class_num;
  std::memset(user_data, 0, sizeof(user_data));
}


//dxUserGeom::~dxUserGeom()
//{
//    //dAASSERT(false);
//   //dGeomClass *c = &user_classes[type-dFirstUserClass];
//   //if (c->dtor) c->dtor (this);
//}


void dxUserGeom::computeAABB()
{
  user_classes[type-dFirstUserClass].aabb (this,aabb);
}


int dxUserGeom::AABBTest (dxGeom *o, dReal aabb[6])
{
  dGeomClass *c = &user_classes[type-dFirstUserClass];
  if (c->aabb_test) return c->aabb_test (this,o,aabb);
  else return 1;
}


static int dCollideUserGeomWithGeom (dxGeom *o1, dxGeom *o2, int flags,
				     dContactGeom *contact, int skip)
{
  // this generic collider function is called the first time that a user class
  // tries to collide against something. it will find out the correct collider
  // function and then set the colliders array so that the correct function is
  // called directly the next time around.

  int t1 = o1->type;	// note that o1 is a user geom
  int t2 = o2->type;	// o2 *may* be a user geom

  // find the collider function to use. if o1 does not know how to collide with
  // o2, then o2 might know how to collide with o1 (provided that it is a user
  // geom).
  dColliderFn *fn = user_classes[t1-dFirstUserClass].collider (t2);
  int reverse = 0;
  if (!fn && t2 >= dFirstUserClass && t2 <= dLastUserClass) {
    fn = user_classes[t2-dFirstUserClass].collider (t1);
    reverse = 1;
  }

  // set the colliders array so that the correct function is called directly
  // the next time around. note that fn can be 0 here if no collider was found,
  // which means that dCollide() will always return 0 for this case.
  colliders[t1][t2].fn = fn;
  colliders[t1][t2].reverse = reverse;
  colliders[t2][t1].fn = fn;
  colliders[t2][t1].reverse = !reverse;

  // now call the collider function indirectly through dCollide(), so that
  // contact reversing is properly handled.
  return dCollide (o1,o2,flags,contact,skip);
}


int dCreateGeomClass (const dGeomClass *c)
{
  dUASSERT(c && c->bytes >= 0 && c->collider && c->aabb,"bad geom class");

  if (num_user_classes >= dMaxUserClasses) {
    dDebug (0,"too many user classes, you must increase the limit and "
	      "recompile ODE");
  }
  user_classes[num_user_classes] = *c;
  int class_number = num_user_classes + dFirstUserClass;
  initColliders();
  setAllColliders (class_number,&dCollideUserGeomWithGeom);

  num_user_classes++;
  return class_number;
}


void * dGeomGetClassData (dxGeom *g)
{
  dUASSERT (g && g->type >= dFirstUserClass &&
	    g->type <= dLastUserClass,"not a custom class");
  dxUserGeom *user = (dxUserGeom*) g;
  return user->user_data;
}

// REM
// dGeomID dCreateGeom (int classnum)
// {
//   dUASSERT (classnum >= dFirstUserClass &&
// 	    classnum <= dLastUserClass,"not a custom class");
//   return new dxUserGeom (classnum);
// }

//****************************************************************************
// here is where we deallocate any memory that has been globally
// allocated, or free other global resources.

void dCloseODE()
{
  colliders_initialized = 0;
  num_user_classes = 0;
}


// LWSS ADD - Custom for COD4
#include <win32/win_local.h> // lwss add
#include <universal/pool_allocator.h> // lwss add

#include "odeext.h"
#include <physics/phys_local.h>

void __cdecl dInitUserGeom(dxUserGeom *geom, int classnum, dxSpace *space, dxBody *body)
{
    if (!geom)
        MyAssertHandler(".\\physics\\ode\\src\\collision_kernel.cpp", 749, 0, "%s", "geom");
    if (classnum < 11 || classnum > 15)
        MyAssertHandler(
            ".\\physics\\ode\\src\\collision_kernel.cpp",
            750,
            0,
            "%s\n\t%s",
            "( classnum >= dFirstUserClass ) && ( classnum <= dLastUserClass )",
            "not a custom class");
    if (geom)
    {
        //dxUserGeom::dxUserGeom(geom, classnum, space, body);
        geom->ReInit(classnum, space, body);
    }
}

void __cdecl ODE_GeomTransformGetAAContainedBox(dxGeomTransform *geom, float *mins, float *maxs)
{
    if (geom->type != 6)
        MyAssertHandler(".\\physics\\ode\\src\\collision_transform.cpp", 105, 0, "%s", "geom->type == dGeomTransformClass");
    if (!geom->obj)
        MyAssertHandler(".\\physics\\ode\\src\\collision_transform.cpp", 109, 0, "%s", "tr->obj");
    if ((geom->gflags & 2) != 0)
    {
        //dxGeomTransform::computeFinalTx(geom);
        geom->computeFinalTx();
    }
    geom->obj->pos = geom->finalPos;
    geom->obj->R = geom->finalR;
    ODE_GeomGetAAContainedBox((dxGeomTransform*)geom->obj, mins, maxs);
}

void __cdecl ODE_GeomGetAAContainedBox(dxGeomTransform *geom, float *mins, float *maxs)
{
    const char *v3; // eax
    float lengths[4]; // [esp+1Ch] [ebp-14h] BYREF
    float minlength; // [esp+2Ch] [ebp-4h]

    switch (dGeomGetClass(geom))
    {
    case 1:
        dGeomBoxGetLengths(geom, lengths);
        minlength = lengths[0];
        if (lengths[1] < lengths[0])
            minlength = lengths[1];
        if (lengths[2] < minlength)
            minlength = lengths[2];
        if (minlength <= 0.0)
        {
            v3 = va("%f %f %f", lengths[0], lengths[1], lengths[2]);
            MyAssertHandler(".\\physics\\ode\\src\\collision_kernel.cpp", 637, 0, "%s\n\t%s", "minlength > 0", v3);
        }
        minlength = minlength * 0.5;
        *mins = -minlength;
        mins[1] = -minlength;
        mins[2] = -minlength;
        *maxs = minlength;
        maxs[1] = minlength;
        maxs[2] = minlength;
        break;
    case 6:
        ODE_GeomTransformGetAAContainedBox(geom, mins, maxs);
        break;
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
        Phys_GeomUserGetAAContainedBox(geom, mins, maxs);
        break;
    default:
        if (!alwaysfails)
            MyAssertHandler(".\\physics\\ode\\src\\collision_kernel.cpp", 657, 0, "Invalid geom");
        break;
    }
}

dxGeom *ODE_CreateGeom(int classnum, dxSpace *space, dxBody *body)
{
    dxUserGeom *geom;

    if (classnum < dFirstUserClass || classnum > dLastUserClass)
        MyAssertHandler(
            ".\\physics\\ode\\src\\collision_kernel.cpp",
            737,
            0,
            "%s\n\t%s",
            "( classnum >= dFirstUserClass ) && ( classnum <= dLastUserClass )",
            "not a custom class");
    
    geom = (dxUserGeom *)ODE_AllocateGeom();
    if (!geom)
        return nullptr;

    return new (geom) dxUserGeom(classnum, space, body);
}

namespace
{
constexpr std::size_t ODE_MAX_NESTED_TRANSFORM_DEPTH = 8;

int ODE_NoReportKnownWorldIndex(const dxWorld *const world) noexcept
{
    for (int index = 0; index < 3; ++index)
    {
        if (world == &odeGlob.world[index])
            return index;
    }
    return -1;
}

int ODE_NoReportKnownSpaceIndex(const dxSpace *const space) noexcept
{
    for (int index = 0; index < 3; ++index)
    {
        if (space == &odeGlob.space[index])
            return index;
    }
    return -1;
}

bool ODE_NoReportWorldBodyListIsValid(
    dxWorld *const world,
    dxBody *const target = nullptr) noexcept
{
    if (ODE_NoReportKnownWorldIndex(world) < 0
        || world->nb < 0 || world->nb > 512)
    {
        return false;
    }

    bool foundTarget = false;
    int visited = 0;
    dObject **expectedBacklink =
        reinterpret_cast<dObject **>(&world->firstbody);
    dxBody *candidate = world->firstbody;
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

bool ODE_NoReportSpaceListIsValid(
    dxSpace *const space,
    dxGeom *const target = nullptr) noexcept
{
    if (ODE_NoReportKnownSpaceIndex(space) < 0
        || space->type != dSimpleSpaceClass || space->parent_space
        || space->body || space->body_next || space->next || space->tome
        || space->lock_count != 0
        || space->count < 0
        || space->count > static_cast<int>(ODE_GEOM_POOL_COUNT))
    {
        return false;
    }

    bool foundTarget = false;
    bool foundCurrent = space->current_geom == nullptr;
    bool seenClean = false;
    int visited = 0;
    dxGeom **expectedBacklink = &space->first;
    dxGeom *geom = space->first;
    while (geom)
    {
        if (visited >= space->count
            || visited >= static_cast<int>(ODE_GEOM_POOL_COUNT)
            || Pool_TryValidateAllocatedNoReport(
                   ODE_GeomPoolStorage(), &odeGlob.geomPool, geom)
                != poolmutationstatus_t::Success
            || geom->parent_space != space
            || geom->tome != expectedBacklink)
        {
            return false;
        }
        if (geom == target)
            foundTarget = true;
        if ((geom->gflags & GEOM_DIRTY) == 0)
            seenClean = true;
        else if (seenClean)
            return false;
        if (geom == space->current_geom)
        {
            if (space->current_index != visited)
                return false;
            foundCurrent = true;
        }
        expectedBacklink = &geom->next;
        geom = geom->next;
        ++visited;
    }
    return visited == space->count && foundCurrent
        && (!target || foundTarget);
}

bool ODE_NoReportBodyGeomListIsValid(
    dxBody *const body,
    dxGeom *const target = nullptr) noexcept
{
    if (!body || !body->world)
        return false;

    bool foundTarget = false;
    std::size_t visited = 0;
    dxGeom *geom = body->geom;
    while (geom)
    {
        if (visited >= ODE_GEOM_POOL_COUNT
            || Pool_TryValidateAllocatedNoReport(
                   ODE_GeomPoolStorage(), &odeGlob.geomPool, geom)
                != poolmutationstatus_t::Success
            || geom->body != body)
        {
            return false;
        }
        if (geom == target)
            foundTarget = true;
        geom = geom->body_next;
        ++visited;
    }
    return !target || foundTarget;
}

bool ODE_NoReportTryGetGeomPoolIndex(
    const dxGeom *const geom,
    std::size_t *const outIndex) noexcept
{
    if (!geom || !outIndex)
        return false;
    const poolstorage_t storage = ODE_GeomPoolStorage();
    const std::uintptr_t begin =
        reinterpret_cast<std::uintptr_t>(storage.base);
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(geom);
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

bool ODE_IsNoReportPoolGeomType(const dxGeom *geom) noexcept;

bool ODE_NoReportGlobalGeomListsAreValid(
    dxGeom *const pendingAttachment = nullptr) noexcept
{
    const poolstorage_t bodyStorage = ODE_BodyPoolStorage();
    const poolstorage_t geomStorage = ODE_GeomPoolStorage();
    if (Pool_TryValidateFullNoReport(
            bodyStorage, &odeGlob.bodyPool)
            != poolmutationstatus_t::Success
        || Pool_TryValidateFullNoReport(
            geomStorage, &odeGlob.geomPool)
            != poolmutationstatus_t::Success)
    {
        return false;
    }

    std::size_t pendingIndex = geomStorage.itemCount;
    if (pendingAttachment
        && !ODE_NoReportTryGetGeomPoolIndex(
            pendingAttachment, &pendingIndex))
    {
        return false;
    }

    bool bodySeen[512]{};
    enum : std::uint8_t
    {
        ODE_GEOM_SEEN_IN_SPACE = 1,
        ODE_GEOM_SEEN_ON_BODY = 2,
        ODE_GEOM_REFERENCED_BY_TRANSFORM = 4,
        ODE_GEOM_REACHABLE_FROM_ROOT = 8,
    };
    std::uint8_t geomState[ODE_GEOM_POOL_COUNT]{};
    const std::uintptr_t bodyBase =
        reinterpret_cast<std::uintptr_t>(bodyStorage.base);
    for (int index = 0; index < 3; ++index)
    {
        dxWorld *const world = &odeGlob.world[index];
        if (!ODE_NoReportWorldBodyListIsValid(world)
            || !ODE_NoReportSpaceListIsValid(&odeGlob.space[index]))
        {
            return false;
        }
        for (dxGeom *geom = odeGlob.space[index].first; geom;
             geom = geom->next)
        {
            std::size_t geomIndex = 0;
            if (!ODE_NoReportTryGetGeomPoolIndex(geom, &geomIndex)
                || (geomState[geomIndex] & ODE_GEOM_SEEN_IN_SPACE))
            {
                return false;
            }
            geomState[geomIndex] |= ODE_GEOM_SEEN_IN_SPACE;
        }
        for (dxBody *body = world->firstbody; body;
             body = static_cast<dxBody *>(body->next))
        {
            const std::uintptr_t bodyAddress =
                reinterpret_cast<std::uintptr_t>(body);
            const std::size_t bodyIndex = static_cast<std::size_t>(
                (bodyAddress - bodyBase) / bodyStorage.itemSize);
            if (bodyIndex >= bodyStorage.itemCount || bodySeen[bodyIndex]
                || !ODE_NoReportBodyGeomListIsValid(body))
                return false;
            bodySeen[bodyIndex] = true;
            for (dxGeom *geom = body->geom; geom;
                 geom = geom->body_next)
            {
                std::size_t geomIndex = 0;
                if (!ODE_NoReportTryGetGeomPoolIndex(geom, &geomIndex)
                    || (geomState[geomIndex] & ODE_GEOM_SEEN_ON_BODY))
                {
                    return false;
                }
                geomState[geomIndex] |= ODE_GEOM_SEEN_ON_BODY;
            }
        }
    }
    for (std::size_t index = 0; index < bodyStorage.itemCount; ++index)
    {
        const bool allocated = bodyStorage.control->slotState[index]
            == POOL_SLOT_ALLOCATED;
        if (allocated != bodySeen[index])
            return false;
    }

    const auto *const geomBytes =
        static_cast<const unsigned char *>(geomStorage.base);
    for (std::size_t index = 0; index < geomStorage.itemCount; ++index)
    {
        if (geomStorage.control->slotState[index] != POOL_SLOT_ALLOCATED)
        {
            if (geomState[index] != 0)
                return false;
            continue;
        }

        auto *const geom = const_cast<dxGeom *>(
            reinterpret_cast<const dxGeom *>(
                geomBytes + index * geomStorage.itemSize));
        if (!ODE_IsNoReportPoolGeomType(geom))
            return false;

        const std::uint8_t rootMembership = geomState[index]
            & (ODE_GEOM_SEEN_IN_SPACE | ODE_GEOM_SEEN_ON_BODY);
        if (rootMembership != 0
            && rootMembership
                != (ODE_GEOM_SEEN_IN_SPACE | ODE_GEOM_SEEN_ON_BODY))
        {
            return false;
        }
        if (rootMembership != 0)
        {
            if (!geom->parent_space || !geom->body
                || geom->pos != geom->body->info.pos
                || geom->R != geom->body->info.R
                || ODE_NoReportKnownSpaceIndex(geom->parent_space)
                    != ODE_NoReportKnownWorldIndex(geom->body->world))
            {
                return false;
            }
        }
        else if (geom->parent_space || geom->body || geom->next
            || geom->tome || geom->body_next)
        {
            return false;
        }

        if (geom->type != dGeomTransformClass)
            continue;
        auto *const transform = static_cast<dxGeomTransform *>(geom);
        if ((transform->cleanup != 0 && transform->cleanup != 1)
            || !transform->obj)
        {
            if (transform->cleanup != 0 && transform->cleanup != 1)
                return false;
            continue;
        }
        std::size_t childIndex = 0;
        if (!ODE_NoReportTryGetGeomPoolIndex(
                transform->obj, &childIndex)
            || childIndex == index
            || (geomState[childIndex]
                & ODE_GEOM_REFERENCED_BY_TRANSFORM))
        {
            return false;
        }
        geomState[childIndex] |= ODE_GEOM_REFERENCED_BY_TRANSFORM;
    }

    for (std::size_t index = 0; index < geomStorage.itemCount; ++index)
    {
        if (geomStorage.control->slotState[index] != POOL_SLOT_ALLOCATED)
            continue;
        const bool isRoot = (geomState[index]
            & (ODE_GEOM_SEEN_IN_SPACE | ODE_GEOM_SEEN_ON_BODY)) != 0;
        const bool isReferenced = (geomState[index]
            & ODE_GEOM_REFERENCED_BY_TRANSFORM) != 0;
        if ((isRoot && isReferenced)
            || (!isRoot && !isReferenced && index != pendingIndex)
            || (index == pendingIndex && (isRoot || isReferenced)))
        {
            return false;
        }
    }

    // Unique incoming references make all transform trees disjoint. Walking
    // from each published root therefore both enforces the supported nesting
    // depth and rejects otherwise-unreachable transform cycles.
    for (std::size_t rootIndex = 0;
         rootIndex < geomStorage.itemCount;
         ++rootIndex)
    {
        const std::uint8_t rootMembership = geomState[rootIndex]
            & (ODE_GEOM_SEEN_IN_SPACE | ODE_GEOM_SEEN_ON_BODY);
        if (rootMembership == 0)
            continue;

        std::size_t currentIndex = rootIndex;
        for (std::size_t depth = 0;; ++depth)
        {
            if (depth > ODE_MAX_NESTED_TRANSFORM_DEPTH
                || (geomState[currentIndex]
                    & ODE_GEOM_REACHABLE_FROM_ROOT))
            {
                return false;
            }
            geomState[currentIndex] |= ODE_GEOM_REACHABLE_FROM_ROOT;
            auto *const geom = const_cast<dxGeom *>(
                reinterpret_cast<const dxGeom *>(
                    geomBytes + currentIndex * geomStorage.itemSize));
            if (geom->type != dGeomTransformClass
                || !static_cast<dxGeomTransform *>(geom)->obj)
            {
                break;
            }
            if (!ODE_NoReportTryGetGeomPoolIndex(
                    static_cast<dxGeomTransform *>(geom)->obj,
                    &currentIndex))
            {
                return false;
            }
        }
    }

    for (std::size_t index = 0; index < geomStorage.itemCount; ++index)
    {
        if (geomStorage.control->slotState[index] == POOL_SLOT_ALLOCATED
            && index != pendingIndex
            && !(geomState[index] & ODE_GEOM_REACHABLE_FROM_ROOT))
        {
            return false;
        }
    }
    return true;
}

bool ODE_IsNoReportPoolGeomType(const dxGeom *const geom) noexcept
{
    if (!geom)
        return false;
    if (geom->type == dBoxClass || geom->type == dGeomTransformClass)
        return true;
    if (geom->type < dFirstUserClass || geom->type > dLastUserClass)
        return false;
    const int classIndex = geom->type - dFirstUserClass;
    return classIndex < num_user_classes
        && classIndex < dMaxUserClasses
        && user_classes[classIndex].isPlaceable
        && user_classes[classIndex].bytes >= 0
        && static_cast<std::size_t>(user_classes[classIndex].bytes)
            <= physics::ode::kUserGeomClassDataBytes
        && user_classes[classIndex].collider
        && user_classes[classIndex].aabb;
}

bool ODE_NoReportAttachmentTargetsAreValid(
    dxSpace *const space,
    dxBody *const body,
    dxGeom *const pendingAttachment = nullptr) noexcept
{
    if (!space || !body
        || Pool_TryValidateAllocatedNoReport(
               ODE_BodyPoolStorage(), &odeGlob.bodyPool, body)
            != poolmutationstatus_t::Success)
    {
        return false;
    }
    const int worldIndex = ODE_NoReportKnownWorldIndex(body->world);
    const int spaceIndex = ODE_NoReportKnownSpaceIndex(space);
    return worldIndex >= 0 && worldIndex == spaceIndex
        && space->count < static_cast<int>(ODE_GEOM_POOL_COUNT)
        && ODE_NoReportGlobalGeomListsAreValid(pendingAttachment)
        && ODE_NoReportWorldBodyListIsValid(body->world, body)
        && ODE_NoReportSpaceListIsValid(space)
        && ODE_NoReportBodyGeomListIsValid(body);
}

void ODE_CommitAttachGeomNoReport(
    dxGeom *const geom,
    dxSpace *const space,
    dxBody *const body) noexcept
{
    geom->pos = body->info.pos;
    geom->R = body->info.R;
    geom->body = body;
    geom->body_next = body->geom;
    body->geom = geom;

    geom->parent_space = space;
    geom->next = space->first;
    geom->tome = &space->first;
    if (space->first)
        space->first->tome = &geom->next;
    space->first = geom;
    ++space->count;
    space->current_geom = nullptr;
    geom->gflags |= GEOM_DIRTY | GEOM_AABB_BAD;
    space->gflags |= GEOM_DIRTY | GEOM_AABB_BAD;
}

bool ODE_NoReportRootGeomTopologyIsValid(dxGeom *const geom) noexcept
{
    if (geom->parent_space)
    {
        if (!ODE_NoReportSpaceListIsValid(geom->parent_space, geom))
            return false;
    }
    else if (geom->next || geom->tome)
    {
        return false;
    }

    if (geom->body)
    {
        if (Pool_TryValidateAllocatedNoReport(
                ODE_BodyPoolStorage(), &odeGlob.bodyPool, geom->body)
                != poolmutationstatus_t::Success
            || !ODE_NoReportWorldBodyListIsValid(
                geom->body->world, geom->body)
            || !ODE_NoReportBodyGeomListIsValid(geom->body, geom))
        {
            return false;
        }
    }
    else if (geom->body_next)
    {
        return false;
    }

    if (geom->parent_space && geom->body
        && ODE_NoReportKnownSpaceIndex(geom->parent_space)
            != ODE_NoReportKnownWorldIndex(geom->body->world))
    {
        return false;
    }
    return true;
}

dxGeom *ODE_NoReportCleanupChild(dxGeom *const geom) noexcept
{
    if (geom->type != dGeomTransformClass)
        return nullptr;
    auto *const transform = static_cast<dxGeomTransform *>(geom);
    return transform->cleanup ? transform->obj : nullptr;
}

bool ODE_NoReportCleanupOwnershipIsUnique(dxGeom *const root) noexcept
{
    const poolstorage_t storage = ODE_GeomPoolStorage();
    if (Pool_TryValidateFullNoReport(storage, &odeGlob.geomPool)
        != poolmutationstatus_t::Success)
    {
        return false;
    }

    dxGeom *expectedOwner = nullptr;
    for (dxGeom *node = root; node;
         expectedOwner = node, node = ODE_NoReportCleanupChild(node))
    {
        std::size_t ownerCount = 0;
        bool expectedOwnerFound = false;
        for (std::size_t index = 0; index < storage.itemCount; ++index)
        {
            if (storage.control->slotState[index] != POOL_SLOT_ALLOCATED)
                continue;
            auto *const candidate = reinterpret_cast<dxGeom *>(
                static_cast<unsigned char *>(storage.base)
                + index * storage.itemSize);
            if (candidate->type != dGeomTransformClass)
                continue;
            auto *const transform =
                static_cast<dxGeomTransform *>(candidate);
            if (transform->obj != node)
                continue;
            ++ownerCount;
            if (candidate == expectedOwner)
                expectedOwnerFound = true;
        }

        if (!expectedOwner)
        {
            if (ownerCount != 0)
                return false;
        }
        else if (ownerCount != 1 || !expectedOwnerFound)
        {
            return false;
        }
    }
    return true;
}

bool ODE_ValidateGeomDestructNoReport(dxGeom *const root) noexcept
{
    if (!root)
        return false;
    dxGeom *geom = root;
    for (std::size_t depth = 0; geom; ++depth)
    {
        if (depth > ODE_MAX_NESTED_TRANSFORM_DEPTH
            || Pool_TryValidateAllocatedNoReport(
                   ODE_GeomPoolStorage(), &odeGlob.geomPool, geom)
                != poolmutationstatus_t::Success
            || !ODE_IsNoReportPoolGeomType(geom))
        {
            return false;
        }
        if (depth == 0)
        {
            if (!ODE_NoReportRootGeomTopologyIsValid(geom))
                return false;
        }
        else if (geom->parent_space || geom->body || geom->next
            || geom->tome || geom->body_next)
        {
            return false;
        }

        if (geom->type == dGeomTransformClass)
        {
            const auto *const transform =
                static_cast<const dxGeomTransform *>(geom);
            // A detached child would become an unreachable pool allocation if
            // its wrapper were silently destroyed without cleanup enabled.
            if (transform->obj && !transform->cleanup)
                return false;
        }
        dxGeom *const child = ODE_NoReportCleanupChild(geom);
        if (!child)
            break;
        if (Pool_TryValidateAllocatedNoReport(
                ODE_GeomPoolStorage(), &odeGlob.geomPool, child)
                != poolmutationstatus_t::Success)
        {
            return false;
        }

        dxGeom *ancestor = root;
        for (std::size_t ancestorDepth = 0;
             ancestorDepth <= depth;
             ++ancestorDepth)
        {
            if (ancestor == child)
                return false;
            ancestor = ODE_NoReportCleanupChild(ancestor);
        }
        geom = child;
    }
    return ODE_NoReportCleanupOwnershipIsUnique(root);
}

void ODE_CommitUnlinkGeomNoReport(dxGeom *const geom) noexcept
{
    if (geom->parent_space)
    {
        dxSpace *const space = geom->parent_space;
        if (geom->next)
            geom->next->tome = geom->tome;
        *geom->tome = geom->next;
        --space->count;
        space->current_geom = nullptr;
        space->gflags |= GEOM_DIRTY | GEOM_AABB_BAD;
        geom->next = nullptr;
        geom->tome = nullptr;
        geom->parent_space = nullptr;
    }

    if (geom->body)
    {
        dxBody *const body = geom->body;
        dxGeom **link = &body->geom;
        while (*link != geom)
            link = &(*link)->body_next;
        *link = geom->body_next;
        geom->body = nullptr;
        geom->body_next = nullptr;
    }
}

odegeomcleanupstatus_t ODE_CommitGeomDestructNoReport(
    dxGeom *const geom) noexcept
{
    ODE_CommitUnlinkGeomNoReport(geom);

    if (dxGeom *const child = ODE_NoReportCleanupChild(geom))
    {
        const odegeomcleanupstatus_t nestedStatus =
            ODE_CommitGeomDestructNoReport(child);
        if (nestedStatus != odegeomcleanupstatus_t::Success)
            return odegeomcleanupstatus_t::NestedCleanupFailed;
        static_cast<dxGeomTransform *>(geom)->obj = nullptr;
    }

#ifdef USE_POOL_ALLOCATOR
    return Pool_TryFreeNoReport(
               ODE_GeomPoolStorage(), &odeGlob.geomPool, geom)
            == poolmutationstatus_t::Success
        ? odegeomcleanupstatus_t::Success
        : odegeomcleanupstatus_t::PoolStateInvalid;
#else
    free(geom);
    return odegeomcleanupstatus_t::Success;
#endif
}
} // namespace

bool ODE_TryValidateGlobalGeomListsNoReport() noexcept
{
    return ODE_NoReportGlobalGeomListsAreValid();
}

bool ODE_TryValidateGeomAttachmentNoReport(
    dxSpace *const space,
    dxBody *const body) noexcept
{
    return ODE_NoReportAttachmentTargetsAreValid(space, body);
}

poolmutationstatus_t ODE_TryAttachGeomNoReport(
    dxGeom *const geom,
    dxSpace *const space,
    dxBody *const body) noexcept
{
    if (!geom
        || Pool_TryValidateAllocatedNoReport(
               ODE_GeomPoolStorage(), &odeGlob.geomPool, geom)
            != poolmutationstatus_t::Success
        || !ODE_IsNoReportPoolGeomType(geom)
        || !(geom->gflags & GEOM_PLACEABLE)
        || geom->parent_space || geom->body || geom->next || geom->tome
        || geom->body_next
        || !ODE_NoReportAttachmentTargetsAreValid(space, body, geom))
    {
        return poolmutationstatus_t::InvalidState;
    }

    ODE_CommitAttachGeomNoReport(geom, space, body);
    return poolmutationstatus_t::Success;
}

poolmutationstatus_t ODE_TryCreateGeomNoReport(
    const int classnum,
    dxSpace *const space,
    dxBody *const body,
    dxGeom **const outGeom) noexcept
{
    if (!outGeom)
        return poolmutationstatus_t::InvalidState;
    *outGeom = nullptr;

    if (classnum < dFirstUserClass || classnum > dLastUserClass)
        return poolmutationstatus_t::InvalidState;
    const int classIndex = classnum - dFirstUserClass;
    if (classIndex >= num_user_classes
        || classIndex >= dMaxUserClasses
        || !ODE_NoReportAttachmentTargetsAreValid(space, body))
    {
        return poolmutationstatus_t::InvalidState;
    }

    const dGeomClass &geomClass = user_classes[classIndex];
    if (!geomClass.isPlaceable || geomClass.bytes < 0
        || static_cast<std::size_t>(geomClass.bytes)
            > physics::ode::kUserGeomClassDataBytes
        || !geomClass.collider || !geomClass.aabb)
    {
        return poolmutationstatus_t::InvalidState;
    }

    dxGeom *storage = nullptr;
    const poolmutationstatus_t allocationStatus =
        ODE_TryAllocateGeomNoReport(&storage);
    if (allocationStatus != poolmutationstatus_t::Success)
        return allocationStatus;

    auto *const geom = new (storage) dxUserGeom(ODE_NO_REPORT_INIT, classnum);
    const poolmutationstatus_t attachStatus =
        ODE_TryAttachGeomNoReport(geom, space, body);
    if (attachStatus != poolmutationstatus_t::Success)
    {
        (void)ODE_TryGeomDestructNoReport(geom);
        return attachStatus;
    }

    *outGeom = geom;
    return poolmutationstatus_t::Success;
}

poolmutationstatus_t ODE_TryGeomTransformSetGeomNoReport(
    dxGeom *const transformGeom,
    dxGeom *const object) noexcept
{
    if (!transformGeom || !object || transformGeom == object
        || !ODE_NoReportGlobalGeomListsAreValid()
        || !ODE_ValidateGeomDestructNoReport(transformGeom)
        || !ODE_ValidateGeomDestructNoReport(object)
        || transformGeom->type != dGeomTransformClass)
    {
        return poolmutationstatus_t::InvalidState;
    }

    auto *const transform = static_cast<dxGeomTransform *>(transformGeom);
    if (transform->obj || !transformGeom->parent_space
        || !transformGeom->body
        || object->parent_space != transformGeom->parent_space
        || object->body != transformGeom->body)
    {
        return poolmutationstatus_t::InvalidState;
    }

    ODE_CommitUnlinkGeomNoReport(object);
    transform->obj = object;
    return poolmutationstatus_t::Success;
}

void dGeomFree(dxGeom* g)
{
    iassert(g);

    switch (g->type)
    {
    case dBoxClass:
    case 0xB: // user classes
    case 0xC:
    case 0xD:
    case 0xE:
        if (g->type < dFirstUserClass || user_classes[g->type - dFirstUserClass].isPlaceable)
        {
#ifdef USE_POOL_ALLOCATOR
            Sys_EnterCriticalSection(CRITSECT_PHYSICS);
            const bool geomFreed = Pool_Free(
                ODE_GeomPoolStorage(),
                &odeGlob.geomPool,
                g);
            Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
            if (!geomFreed)
                MyAssertHandler(
                    __FILE__,
                    __LINE__,
                    0,
                    "%s",
                    "geom pool free succeeded");
#else
            free(g);
#endif
        }
        break;
    case dGeomTransformClass: {
        static_cast<dxGeomTransform*>(g)->Destruct();
#ifdef USE_POOL_ALLOCATOR
        Sys_EnterCriticalSection(CRITSECT_PHYSICS);
        const bool geomFreed = Pool_Free(
            ODE_GeomPoolStorage(),
            &odeGlob.geomPool,
            g);
        Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
        if (!geomFreed)
            MyAssertHandler(
                __FILE__,
                __LINE__,
                0,
                "%s",
                "geom transform pool free succeeded");
#else
        free(g);
#endif
        break;
    }
    case dTriMeshClass:
        break;
    case dSimpleSpaceClass: {
        static_cast<dxSpace*>(g)->clear();
        break;
    }
    default:
        iassert(false);
        break;
    }
}

dxGeom* ODE_AllocateGeom()
{
#ifdef USE_POOL_ALLOCATOR
    dxGeom* geom;
    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    geom = static_cast<dxGeom *>(Pool_Alloc(
        ODE_GeomPoolStorage(), &odeGlob.geomPool));
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
    return geom;
#else
    return (dxGeom *)malloc(sizeof(dxGeomTransform));
#endif
}

poolmutationstatus_t ODE_TryAllocateGeomNoReport(
    dxGeom **const outGeom) noexcept
{
    if (!outGeom)
        return poolmutationstatus_t::InvalidState;
    *outGeom = nullptr;
#ifdef USE_POOL_ALLOCATOR
    if (!ODE_NoReportGlobalGeomListsAreValid())
    {
        return poolmutationstatus_t::InvalidState;
    }
    const poolallocresult_t allocation = Pool_TryAllocNoReport(
        ODE_GeomPoolStorage(), &odeGlob.geomPool);
    *outGeom = static_cast<dxGeom *>(allocation.item);
    return allocation.status;
#else
    *outGeom = static_cast<dxGeom *>(malloc(sizeof(dxGeomTransform)));
    return *outGeom
        ? poolmutationstatus_t::Success
        : poolmutationstatus_t::Unavailable;
#endif
}

void ODE_GeomDestruct(dxGeom* g)
{
    dAASSERT(g);
    if (g->parent_space)
        dSpaceRemove(g->parent_space, g);
    g->bodyRemove();
    dGeomFree(g);
}

odegeomcleanupstatus_t ODE_TryGeomDestructNoReport(
    dxGeom *const geom) noexcept
{
    if (!ODE_NoReportGlobalGeomListsAreValid()
        || !ODE_ValidateGeomDestructNoReport(geom))
        return odegeomcleanupstatus_t::InvalidArgument;
    return ODE_CommitGeomDestructNoReport(geom);
}

bool ODE_TryValidateGeomDestructNoReport(
    dxGeom *const geom) noexcept
{
    return ODE_NoReportGlobalGeomListsAreValid()
        && ODE_ValidateGeomDestructNoReport(geom);
}

// LWSS END
