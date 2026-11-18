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

#include "simulation.h"
#include <dmsdk/dlib/hash.h>
#include <dmsdk/dlib/time.h>
#include <dmsdk/dlib/log.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

namespace Moon
{
    const UnitSpec SPECS[KIND_COUNT] = {
        { "SCOUT", 50, 49, 35, 8, 0.55f, 1.4f },
        { "ASSAULT", 140, 29, 48, 19, 0.9f, 2.1f },
        { "SIEGE", 90, 19, 88, 44, 2.1f, 2.8f },
        { "SUPPORT", 75, 34, 42, -15, 0.8f, 1.8f }
    };
    const Obstacle OBSTACLES[] = {
        { -340, -200, 46, 28 }, { -340, 210, 55, 34 },
        { 340, -210, 55, 34 }, { 340, 200, 46, 28 },
        { -100, -190, 38, 23 }, { 100, 190, 38, 23 },
        { -95, 490, 65, 36 }, { 95, -490, 65, 36 }
    };
    const uint32_t OBSTACLE_COUNT = sizeof(OBSTACLES) / sizeof(OBSTACLES[0]);
    static const char* SYSTEM_NAMES[] = { "moon.grid", "moon.think", "moon.move", "moon.resolve" };
    static const float BASE_X[] = { -620, 620 };
    static const float BASE_Z[] = { 0, 0 };

    // Keep movement, camera projection and control values within their allowed range.
    static float Clamp(float v, float low, float high)
    {
        return v < low ? low : (v > high ? high : v);
    }

    // Compare distances and weapon ranges without taking square roots.
    static float Square(float v)
    {
        return v * v;
    }

    // Hash an index into a deterministic seed, independent of worker execution order.
    static uint32_t Mix(uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        return x ^ (x >> 16);
    }

    // Produce repeatable spawn variation without shared random-number state.
    static float Random01(uint32_t x)
    {
        return (Mix(x) & 65535) / 65535.0f;
    }

    // Map a world coordinate to a clamped cell in the spatial lookup grid.
    static int32_t Cell(float v)
    {
        return (int32_t)Clamp((v + MAP_HALF) / 32, 0, GRID_SIDE - 1);
    }

    // Read the horizontal world coordinate from the registered position field.
    static float X(const Unit& u)
    {
        return u.m_Position.m_Values[0];
    }

    // Read the ground-plane depth coordinate from the registered position field.
    static float Z(const Unit& u)
    {
        return u.m_Position.m_Values[2];
    }

    // Dead units retain their rows while waiting for reinforcements.
    static bool Alive(const Unit& u)
    {
        return u.m_Health > 0;
    }

    // Test map edges and circular blockers with space for the unit's radius.
    bool IsWalkable(float x, float z, float margin)
    {
        if (fabsf(x) > MAP_HALF - margin || fabsf(z) > MAP_HALF - margin)
        {
            return false;
        }

        for (uint32_t i = 0; i < OBSTACLE_COUNT; ++i)
        {
            if (Square(x - OBSTACLES[i].m_X) + Square(z - OBSTACLES[i].m_Z) < Square(OBSTACLES[i].m_Radius + margin))
            {
                return false;
            }
        }

        return true;
    }

    // Project a world point into the angled orthographic camera's screen coordinates.
    void WorldToScreen(const Camera* c, float x, float y, float z, float* sx, float* sy)
    {
        float scale = c->m_Height / c->m_Span;
        *sx = c->m_Width * 0.5f + (x - c->m_X) * scale;
        *sy = c->m_Height * 0.5f + (y - z + c->m_Z) * VIEW_SIN * scale;
    }

    // Invert the camera projection at ground height for selections and destinations.
    void ScreenToGround(const Camera* c, float sx, float sy, float* x, float* z)
    {
        float scale = c->m_Span / c->m_Height;
        *x = c->m_X + (sx - c->m_Width * 0.5f) * scale;
        *z = c->m_Z - (sy - c->m_Height * 0.5f) * scale / VIEW_SIN;
    }

