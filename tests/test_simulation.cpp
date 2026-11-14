// Copyright 2020-2026 The Defold Foundation
// Copyright 2014-2020 King
// Copyright 2009-2014 Ragnar Svensson, Christian Murray
// Licensed under the Defold License version 1.0 (the "License"); you may not use
// this file except in compliance with the License.
//
// You may obtain a copy of the License, together with FAQs at
// https://www.defold.com/license
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "simulation.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static void Check(bool condition, const char* message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static HDataStoreContext Store()
{
    HDataStoreContext s = DataStoreCreate(0);
    DataStoreEventDesc event = {};
    event.m_Name = DATASTORE_EVENT_GAMEOBJECT_PRE_UPDATE;
    Check(DataStoreEventAdd(s, &event) == DATASTORE_RESULT_OK, "register pre-update event");
    return s;
}

static void Step(Moon::Battle* battle, HJobContext jobs, float dt)
{
    Moon::SetDeltaTime(battle, dt);
    Check(DataStoreSchedulerBegin(battle->m_Store, jobs) == DATASTORE_RESULT_OK, "begin schedule");
    Check(DataStoreSchedulerEvent(battle->m_Store, DATASTORE_EVENT_GAMEOBJECT_PRE_UPDATE) == DATASTORE_RESULT_OK, "signal pre-update");
    Check(DataStoreSchedulerSync(battle->m_Store) == DATASTORE_RESULT_OK, "sync schedule");
    Check(DataStoreSchedulerEnd(battle->m_Store) == DATASTORE_RESULT_OK, "end schedule");
}

// Ground picking must invert the angled projection after pan, zoom and resize.
static void Projection()
{
    Moon::Camera c = { -312, 85, 640, 1600, 1000 };
    for (int32_t i = -700; i <= 700; i += 35)
    {
        float sx, sy, x, z;
        Moon::WorldToScreen(&c, (float)i, 0, (float)-i, &sx, &sy);
        Moon::ScreenToGround(&c, sx, sy, &x, &z);
        Check(fabsf(x - i) < 0.001f && fabsf(z + i) < 0.001f, "projection round trip");
    }
}

// Reverse drag, additive selection and orders must affect living cyan units only.
static void Selection(HJobContext jobs)
{
    HDataStoreContext s = Store();
    Moon::Battle* b = Moon::Create(s, 512);
    Check(b != 0, "create battle");
    Moon::Select(b, 2000, 2000, -1000, -1000, false, -1);
    Check(b->m_SelectedCount == 256, "reverse rectangle selects cyan army");
    for (uint32_t i = 256; i < 512; ++i)
    {
        Check(!b->m_Snapshot[i].m_Selected, "enemy is not selected");
    }

    Moon::Select(b, 0, 0, 0, 0, false, Moon::SCOUT);
    uint32_t scouts = b->m_SelectedCount;
    Check(scouts > 0 && scouts < 256, "type filter");
    Moon::Select(b, 0, 0, 0, 0, true, Moon::ASSAULT);
    Check(b->m_SelectedCount > scouts, "additive selection");
    Moon::Order(b, 740, 740, true);
    Step(b, jobs, 0);
    for (uint32_t i = 0; i < 256; ++i)
    {
        const Moon::Unit& u = b->m_Snapshot[i];
        if (!u.m_Selected)
        {
            continue;
        }

        Check(u.m_Order == 2, "attack order applied");
        Check(fabsf(u.m_GoalX) <= 760 && fabsf(u.m_GoalZ) <= 760, "formation is inside map");
    }

    Moon::Stop(b);
    Step(b, jobs, 0);
    for (uint32_t i = 0; i < b->m_Count; ++i)
    {
        const Moon::Unit& u = b->m_Snapshot[i];
        if (u.m_Selected)
        {
            Check(u.m_Order == 1 && u.m_GoalX == u.m_Position.m_Values[0] && u.m_GoalZ == u.m_Position.m_Values[2], "hold preserves the current position");
        }
    }

    Moon::Select(b, 0, 0, 0, 0, false, Moon::KIND_COUNT);
    Moon::Order(b, 0, 0, false);
    Moon::Unit before = b->m_Snapshot[0];
    b->m_Paused = true;
    Step(b, jobs, 1.0f / 60);
    Check(b->m_Snapshot[0].m_Position.m_Values[0] == before.m_Position.m_Values[0], "pause freezes movement");
    Check(Moon::Reset(b, 1024), "reset larger army");
    Step(b, jobs, 0);
    Check(b->m_Stats.m_Alive[0] + b->m_Stats.m_Alive[1] == 1024, "reset updates live queries");
    Check(!Moon::Reset(b, 513) && b->m_Count == 1024, "invalid reset preserves battle");
    Moon::Destroy(b);
    Check(DataStoreDestroy(s) == DATASTORE_RESULT_OK, "destroy selection store");
}

