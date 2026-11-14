// Copyright 2020-2026 The Defold Foundation
// Copyright 2014-2020 King
// Copyright 2009-2014 Ragnar Svensson, Christian Murray
// SPDX-License-Identifier: MIT
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef MOONBASE_SIMULATION_H
#define MOONBASE_SIMULATION_H

#include <dmsdk/datastore/datastore.h>

namespace Moon
{
    const uint32_t MAX_UNITS = 262144;
    const uint32_t GRID_SIDE = 48;
    const uint32_t RELAY_COUNT = 5;
    const float MAP_HALF = 768.0f;
    const float VIEW_SIN = 0.70710678118f;
    const uint32_t NO_TARGET = UINT32_MAX;

    enum UnitKind
    {
        SCOUT, ASSAULT, SIEGE, SUPPORT, KIND_COUNT
    };
    struct UnitSpec
    {
        const char* m_Name;
        float       m_Health, m_Speed, m_Range, m_Damage, m_Reload, m_Radius;
    };
    extern const UnitSpec SPECS[KIND_COUNT];

    struct Unit
    {
        DataStoreVector3 m_Position;
        float            m_GoalX, m_GoalZ, m_DirectionX, m_DirectionZ;
        float            m_Health, m_Cooldown, m_Flash, m_Respawn;
        uint32_t         m_Index, m_Team, m_Kind, m_Target, m_Order, m_Selected, m_Fired;
    };
    struct Relay
    {
        float    m_X, m_Z, m_Control;
        int32_t  m_Owner;
        uint32_t m_Presence[2];
    };
    struct Obstacle
    {
        float m_X, m_Z, m_Radius, m_Height;
    };
    extern const Obstacle OBSTACLES[];
    extern const uint32_t OBSTACLE_COUNT;

    struct Camera
    {
        float m_X, m_Z, m_Span;
        float m_Width, m_Height;
    };
    struct Statistics
    {
        uint32_t m_Alive[2], m_Selected, m_Kills[2], m_Relays[2];
        uint32_t m_SelectedKinds[KIND_COUNT];
        float    m_BaseHealth[2], m_SimulationMs;
    };
    struct Battle
    {
        HDataStoreContext m_Store; // Borrowed; all callbacks/queries removed by Destroy.
        HDataStoreQuery   m_Query;
        Unit*             m_Snapshot; // Owned, indexed by immutable Unit::m_Index; never store pointers.
        float*            m_Damage;
        int32_t*          m_Next;
        int32_t           m_Heads[GRID_SIDE * GRID_SIDE];
        int32_t*          m_TargetNext;
        int32_t           m_TargetHeads[2][GRID_SIDE * GRID_SIDE];
        Relay             m_Relays[RELAY_COUNT];
        Camera            m_Camera;
        Statistics        m_Stats;
        uint32_t          m_Count, m_Tick, m_SelectedCount;
        uint64_t          m_StepStart;
        float             m_Dt, m_Time, m_CommandX, m_CommandZ, m_CommandAge;
        int32_t           m_Winner;
        bool              m_Autoplay, m_Paused, m_AttackMove;
    };

    // Create owns its storage; the supplied store must outlive Destroy.
    Battle* Create(HDataStoreContext store, uint32_t count);
    void    Destroy(Battle* battle);
    bool    Reset(Battle* battle, uint32_t count);
    void    SetDeltaTime(Battle* battle, float dt);
    void    ScreenToGround(const Camera* camera, float sx, float sy, float* x, float* z);
    void    WorldToScreen(const Camera* camera, float x, float y, float z, float* sx, float* sy);
    void    Select(Battle* battle, float x0, float y0, float x1, float y1, bool additive, int32_t kind);
    void    Order(Battle* battle, float x, float z, bool attack_move);
    void    Stop(Battle* battle);
    bool    IsWalkable(float x, float z, float margin);
}
#endif