    // Place an initial unit or reinforcement in its team's lanes, outside blockers.
    static void Spawn(Unit* u, bool initial)
    {
        const UnitSpec& spec = SPECS[u->m_Kind];
        uint32_t seed = u->m_Index * 137 + (initial ? 0 : 971);
        float sign = u->m_Team ? 1.0f : -1.0f;
        float lane = ((int32_t)(u->m_Index % 3) - 1) * 295.0f;
        float x = sign * (initial ? 140.0f + Random01(seed) * 460.0f : 560 + Random01(seed) * 110);
        float z = lane + (Random01(seed + 7) - 0.5f) * 165;
        for (uint32_t i = 0; i < OBSTACLE_COUNT; ++i)
        {
            float dx = x - OBSTACLES[i].m_X, dz = z - OBSTACLES[i].m_Z;
            float d = sqrtf(dx * dx + dz * dz), r = OBSTACLES[i].m_Radius + 5;
            if (d < r)
            {
                x = OBSTACLES[i].m_X + r * dx / (d + 0.001f);
                z = OBSTACLES[i].m_Z + r * dz / (d + 0.001f);
            }
        }

        u->m_Position.m_Values[0] = x;
        u->m_Position.m_Values[1] = 0;
        u->m_Position.m_Values[2] = z;
        u->m_GoalX = -sign * 620;
        u->m_GoalZ = 0;
        u->m_DirectionX = -sign;
        u->m_DirectionZ = 0;
        u->m_Health = spec.m_Health;
        u->m_Cooldown = Random01(seed + 11) * spec.m_Reload;
        u->m_Target = NO_TARGET;
        u->m_Respawn = u->m_Flash = 0;
        u->m_Order = u->m_Fired = 0;
    }

    // Serial first stage: snapshot rows, rebuild neighbor chains and count relay presence.
    // Later worker stages read this snapshot without observing one another's writes.
    static void Grid(HDataStoreQuery query, uint32_t first, uint32_t count, void* context)
    {
        Battle* b = (Battle*)context;
        b->m_StepStart = dmTime::GetMonotonicTime();
        memset(b->m_Heads, 0xff, sizeof(b->m_Heads));
        memset(b->m_TargetHeads, 0xff, sizeof(b->m_TargetHeads));
        for (uint32_t i = 0; i < RELAY_COUNT; ++i)
        {
            memset(b->m_Relays[i].m_Presence, 0, sizeof(b->m_Relays[i].m_Presence));
        }

        DataStoreIterator it = DataStoreQueryIterRange(query, first, count);
        while (DataStoreIterNext(&it))
        {
            uint32_t row_count;
            Unit* units = (Unit*)DataStoreIterGetDataMutable(&it, &row_count);
            for (uint32_t row_index = 0; row_index < row_count; ++row_index)
            {
                const Unit* u = &units[row_index];
                b->m_Snapshot[u->m_Index] = *u;
                if (!Alive(*u))
                {
                    continue;
                }

                int32_t cell = Cell(Z(*u)) * GRID_SIDE + Cell(X(*u));
                b->m_Next[u->m_Index] = b->m_Heads[cell];
                b->m_Heads[cell] = u->m_Index;
                // Bound target sampling within one team so allies cannot hide enemies.
                b->m_TargetNext[u->m_Index] = b->m_TargetHeads[u->m_Team][cell];
                b->m_TargetHeads[u->m_Team][cell] = u->m_Index;
                for (uint32_t r = 0; r < RELAY_COUNT; ++r)
                {
                    if (Square(X(*u) - b->m_Relays[r].m_X) + Square(Z(*u) - b->m_Relays[r].m_Z) < 48 * 48)
                    {
                        ++b->m_Relays[r].m_Presence[u->m_Team];
                    }
                }
            }
        }
    }

    // Search nearby snapshot cells for an enemy, a repair target, or an enemy HQ.
    // Neighbor visits are bounded so dense armies cannot make the search quadratic.
    static uint32_t FindTarget(const Battle* b, const Unit& u)
    {
        bool healer = u.m_Kind == SUPPORT;
        float range = SPECS[u.m_Kind].m_Range + 32, best = range * range;
        uint32_t target = NO_TARGET;
        int32_t xmin = Cell(X(u) - range), xmax = Cell(X(u) + range);
        int32_t zmin = Cell(Z(u) - range), zmax = Cell(Z(u) + range);
        for (int32_t z = zmin; z <= zmax; ++z)
        {
            for (int32_t x = xmin; x <= xmax; ++x)
            {
                int32_t index = b->m_TargetHeads[healer ? u.m_Team : 1 - u.m_Team][z * GRID_SIDE + x];
                // Bounded sampling keeps a dense melee from becoming quadratic.
                for (uint32_t visits = 0; index >= 0 && visits < 12; ++visits, index = b->m_TargetNext[index])
                {
                    const Unit& v = b->m_Snapshot[index];
                    if (healer ? (v.m_Team != u.m_Team || v.m_Health >= SPECS[v.m_Kind].m_Health || v.m_Index == u.m_Index) : v.m_Team == u.m_Team)
                    {
                        continue;
                    }

                    float distance = Square(X(v) - X(u)) + Square(Z(v) - Z(u));
                    if (distance < best)
                    {
                        best = distance;
                        target = index;
                    }
                }
            }
        }

        if (!healer && Square(BASE_X[1 - u.m_Team] - X(u)) + Square(BASE_Z[1 - u.m_Team] - Z(u)) < Square(SPECS[u.m_Kind].m_Range + 16))
        {
            target = MAX_UNITS + 1 - u.m_Team;
        }

        return target;
    }