// A lone intruder must be attacked even when many allies occupy the same spatial cell.
static void DenseTargeting(HJobContext jobs, uint32_t defenders)
{
    HDataStoreContext store = Store();
    Moon::Battle* b = Moon::Create(store, 128);
    Check(b != 0, "create dense targeting battle");
    b->m_Autoplay = false;
    uint32_t intruder = defenders ? 0 : 64;
    Check(DataStoreQueryTryBegin(b->m_Query) == DATASTORE_RESULT_OK, "reserve dense battle rows");
    DataStoreIterator it = DataStoreQueryIter(b->m_Query);
    while (DataStoreIterNext(&it))
    {
        DataStoreRowIterator rows = DataStoreIterRows(&it);
        while (DataStoreRowIterNext(&rows))
        {
            Moon::Unit* u = (Moon::Unit*)DataStoreRowIterGetDataMut(&rows);
            bool guard = u->m_Team == defenders;
            u->m_Kind = Moon::ASSAULT;
            u->m_Health = guard || u->m_Index == intruder ? Moon::SPECS[Moon::ASSAULT].m_Health : 0;
            u->m_Respawn = 100;
            u->m_Cooldown = guard ? 0 : 1000;
            u->m_Target = Moon::NO_TARGET;
            u->m_Order = 1;
            u->m_Position.m_Values[0] = guard ? 4 + (u->m_Index % 8) * 0.1f : 0;
            u->m_Position.m_Values[2] = guard ? (u->m_Index % 4) * 0.1f : 0;
            u->m_GoalX = u->m_Position.m_Values[0];
            u->m_GoalZ = u->m_Position.m_Values[2];
        }
    }

    DataStoreQueryEnd(b->m_Query);
    Step(b, jobs, 1.0f / 60);
    Check(b->m_Snapshot[intruder].m_Health < Moon::SPECS[Moon::ASSAULT].m_Health, "defenders attack an intruder hidden behind allies");
    Moon::Destroy(b);
    Check(DataStoreDestroy(store) == DATASTORE_RESULT_OK, "destroy dense targeting store");
}

// A real battle must exchange damage, capture objectives and obey map bounds.
static void Combat(HJobContext jobs)
{
    HDataStoreContext s = Store();
    Moon::Battle* b = Moon::Create(s, 1024);
    Check(b != 0, "create combat");
    for (uint32_t tick = 0; tick < 1800; ++tick)
    {
        Step(b, jobs, 1.0f / 60);
    }

    Check(b->m_Stats.m_Kills[0] + b->m_Stats.m_Kills[1] > 0, "armies fight");
    Check(b->m_Stats.m_Relays[0] + b->m_Stats.m_Relays[1] > 0, "armies capture relays");
    for (uint32_t i = 0; i < b->m_Count; ++i)
    {
        const Moon::Unit& u = b->m_Snapshot[i];
        Check(isfinite(u.m_Position.m_Values[0]) && isfinite(u.m_Health), "state stays finite");
        if (u.m_Health > 0)
        {
            Check(Moon::IsWalkable(u.m_Position.m_Values[0], u.m_Position.m_Values[2], Moon::SPECS[u.m_Kind].m_Radius - 0.02f), "units avoid blockers and boundaries");
        }
    }

    printf("combat: kills %u/%u; relays %u/%u\n", b->m_Stats.m_Kills[0], b->m_Stats.m_Kills[1], b->m_Stats.m_Relays[0], b->m_Stats.m_Relays[1]);
    Moon::Destroy(b);
    Check(DataStoreDestroy(s) == DATASTORE_RESULT_OK, "destroy combat store");
}

// Worker splitting must produce the same observable battle as a zero-worker schedule.
static void Determinism(HJobContext jobs)
{
    HDataStoreContext sa = Store(), sb = Store();
    Moon::Battle* a = Moon::Create(sa, 1024);
    Moon::Battle* b = Moon::Create(sb, 1024);
    JobSystemCreateParams p = { "MoonSingle", 0 };
    HJobContext single = JobSystemCreate(&p);
    for (uint32_t tick = 0; tick < 180; ++tick)
    {
        Step(a, single, 1.0f / 60);
        Step(b, jobs, 1.0f / 60);
    }

    Check(memcmp(a->m_Snapshot, b->m_Snapshot, 1024 * sizeof(Moon::Unit)) == 0, "parallel battle matches serial");
    Moon::Destroy(a);
    Moon::Destroy(b);
    JobSystemDestroy(single);
    DataStoreDestroy(sa);
    DataStoreDestroy(sb);
}

int main()
{
    JobSystemCreateParams p = { "MoonTests", 2 };
    HJobContext jobs = JobSystemCreate(&p);
    Projection();
    Selection(jobs);
    DenseTargeting(jobs, 0);
    DenseTargeting(jobs, 1);
    Combat(jobs);
    Determinism(jobs);
    puts("PASS: projection, selection, formations, pause, reset, dense targeting, combat, bounds, worker determinism");
    JobSystemDestroy(jobs);
    return 0;
}
