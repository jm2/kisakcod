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

internal data structures and functions for collision detection.

*/

#ifndef _ODE_COLLISION_KERNEL_H_
#define _ODE_COLLISION_KERNEL_H_

#include <ode/common.h>
#include <ode/contact.h>
#include <ode/collision.h>
#include "objects.h"
#include "user_geom_storage.h"
#include <universal/assertive.h>
#include <universal/pool_allocator.h>

#include <new>

//****************************************************************************
// constants and macros

// mask for the number-of-contacts field in the dCollide() flags parameter
#define NUMC_MASK (0xffff)

#define IS_SPACE(geom) \
  ((geom)->type >= dFirstSpaceClass && (geom)->type <= dLastSpaceClass)

//****************************************************************************
// geometry object base class

// position vector and rotation matrix for geometry objects that are not
// connected to bodies.

struct dxPosR {
  dVector3 pos;
  dMatrix3 R;
};

// Selects the constructor variants used by the PHYSICS-locked transaction
// path.  Those variants initialize only object-local state; publication into
// the body and space lists is performed separately after complete topology
// validation.
struct ode_no_report_init_t
{
};
inline constexpr ode_no_report_init_t ODE_NO_REPORT_INIT{};


// geom flags.
//
// GEOM_DIRTY means that the space data structures for this geom are
// potentially not up to date. NOTE THAT all space parents of a dirty geom
// are themselves dirty. this is an invariant that must be enforced.
//
// GEOM_AABB_BAD means that the cached AABB for this geom is not up to date.
// note that GEOM_DIRTY does not imply GEOM_AABB_BAD, as the geom might
// recalculate its own AABB but does not know how to update the space data
// structures for the space it is in. but GEOM_AABB_BAD implies GEOM_DIRTY.
// the valid combinations are: 0, GEOM_DIRTY, GEOM_DIRTY|GEOM_AABB_BAD.

enum {
  GEOM_DIRTY	= 1,	// geom is 'dirty', i.e. position unknown
  GEOM_AABB_BAD	= 2,	// geom's AABB is not valid
  GEOM_PLACEABLE = 4,	// geom is placeable
  GEOM_ENABLED = 8,		// geom is enabled

  // Ray specific
  RAY_FIRSTCONTACT = 0x10000,
  RAY_BACKFACECULL = 0x20000,
  RAY_CLOSEST_HIT  = 0x40000
};


// geometry object base class. pos and R will either point to a separately
// allocated buffer (if body is 0 - pos points to the dxPosR object) or to
// the pos and R of the body (if body nonzero).
// a dGeomID is a pointer to this object.

struct dxGeom : public dBase {
  int type;		// geom type number, set by subclass constructor
  int gflags;		// flags used by geom and space
  void *data;		// user-defined data pointer
  dBodyID body;		// dynamics body associated with this object (if any)
  dxGeom *body_next;	// next geom in body's linked list of associated geoms
  dReal *pos;		// pointer to object's position vector
  dReal *R;		// pointer to object's rotation matrix

  // information used by spaces
  dxGeom *next;		// next geom in linked list of geoms
  dxGeom **tome;	// linked list backpointer
  dxSpace *parent_space;// the space this geom is contained in, 0 if none
  dReal aabb[6];	// cached AABB for this space
  unsigned long category_bits,collide_bits;

  dxGeom (dSpaceID _space, int is_placeable, dxBody *new_body); // MOD
  dxGeom (ode_no_report_init_t, int is_placeable) noexcept;
  virtual ~dxGeom() = default; // LWSS: make default

  virtual void computeAABB()=0;
  // compute the AABB for this object and put it in aabb. this function
  // always performs a fresh computation, it does not inspect the
  // GEOM_AABB_BAD flag.

  virtual int AABBTest (dxGeom *o, dReal aabb[6]);
  // test whether the given AABB object intersects with this object, return
  // 1=yes, 0=no. this is used as an early-exit test in the space collision
  // functions. the default implementation returns 1, which is the correct
  // behavior if no more detailed implementation can be provided.

  // utility functions

  // compute the AABB only if it is not current. this function manipulates
  // the GEOM_AABB_BAD flag.

  void recomputeAABB() {
    if (gflags & GEOM_AABB_BAD) {
      computeAABB();
      gflags &= ~GEOM_AABB_BAD;
    }
  }

  // add and remove this geom from a linked list maintained by a space.

  void spaceAdd (dxGeom **first_ptr) {
    next = *first_ptr;
    tome = first_ptr;
    if (*first_ptr) (*first_ptr)->tome = &next;
    *first_ptr = this;
  }
  void spaceRemove() {
    if (next) next->tome = tome;
    *tome = next;
  }

  // add and remove this geom from a linked list maintained by a body.

  void bodyAdd (dxBody *b) {
    body = b;
    body_next = b->geom;
    b->geom = this;
  }
  void bodyRemove();
};

//****************************************************************************
// the base space class
//
// the contained geoms are divided into two kinds: clean and dirty.
// the clean geoms have not moved since they were put in the list,
// and their AABBs are valid. the dirty geoms have changed position, and
// their AABBs are may not be valid. the two types are distinguished by the
// GEOM_DIRTY flag. all dirty geoms come *before* all clean geoms in the list.

struct dxSpace : public dxGeom {
  int count;			// number of geoms in this space
  dxGeom *first;		// first geom in list
  int cleanup;			// cleanup mode, 1=destroy geoms on exit

  // cached state for getGeom()
  int current_index;		// only valid if current_geom != 0
  dxGeom *current_geom;		// if 0 then there is no information