    // Parallel decision stage: update goals, targets, weapon timers and respawns.
    // Each job writes only its reserved rows and reads other units through the snapshot.
    static void Think(HDataStoreQuery query, uint32_t first, uint32_t count, void* context)
    {
        Battle* b = (Battle*)context;
        if (b->m_Dt == 0)
        {
            return;
        }

        DataStoreIterator it = DataStoreQueryIterRange(query, first, count);
        while (DataStoreIterNext(&it))
        {
            uint32_t row_count;
            Unit* units = (Unit*)DataStoreIterGetDataMutable(&it, &row_count);
            for (uint32_t row_index = 0; row_index < row_count; ++row_index)
            {
                Unit* u = &units[row_index];
                u->m_Fired = 0;
                u->m_Flash = fmaxf(0, u->m_Flash - b->m_Dt);
                if (!Alive(*u))
                {
                    u->m_Respawn -= b->m_Dt;
                    if (u->m_Respawn <= 0)
                    {
                        Spawn(u, false);
                    }

                    continue;
                }

                u->m_Cooldown -= b->m_Dt;
                if ((u->m_Index + b->m_Tick) % 8 == 0 || (u->m_Target < b->m_Count && !Alive(b->m_Snapshot[u->m_Target])))
                {
                    u->m_Target = FindTarget(b, *u);
                }

                bool autonomous = u->m_Team || b->m_Autoplay;
                if (autonomous)
                {
                    u->m_Order = 0;
                    u->m_GoalX = BASE_X[1 - u->m_Team];
                    u->m_GoalZ = BASE_Z[1 - u->m_Team];
                    float best = 1000000;
                    for (uint32_t r = 0; r < RELAY_COUNT; ++r)
                    {
                        const Relay& relay = b->m_Relays[r];
                        if (relay.m_Owner == (int32_t)u->m_Team)
                        {
                            continue;
                        }

                        float distance = Square(relay.m_X - X(*u)) + Square(relay.m_Z - Z(*u));
                        if (distance < best)
                        {
                            best = distance;
                            u->m_GoalX = relay.m_X;
                            u->m_GoalZ = relay.m_Z;
                        }
                    }

                    // Stable per-unit offsets avoid every army aiming at one point.
                    u->m_GoalX += (Random01(u->m_Index + 111) - 0.5f) * 58;
                    u->m_GoalZ += (Random01(u->m_Index + 113) - 0.5f) * 58;
                }

                float tx = u->m_GoalX, tz = u->m_GoalZ;
                bool target_valid = u->m_Target < b->m_Count || (u->m_Target >= MAX_UNITS && u->m_Target <= MAX_UNITS + 1);
                if (target_valid)
                {
                    float target_x = u->m_Target < b->m_Count ? X(b->m_Snapshot[u->m_Target]) : BASE_X[u->m_Target - MAX_UNITS];
                    float target_z = u->m_Target < b->m_Count ? Z(b->m_Snapshot[u->m_Target]) : BASE_Z[u->m_Target - MAX_UNITS];
                    float distance = Square(target_x - X(*u)) + Square(target_z - Z(*u));
                    if (distance < Square(SPECS[u->m_Kind].m_Range))
                    {
                        if (u->m_Cooldown <= 0)
                        {
                            u->m_Fired = 1;
                            u->m_Cooldown = SPECS[u->m_Kind].m_Reload;
                            u->m_Flash = 0.14f;
                        }

                        if (u->m_Order != 1)
                        {
                            tx = X(*u);
                            tz = Z(*u);
                        }
                    }
                    else if (distance > Square(SPECS[u->m_Kind].m_Range + 64))
                    {
                        u->m_Target = NO_TARGET;
                    }
                    else if (u->m_Order != 1 && (autonomous || u->m_Order == 2))
                    {
                        tx = target_x;
                        tz = target_z;
                    }
                }

                float dx = tx - X(*u), dz = tz - Z(*u), distance = sqrtf(dx * dx + dz * dz);
                if (distance > 2)
                {
                    u->m_DirectionX = dx / distance;
                    u->m_DirectionZ = dz / distance;
                }
                else
                {
                    u->m_DirectionX = u->m_DirectionZ = 0;
                }
            }
        }
    }

