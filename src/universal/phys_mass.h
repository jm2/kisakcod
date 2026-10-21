#pragma once

// PhysMass: the centre of mass and the inertia tensor, 36 bytes at both
// widths. It lives here, not in physics/phys_local.h, because the dynamic
// entity def a headless TU loads and hashes holds it by value.
struct PhysMass // sizeof=0x24
{                                       // ...
    float centerOfMass[3];
    float momentsOfInertia[3];
    float productsOfInertia[3];
};