  // locking stuff. the space is locked when it is currently traversing its
  // internal data structures, e.g. in collide() and collide2(). operations
  // that modify the contents of the space are not permitted when the space
  // is locked.
  int lock_count;

  dxSpace (dSpaceID _space);
  ~dxSpace();

  // ADD
  void clear();

  void computeAABB() override;

  void setCleanup (int mode);
  int getCleanup();
  int query (dxGeom *geom);
  int getNumGeoms();
  virtual dxGeom *getGeom (int i);

  virtual void add (dxGeom *);
  virtual void remove (dxGeom *);
  virtual void dirty (dxGeom *);

  virtual void cleanGeoms()=0;
  // turn all dirty geoms into clean geoms by computing their AABBs and any
  // other space data structures that are required. this should clear the
  // GEOM_DIRTY and GEOM_AABB_BAD flags of all geoms.

  virtual void collide (void *data, dNearCallback *callback)=0;
  virtual void collide2 (void *data, dxGeom *geom, dNearCallback *callback)=0;
};

// ADD - move these out of collision_space.cpp and collision_kernel.cpp
struct dxSimpleSpace : public dxSpace {
    dxSimpleSpace(dSpaceID _space);

    // LWSS ADD - HACK for re-construction
    inline void ReInit()
    {
        new (this) dxSimpleSpace();
    }
    // LWSS END

    // ADDITION
    dxSimpleSpace() : dxSimpleSpace(nullptr) { }

    void cleanGeoms();
    void collide(void* data, dNearCallback* callback);
    void collide2(void* data, dxGeom* geom, dNearCallback* callback);
};

struct dxUserGeom : public dxGeom {
    alignas(physics::ode::kUserGeomClassDataAlignment)
        unsigned char user_data[physics::ode::kUserGeomClassDataBytes]; // MOD

    dxUserGeom(int class_num = dFirstUserClass, dxSpace *space = nullptr, dxBody *body = nullptr); // MOD
    dxUserGeom(ode_no_report_init_t, int class_num) noexcept;

    void ReInit(int class_num, dxSpace *space, dxBody *body)
    {
        new (this) dxUserGeom(class_num, space, body);
    }

    //dxUserGeom() : dxUserGeom(0, nullptr, nullptr) { } // ADD

    virtual ~dxUserGeom() = default; // LWSS: Make default
    void computeAABB() override;
    int AABBTest(dxGeom* o, dReal aabb[6]) override;
};
// END

// LWSS ADD- Custom for COD4
void __cdecl ODE_GeomGetAAContainedBox(struct dxGeomTransform *geom, float *mins, float *maxs);
void __cdecl dInitUserGeom(dxUserGeom *geom, int classnum, dxSpace *space, dxBody *body);

inline dxGeom *__cdecl ODE_BodyGetFirstGeom(dxBody *body)
{
    if (!body)
        MyAssertHandler(".\\physics\\ode\\src\\collision_kernel.cpp", 310, 0, "%s", "body");
    return body->geom;
}

// expose this
struct dxGeomTransform : public dxGeom {
    dxGeom* obj;		// object that is being transformed
    int cleanup;		// 1 to destroy obj when destroyed
    int infomode;		// 1 to put Tx geom in dContactGeom g1

    // cached final object transform (body tx + relative tx). this is set by
    // computeAABB(), and it is valid while the AABB is valid.
    //dVector3 final_pos;
    //dMatrix3 final_R;
    dMatrix3 localR;
    dVector3 localPos;
    dMatrix3 finalR;
    dVector3 finalPos;

    dxGeomTransform(dSpaceID space, dxBody* body); // MOD
    explicit dxGeomTransform(ode_no_report_init_t) noexcept;
    ~dxGeomTransform();
    void computeAABB() override;
    void computeFinalTx();
    void Destruct();
};

static_assert(
    sizeof(dxUserGeom) <= sizeof(dxGeomTransform),
    "ODE geom-pool slots must hold the native-width user-geom payload");
static_assert(
    alignof(dxUserGeom) <= alignof(dxGeomTransform),
    "ODE geom-pool slots must satisfy native user-geom alignment");

dxGeom *ODE_CreateGeom(int classnum, dxSpace *space, dxBody *body);
dxGeom *ODE_AllocateGeom();
void ODE_GeomDestruct(dxGeom* g);
enum class odegeomcleanupstatus_t : std::uint8_t
{
    Success,
    InvalidArgument,
    NestedCleanupFailed,
    PoolStateInvalid,
};
// Silent geometry mutations are lower-layer pieces of PHYSICS-locked
// transactions. Callers must hold CRITSECT_PHYSICS across preflight, mutation,
// and any rollback so the validated fixed-pool topology cannot change between
// those phases.
[[nodiscard]] poolmutationstatus_t ODE_TryAllocateGeomNoReport(
    dxGeom **outGeom) noexcept;
[[nodiscard]] poolmutationstatus_t ODE_TryCreateGeomNoReport(
    int classnum,
    dxSpace *space,
    dxBody *body,
    dxGeom **outGeom) noexcept;
[[nodiscard]] poolmutationstatus_t ODE_TryAttachGeomNoReport(
    dxGeom *geom,
    dxSpace *space,
    dxBody *body) noexcept;
[[nodiscard]] bool ODE_TryValidateGeomAttachmentNoReport(
    dxSpace *space,
    dxBody *body) noexcept;
[[nodiscard]] bool ODE_TryValidateGeomDestructNoReport(
    dxGeom *geom) noexcept;
[[nodiscard]] bool ODE_TryValidateGlobalGeomListsNoReport() noexcept;
[[nodiscard]] odegeomcleanupstatus_t ODE_TryGeomDestructNoReport(
    dxGeom *geom) noexcept;
// LWSS END
#endif