    // Parallel movement stage: integrate steering while avoiding neighbors and blockers.
    static void Move(HDataStoreQuery query, uint32_t first, uint32_t count, void* context)
    {
        Battle* b = (Battle*)context;
        if (b->m_Dt == 0)
        {
            return;
        }

        DataStoreIterator it = DataStoreQueryIterRange(query, first, count);
        while (DataStoreIterNext(&it))
        {
            uint32_t row_count;
            Unit* units = (Unit*)DataStoreIterGetDataMutable(&it, &row_count);
            for (uint32_t row_index = 0; row_index < row_count; ++row_index)
            {
                Unit* u = &units[row_index];
                if (!Alive(*u))
                {
                    continue;
                }

                float x = X(*u), z = Z(*u), vx = u->m_DirectionX, vz = u->m_DirectionZ;
                int32_t cx = Cell(x), cz = Cell(z);
                float separation_x = 0, separation_z = 0;
                for (int32_t gz = cz - 1; gz <= cz + 1; ++gz)
                {
                    for (int32_t gx = cx - 1; gx <= cx + 1; ++gx)
                    {
                        if (gx < 0 || gz < 0 || gx >= (int32_t)GRID_SIDE || gz >= (int32_t)GRID_SIDE)
                        {
                            continue;
                        }

                        int32_t index = b->m_Heads[gz * GRID_SIDE + gx];
                        for (uint32_t visits = 0; index >= 0 && visits < 10; ++visits, index = b->m_Next[index])
                        {
                            const Unit& v = b->m_Snapshot[index];
                            float dx = x - X(v), dz = z - Z(v), d2 = dx * dx + dz * dz;
                            float radius = SPECS[u->m_Kind].m_Radius + SPECS[v.m_Kind].m_Radius + 1;
                            if (d2 > 0.001f && d2 < radius * radius)
                            {
                                separation_x += dx / d2;
                                separation_z += dz / d2;
                            }
                        }
                    }
                }

                vx += Clamp(separation_x * 1.6f, -0.8f, 0.8f);
                vz += Clamp(separation_z * 1.6f, -0.8f, 0.8f);
                for (uint32_t o = 0; o < OBSTACLE_COUNT; ++o)
                {
                    const Obstacle& obstacle = OBSTACLES[o];
                    float dx = x - obstacle.m_X, dz = z - obstacle.m_Z;
                    float distance2 = dx * dx + dz * dz, radius = obstacle.m_Radius + 15;
                    if (distance2 < radius * radius && distance2 > 0.000001f)
                    {
                        float distance = sqrtf(distance2);
                        float nx = dx / distance, nz = dz / distance;
                        float inward = vx * nx + vz * nz;
                        if (inward < 0)
                        {
                            float side = (u->m_GoalX - x) * -nz + (u->m_GoalZ - z) * nx >= 0 ? 1.0f : -1.0f;
                            vx -= inward * nx + side * nz;
                            vz -= inward * nz - side * nx;
                        }
                    }
                }

                float length2 = vx * vx + vz * vz;
                if (length2 > 1)
                {
                    float length = sqrtf(length2);
                    vx /= length;
                    vz /= length;
                }

                float speed = SPECS[u->m_Kind].m_Speed * b->m_Dt;
                x += vx * speed;
                z += vz * speed;
                for (uint32_t o = 0; o < OBSTACLE_COUNT; ++o)
                {
                    float dx = x - OBSTACLES[o].m_X, dz = z - OBSTACLES[o].m_Z;
                    float d2 = dx * dx + dz * dz, r = OBSTACLES[o].m_Radius + SPECS[u->m_Kind].m_Radius;
                    if (d2 < r * r && d2 > 0.000001f)
                    {
                        float d = sqrtf(d2);
                        x = OBSTACLES[o].m_X + dx * r / d;
                        z = OBSTACLES[o].m_Z + dz * r / d;
                    }
                }

                u->m_Position.m_Values[0] = Clamp(x, -MAP_HALF + 8, MAP_HALF - 8);
                u->m_Position.m_Values[2] = Clamp(z, -MAP_HALF + 8, MAP_HALF - 8);
            }
        }
    }

    // Serial final stage: collect attacks, apply damage/repairs, capture relays and score victory.
    // Resolving after all workers finish keeps damage and snapshots deterministic.
    static void Resolve(HDataStoreQuery query, uint32_t first, uint32_t count, void* context)
    {
        Battle* b = (Battle*)context;
        memset(b->m_Damage, 0, sizeof(float) * b->m_Count);
        b->m_Stats.m_Alive[0] = b->m_Stats.m_Alive[1] = b->m_Stats.m_Selected = 0;
        memset(b->m_Stats.m_SelectedKinds, 0, sizeof(b->m_Stats.m_SelectedKinds));
        DataStoreIterator it = DataStoreQueryIterRange(query, first, count);
        while (DataStoreIterNext(&it))
        {
            uint32_t row_count;
            Unit* units = (Unit*)DataStoreIterGetDataMutable(&it, &row_count);
            for (uint32_t row_index = 0; row_index < row_count; ++row_index)
            {
                const Unit* u = &units[row_index];
                if (u->m_Fired && b->m_Dt > 0)
                {
                    if (u->m_Target < b->m_Count)
                    {
                        b->m_Damage[u->m_Target] += SPECS[u->m_Kind].m_Damage;
                    }
                    else if (u->m_Target >= MAX_UNITS && u->m_Target <= MAX_UNITS + 1)
                    {
                        b->m_Stats.m_BaseHealth[u->m_Target - MAX_UNITS] -= SPECS[u->m_Kind].m_Damage * 0.04f;
                    }
                }
            }
        }

        it = DataStoreQueryIterRange(query, first, count);
        while (DataStoreIterNext(&it))
        {
            uint32_t row_count;
            Unit* units = (Unit*)DataStoreIterGetDataMutable(&it, &row_count);
            for (uint32_t row_index = 0; row_index < row_count; ++row_index)
            {
                Unit* u = &units[row_index];
                if (Alive(*u))
                {
                    u->m_Health = fminf(SPECS[u->m_Kind].m_Health, u->m_Health - b->m_Damage[u->m_Index]);
                    if (!Alive(*u))
                    {
                        u->m_Respawn = 8;
                        ++b->m_Stats.m_Kills[1 - u->m_Team];
                        u->m_Flash = 0;
                    }
                    else
                    {
                        ++b->m_Stats.m_Alive[u->m_Team];
                        if (u->m_Selected)
                        {
                            ++b->m_Stats.m_Selected;
                            ++b->m_Stats.m_SelectedKinds[u->m_Kind];
                        }
                    }
                }

                b->m_Snapshot[u->m_Index] = *u;
            }
        }

        b->m_Stats.m_Relays[0] = b->m_Stats.m_Relays[1] = 0;
        for (uint32_t r = 0; r < RELAY_COUNT; ++r)
        {
            Relay* relay = &b->m_Relays[r];
            int32_t advantage = (int32_t)relay->m_Presence[1] - (int32_t)relay->m_Presence[0];
            if (advantage)
            {
                relay->m_Control = Clamp(relay->m_Control + (advantage > 0 ? 1 : -1) * b->m_Dt * 0.16f, -1, 1);
            }

            if (relay->m_Control <= -1)
            {
                relay->m_Owner = 0;
            }

            if (relay->m_Control >= 1)
            {
                relay->m_Owner = 1;
            }

            if (relay->m_Owner >= 0)
            {
                ++b->m_Stats.m_Relays[relay->m_Owner];
                b->m_Stats.m_BaseHealth[1 - relay->m_Owner] -= b->m_Dt * 0.7f;
            }
        }

        for (uint32_t t = 0; t < 2; ++t)
        {
            if (b->m_Stats.m_BaseHealth[t] <= 0)
            {
                b->m_Stats.m_BaseHealth[t] = 0;
                b->m_Winner = 1 - t;
            }
        }

        b->m_Time += b->m_Dt;
        b->m_CommandAge += b->m_Dt;
        ++b->m_Tick;
        b->m_SelectedCount = b->m_Stats.m_Selected;
        b->m_Stats.m_SimulationMs = (dmTime::GetMonotonicTime() - b->m_StepStart) * 0.001f;
    }

    // Create the unit query used by scheduled systems and explicit player commands.
    static HDataStoreQuery CreateQuery(HDataStoreContext store)
    {
        DataStoreQueryDesc desc = {};
        desc.m_Items[0] = DataStoreTag("moon.unit");
        desc.m_Flags = DATASTORE_ROW_MUTABLE;
        HDataStoreQuery query = 0;
        if (DataStoreQueryCreate(store, &desc, &query) != DATASTORE_RESULT_OK)
        {
            return 0;
        }

        return query;
    }

    // Replace the unit table and reset objectives while retaining battle-side buffers.
    // Call between schedules, with no outstanding reservation of the previous rows.
    bool Reset(Battle* b, uint32_t count)
    {
        if (count < 2 || count > MAX_UNITS || (count & 1))
        {
            return false;
        }

        if (b->m_Count && DataStoreTableRemove(b->m_Store, dmHashString64("moon.units")) != DATASTORE_RESULT_OK)
        {
            return false;
        }

        b->m_Count = 0;
        DataStoreFieldDesc fields[] = {
            { dmHashString64("position"), DATASTORE_TYPE_VECTOR3, offsetof(Unit, m_Position), 0, "position" },
            { dmHashString64("goal_x"), DATASTORE_TYPE_FLOAT, offsetof(Unit, m_GoalX), 0, "goal_x" },
            { dmHashString64("goal_z"), DATASTORE_TYPE_FLOAT, offsetof(Unit, m_GoalZ), 0, "goal_z" },
            { dmHashString64("direction_x"), DATASTORE_TYPE_FLOAT, offsetof(Unit, m_DirectionX), 0, "direction_x" },
            { dmHashString64("direction_z"), DATASTORE_TYPE_FLOAT, offsetof(Unit, m_DirectionZ), 0, "direction_z" },
            { dmHashString64("health"), DATASTORE_TYPE_FLOAT, offsetof(Unit, m_Health), 0, "health" },
            { dmHashString64("cooldown"), DATASTORE_TYPE_FLOAT, offsetof(Unit, m_Cooldown), 0, "cooldown" },
            { dmHashString64("flash"), DATASTORE_TYPE_FLOAT, offsetof(Unit, m_Flash), 0, "flash" },
            { dmHashString64("respawn"), DATASTORE_TYPE_FLOAT, offsetof(Unit, m_Respawn), 0, "respawn" },
            { dmHashString64("index"), DATASTORE_TYPE_UINT32, offsetof(Unit, m_Index), 0, "index" },
            { dmHashString64("team"), DATASTORE_TYPE_UINT32, offsetof(Unit, m_Team), 0, "team" },
            { dmHashString64("kind"), DATASTORE_TYPE_UINT32, offsetof(Unit, m_Kind), 0, "kind" },
            { dmHashString64("target"), DATASTORE_TYPE_UINT32, offsetof(Unit, m_Target), 0, "target" },
            { dmHashString64("order"), DATASTORE_TYPE_UINT32, offsetof(Unit, m_Order), 0, "order" },
            { dmHashString64("selected"), DATASTORE_TYPE_UINT32, offsetof(Unit, m_Selected), 0, "selected" },
            { dmHashString64("fired"), DATASTORE_TYPE_UINT32, offsetof(Unit, m_Fired), 0, "fired" }
        };
        dmhash_t tag = dmHashString64("moon.unit");
        DataStoreTableDesc table = { dmHashString64("moon.units"), &tag, 1, fields, sizeof(fields) / sizeof(fields[0]), sizeof(Unit), DATASTORE_RESET_NONE };
        if (DataStoreTableAdd(b->m_Store, &table) != DATASTORE_RESULT_OK)
        {
            return false;
        }

        Unit* units = (Unit*)calloc(count, sizeof(Unit));
        if (!units)
        {
            DataStoreTableRemove(b->m_Store, table.m_Type);
            return false;
        }

        for (uint32_t i = 0; i < count; ++i)
        {
            units[i].m_Index = i;
            units[i].m_Team = i >= count / 2;
            uint32_t mix = Mix(i % (count / 2)) % 10;
            units[i].m_Kind = mix < 3 ? SCOUT : (mix < 7 ? ASSAULT : (mix < 9 ? SIEGE : SUPPORT));
            Spawn(&units[i], true);
        }

        bool ok = DataStoreRowCreate(b->m_Store, table.m_Type, 1, count / 2, units, 0) == DATASTORE_RESULT_OK &&
                  DataStoreRowCreate(b->m_Store, table.m_Type, 2, count / 2, units + count / 2, 0) == DATASTORE_RESULT_OK;
        if (!ok)
        {
            free(units);
            DataStoreTableRemove(b->m_Store, table.m_Type);
            return false;
        }

        memcpy(b->m_Snapshot, units, count * sizeof(Unit));
        free(units);
        b->m_Count = count;
        memset(&b->m_Stats, 0, sizeof(b->m_Stats));
        b->m_Stats.m_BaseHealth[0] = b->m_Stats.m_BaseHealth[1] = 1000;
        b->m_Stats.m_Alive[0] = b->m_Stats.m_Alive[1] = count / 2;
        b->m_Time = 0;
        b->m_Tick = b->m_SelectedCount = 0;
        b->m_Winner = -1;
        b->m_CommandAge = 100;
        const float relay_x[] = { -260, 0, 260, 0, 0 };
        const float relay_z[] = { 0, 0, 0, -330, 330 };
        for (uint32_t i = 0; i < RELAY_COUNT; ++i)
        {
            memset(&b->m_Relays[i], 0, sizeof(Relay));
            b->m_Relays[i].m_X = relay_x[i];
            b->m_Relays[i].m_Z = relay_z[i];
            b->m_Relays[i].m_Owner = -1;
        }

        return true;
    }

    // Allocate owned simulation buffers and register the four dependent DataStore stages.
    // The supplied store remains borrowed and must outlive Destroy.
    Battle* Create(HDataStoreContext store, uint32_t count)
    {
        Battle* b = (Battle*)calloc(1, sizeof(Battle));
        if (!b)
        {
            return 0;
        }

        b->m_Store = store;
        b->m_Snapshot = (Unit*)calloc(MAX_UNITS, sizeof(Unit));
        b->m_Damage = (float*)calloc(MAX_UNITS, sizeof(float));
        b->m_Next = (int32_t*)calloc(MAX_UNITS, sizeof(int32_t));
        b->m_TargetNext = (int32_t*)calloc(MAX_UNITS, sizeof(int32_t));
        b->m_Camera.m_Width = 1440;
        b->m_Camera.m_Height = 900;
        b->m_Camera.m_Span = 920;
        b->m_Autoplay = true;
        if (!b->m_Snapshot || !b->m_Damage || !b->m_Next || !b->m_TargetNext || !Reset(b, count))
        {
            Destroy(b);
            return 0;
        }

        b->m_Query = CreateQuery(store);
        if (!b->m_Query)
        {
            Destroy(b);
            return 0;
        }

        DataStoreSystemProcess callbacks[] = { Grid, Think, Move, Resolve };
        for (uint32_t i = 0; i < 4; ++i)
        {
            DataStoreSystemDesc system = {};
            system.m_Name = dmHashString64(SYSTEM_NAMES[i]);
            system.m_DependsOn[0] = i ? dmHashString64(SYSTEM_NAMES[i - 1]) : DATASTORE_EVENT_GAMEOBJECT_PRE_UPDATE;
            system.m_Query = b->m_Query;
            system.m_Process = callbacks[i];
            system.m_Context = b;
            system.m_RowsPerJob = i == 1 || i == 2 ? 512 : 0;
            if (DataStoreFunctionAdd(store, &system) != DATASTORE_RESULT_OK)
            {
                Destroy(b);
                return 0;
            }
        }

        return b;
    }

    // Unregister systems and the query before removing rows and freeing owned buffers.
    void Destroy(Battle* b)
    {
        if (!b)
        {
            return;
        }

        for (uint32_t i = 0; i < 4; ++i)
        {
            DataStoreFunctionRemove(b->m_Store, dmHashString64(SYSTEM_NAMES[i]));
        }

        if (b->m_Query)
        {
            DataStoreQueryDestroy(b->m_Query);
        }

        if (b->m_Count)
        {
            DataStoreTableRemove(b->m_Store, dmHashString64("moon.units"));
        }

        free(b->m_Snapshot);
        free(b->m_Damage);
        free(b->m_Next);
        free(b->m_TargetNext);
        free(b);
    }

    // Set a bounded simulation step, freezing gameplay while paused or after victory.
    void SetDeltaTime(Battle* b, float dt)
    {
        b->m_Dt = b->m_Paused || b->m_Winner >= 0 ? 0 : Clamp(dt, 0, 1.0f / 30);
    }

    // Select living Selene units by type, nearest click, or projected screen rectangle.
    // Reserve rows for the write and mirror selection into the presentation snapshot.
    void Select(Battle* b, float x0, float y0, float x1, float y1, bool additive, int32_t kind)
    {
        if (DataStoreQueryTryBegin(b->m_Query) != DATASTORE_RESULT_OK)
        {
            return;
        }

        float low_x = fminf(x0, x1), high_x = fmaxf(x0, x1), low_y = fminf(y0, y1), high_y = fmaxf(y0, y1);
        bool click = high_x - low_x < 6 && high_y - low_y < 6;
        uint32_t closest = NO_TARGET;
        float best = 18 * 18;
        if (click && kind < 0)
        {
            for (uint32_t i = 0; i < b->m_Count / 2; ++i)
            {
                const Unit& u = b->m_Snapshot[i];
                if (!Alive(u))
                {
                    continue;
                }

                float sx, sy;
                WorldToScreen(&b->m_Camera, X(u), 2, Z(u), &sx, &sy);
                float d = Square(sx - x1) + Square(sy - y1);
                if (d < best)
                {
                    best = d;
                    closest = i;
                }
            }
        }

        b->m_SelectedCount = 0;
        DataStoreIterator it = DataStoreQueryIter(b->m_Query);
        while (DataStoreIterNext(&it))
        {
            uint32_t row_count;
            Unit* units = (Unit*)DataStoreIterGetDataMutable(&it, &row_count);
            for (uint32_t row_index = 0; row_index < row_count; ++row_index)
            {
                Unit* u = &units[row_index];
                bool chosen = false;
                if (u->m_Team == 0 && Alive(*u))
                {
                    float sx, sy;
                    WorldToScreen(&b->m_Camera, X(*u), 2, Z(*u), &sx, &sy);
                    chosen = kind >= 0 ? ((int32_t)u->m_Kind == kind || kind == KIND_COUNT) : (click ? u->m_Index == closest : sx >= low_x && sx <= high_x && sy >= low_y && sy <= high_y);
                }

                u->m_Selected = chosen || (additive && u->m_Selected);
                b->m_SelectedCount += u->m_Selected && Alive(*u);
                b->m_Snapshot[u->m_Index].m_Selected = u->m_Selected;
            }
        }

        DataStoreQueryEnd(b->m_Query);
        if (b->m_SelectedCount)
        {
            b->m_Autoplay = false;
        }
    }

    // Assign selected units walkable formation slots around a move or attack-move goal.
    void Order(Battle* b, float x, float z, bool attack_move)
    {
        if (!b->m_SelectedCount || DataStoreQueryTryBegin(b->m_Query) != DATASTORE_RESULT_OK)
        {
            return;
        }

        uint32_t columns = (uint32_t)ceilf(sqrtf((float)b->m_SelectedCount)), index = 0;
        float spacing = b->m_SelectedCount > 2048 ? 3.5f : 5.5f;
        DataStoreIterator it = DataStoreQueryIter(b->m_Query);
        while (DataStoreIterNext(&it))
        {
            uint32_t row_count;
            Unit* units = (Unit*)DataStoreIterGetDataMutable(&it, &row_count);
            for (uint32_t row_index = 0; row_index < row_count; ++row_index)
            {
                Unit* u = &units[row_index];
                if (!u->m_Selected || u->m_Team || !Alive(*u))
                {
                    continue;
                }

                float gx = x + ((int32_t)(index % columns) - (columns - 1) * 0.5f) * spacing;
                float gz = z + ((int32_t)(index / columns) - (columns - 1) * 0.5f) * spacing;
                for (uint32_t o = 0; o < OBSTACLE_COUNT; ++o)
                {
                    float dx = gx - OBSTACLES[o].m_X, dz = gz - OBSTACLES[o].m_Z;
                    float distance = sqrtf(dx * dx + dz * dz), radius = OBSTACLES[o].m_Radius + 8;
                    if (distance < radius)
                    {
                        gx = OBSTACLES[o].m_X + radius * (distance > 0.01f ? dx / distance : 1);
                        gz = OBSTACLES[o].m_Z + radius * (distance > 0.01f ? dz / distance : 0);
                    }
                }

                u->m_GoalX = Clamp(gx, -MAP_HALF + 8, MAP_HALF - 8);
                u->m_GoalZ = Clamp(gz, -MAP_HALF + 8, MAP_HALF - 8);
                u->m_Order = attack_move ? 2 : 1;
                ++index;
            }
        }

        DataStoreQueryEnd(b->m_Query);
        b->m_CommandX = x;
        b->m_CommandZ = z;
        b->m_CommandAge = 0;
    }

    // Hold selected units at their current positions and leave attack-move mode.
    void Stop(Battle* b)
    {
        if (DataStoreQueryTryBegin(b->m_Query) != DATASTORE_RESULT_OK)
        {
            return;
        }

        DataStoreIterator it = DataStoreQueryIter(b->m_Query);
        while (DataStoreIterNext(&it))
        {
            uint32_t row_count;
            Unit* units = (Unit*)DataStoreIterGetDataMutable(&it, &row_count);
            for (uint32_t row_index = 0; row_index < row_count; ++row_index)
            {
                Unit* u = &units[row_index];
                if (u->m_Selected)
                {
                    u->m_GoalX = X(*u);
                    u->m_GoalZ = Z(*u);
                    u->m_Order = 1;
                }
            }
        }

        DataStoreQueryEnd(b->m_Query);
    }

}
