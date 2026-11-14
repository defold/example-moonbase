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

#include "geometry.h"
#include <dmsdk/dlib/log.h>
#include <dmsdk/gamesys/resources/res_model.h>
#include <dmsdk/gamesys/resources/res_material.h>
#include <math.h>
#include <string.h>

namespace Moon
{
    static const Color SELENE = { 0.27f, 0.61f, 0.94f, 1 };
    static const Color HELIOS = { 0.77f, 0.24f, 0.20f, 1 };
    static const Color WHITE = { 0.83f, 0.88f, 0.89f, 1 };
    static const Color GREEN = { 0.58f, 0.88f, 0.29f, 1 };
    static const float PI = 3.14159265359f;
    // Apply simple face lighting while preserving the color's alpha.
    static Color Shade(Color c, float s)
    {
        c.m_R *= s;
        c.m_G *= s;
        c.m_B *= s;
        return c;
    }

    // Resolve a simulation team index to its shared world and interface color.
    static Color Team(uint32_t team)
    {
        return team ? HELIOS : SELENE;
    }

    // Fit the 1440-wide console design to smaller screens without scaling up larger ones.
    float GetHudScale(float screen_width)
    {
        return screen_width < 1440 ? screen_width / 1440 : 1;
    }

    // Share command-button bounds between native drawing and input hit testing.
    HudRect GetHudButtonRect(HudAction action, float canvas_width)
    {
        if (action <= HUD_RESTART)
        {
            HudRect r = { 720.0f + (action % 3) * 98, action < HUD_PAUSE ? 108.0f : 40.0f, 88, 56 };
            return r;
        }

        if (action == HUD_LESS || action == HUD_MORE)
        {
            HudRect r = { canvas_width - (action == HUD_LESS ? 98 : 58), 132, 32, 32 };
            return r;
        }

        float width = (canvas_width - 1072) * 0.5f;
        HudRect r = { action == HUD_HOME ? 1040 : 1052 + width, 40, width, 56 };
        return r;
    }

    // Append one colored vertex, growing geometrically to avoid retaining many freed blocks.
    static void V(Mesh* m, float x, float y, float z, Color c)
    {
        if (m->m_Vertices.Full())
        {
            uint32_t capacity = m->m_Vertices.Capacity();
            m->m_Vertices.OffsetCapacity(capacity ? capacity : 512);
        }

        Vertex v = { x, y, z, c.m_R, c.m_G, c.m_B, c.m_A };
        m->m_Vertices.Push(v);
    }

    // Append an unindexed triangle to the current geometry batch.
    static void Tri(Mesh* m, float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz, Color color)
    {
        V(m, ax, ay, az, color);
        V(m, bx, by, bz, color);
        V(m, cx, cy, cz, color);
    }

    // Split a flat four-cornered face into two triangles.
    static void Quad(Mesh* m, float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz, float dx, float dy, float dz, Color color)
    {
        Tri(m, ax, ay, az, bx, by, bz, cx, cy, cz, color);
        Tri(m, ax, ay, az, cx, cy, cz, dx, dy, dz, color);
    }

    // Build a box without a hidden bottom face, shading its sides for depth cues.
    static void Box(Mesh* m, float x, float y, float z, float rx, float h, float rz, Color color)
    {
        Quad(m, x - rx, y + h, z - rz, x + rx, y + h, z - rz, x + rx, y + h, z + rz, x - rx, y + h, z + rz, color);
        Quad(m, x - rx, y, z + rz, x + rx, y, z + rz, x + rx, y + h, z + rz, x - rx, y + h, z + rz, Shade(color, 0.52f));
        Quad(m, x - rx, y, z - rz, x - rx, y, z + rz, x - rx, y + h, z + rz, x - rx, y + h, z - rz, Shade(color, 0.68f));
        Quad(m, x + rx, y, z + rz, x + rx, y, z - rz, x + rx, y + h, z - rz, x + rx, y + h, z + rz, Shade(color, 0.35f));
        Quad(m, x + rx, y, z - rz, x - rx, y, z - rz, x - rx, y + h, z - rz, x + rx, y + h, z - rz, Shade(color, 0.78f));
    }

    // Build a four-sided raised silhouette for small units and relay beacons.
    static void Pyramid(Mesh* m, float x, float y, float z, float radius, float h, Color color)
    {
        Tri(m, x - radius, y, z, x, y + h, z, x, y, z - radius, Shade(color, 0.8f));
        Tri(m, x, y, z - radius, x, y + h, z, x + radius, y, z, color);
        Tri(m, x + radius, y, z, x, y + h, z, x, y, z + radius, Shade(color, 0.5f));
        Tri(m, x, y, z + radius, x, y + h, z, x - radius, y, z, Shade(color, 0.65f));
    }

    // Add a segmented ground ring for selections, relay ranges and order feedback.
    static void Ring(Mesh* m, float x, float y, float z, float radius, float width, Color color, uint32_t segments)
    {
        for (uint32_t i = 0; i < segments; ++i)
        {
            float a = i * 2 * PI / segments, b = (i + 1) * 2 * PI / segments;
            float ca = cosf(a), sa = sinf(a), cb = cosf(b), sb = sinf(b);
            Quad(m, x + ca * radius, y, z + sa * radius, x + cb * radius, y, z + sb * radius, x + cb * (radius - width), y, z + sb * (radius - width), x + ca * (radius - width), y, z + sa * (radius - width), color);
        }
    }

    // Draw a thin world-space strip between two points for barrels and tracers.
    static void Beam(Mesh* m, float x, float y, float z, float tx, float ty, float tz, float width, Color c)
    {
        float dx = tx - x, dz = tz - z, d = sqrtf(dx * dx + dz * dz) + 0.001f;
        float ox = -dz / d * width, oz = dx / d * width;
        Quad(m, x + ox, y, z + oz, tx + ox, ty, tz + oz, tx - ox, ty, tz - oz, x - ox, y, z - oz, c);
    }

    // Add a filled rectangle in the console's two-dimensional coordinate space.
    static void Rect(Mesh* m, float x, float y, float w, float h, Color c)
    {
        Quad(m, x, y, 0, x + w, y, 0, x + w, y + h, 0, x, y + h, 0, c);
    }

    // Border a screen rectangle using four strips of the requested thickness.
    static void Outline(Mesh* m, float x, float y, float w, float h, float t, Color c)
    {
        Rect(m, x, y, w, t, c);
        Rect(m, x, y + h - t, w, t, c);
        Rect(m, x, y, t, h, c);
        Rect(m, x + w - t, y, t, h, c);
    }

    static const Color PLATE = { 0.34f, 0.39f, 0.42f, 1 };
    static const Color EDGE_LIGHT = { 0.68f, 0.75f, 0.79f, 1 };
    static const Color EDGE_DARK = { 0.085f, 0.11f, 0.13f, 1 };
    static const Color DISPLAY = { 0.10f, 0.14f, 0.16f, 1 };

    // Shade and bevel a metal panel; reverse the edge lighting for recessed controls.
    static void Frame(Mesh* m, float x, float y, float w, float h, bool inset, Color fill)
    {
        Rect(m, x, y, w, h, EDGE_DARK);
        Color lower = Shade(fill, inset ? 1.0f : 0.78f);
        Color upper = Shade(fill, inset ? 1.0f : 1.15f);
        V(m, x + 2, y + 2, 0, lower);
        V(m, x + w - 2, y + 2, 0, lower);
        V(m, x + w - 2, y + h - 2, 0, upper);
        V(m, x + 2, y + 2, 0, lower);
        V(m, x + w - 2, y + h - 2, 0, upper);
        V(m, x + 2, y + h - 2, 0, upper);
        Color light = inset ? EDGE_DARK : EDGE_LIGHT;
        Color dark = inset ? EDGE_LIGHT : EDGE_DARK;
        Rect(m, x + 1, y + h - 3, w - 2, 2, light);
        Rect(m, x + 1, y + 1, 2, h - 2, light);
        Rect(m, x + 1, y + 1, w - 2, 2, dark);
        Rect(m, x + w - 3, y + 1, 2, h - 2, dark);
    }

    // Draw a filled disc or outlined circle for console indicators and icons.
    static void Circle(Mesh* m, float x, float y, float radius, float width, Color c)
    {
        for (uint32_t i = 0; i < 16; ++i)
        {
            float a = i * PI / 8, b = (i + 1) * PI / 8;
            if (width >= radius)
            {
                Tri(m, x, y, 0, x + cosf(a) * radius, y + sinf(a) * radius, 0, x + cosf(b) * radius, y + sinf(b) * radius, 0, c);
            }
            else
            {
                Quad(m, x + cosf(a) * radius, y + sinf(a) * radius, 0, x + cosf(b) * radius, y + sinf(b) * radius, 0,
                     x + cosf(b) * (radius - width), y + sinf(b) * (radius - width), 0,
                     x + cosf(a) * (radius - width), y + sinf(a) * (radius - width), 0, c);
            }
        }
    }

    // Place small screw heads at the corners of a console plate.
    static void Fasteners(Mesh* m, float x, float y, float w, float h)
    {
        for (uint32_t i = 0; i < 4; ++i)
        {
            float px = x + (i & 1 ? w - 6 : 6), py = y + (i & 2 ? h - 6 : 6);
            Circle(m, px, py, 2, 2, EDGE_DARK);
            Rect(m, px - 1, py, 2, 1, EDGE_LIGHT);
        }
    }

    // Draw a distinct vehicle portrait for each selectable unit type.
    static void UnitIcon(Mesh* m, uint32_t kind, float x, float y)
    {
        if (kind == SUPPORT)
        {
            Tri(m, x - 13, y, 0, x, y + 16, 0, x + 13, y, 0, Shade(SELENE, 0.55f));
            Tri(m, x - 13, y, 0, x, y - 14, 0, x + 13, y, 0, Shade(SELENE, 0.35f));
            Rect(m, x - 2, y - 9, 4, 20, WHITE);
            Rect(m, x - 9, y - 2, 18, 4, WHITE);
            return;
        }

        float width = kind == SCOUT ? 18.0f : (kind == SIEGE ? 28.0f : 24.0f);
        Rect(m, x - width * 0.5f - 5, y - 15, 6, 29, EDGE_DARK);
        Rect(m, x + width * 0.5f - 1, y - 15, 6, 29, EDGE_DARK);
        for (uint32_t i = 0; i < 5; ++i)
        {
            Rect(m, x - width * 0.5f - 4, y - 13 + i * 5, 4, 2, EDGE_LIGHT);
            Rect(m, x + width * 0.5f, y - 13 + i * 5, 4, 2, EDGE_LIGHT);
        }

        if (kind == SCOUT)
        {
            Tri(m, x - 9, y - 11, 0, x + 9, y - 11, 0, x, y + 16, 0, SELENE);
            Rect(m, x - 3, y - 4, 6, 9, WHITE);
        }
        else
        {
            Frame(m, x - width * 0.5f, y - 13, width, 25, false, Shade(SELENE, 0.6f));
            Circle(m, x, y + 1, kind == SIEGE ? 8 : 6, 8, SELENE);
            Rect(m, x - (kind == SIEGE ? 2 : 1), y + 1, kind == SIEGE ? 4 : 2, kind == SIEGE ? 21 : 17, WHITE);
        }
    }

    // Draw command symbols into the existing console batch without image assets.
    static void CommandIcon(Mesh* m, HudAction action, float x, float y, Color c)
    {
        switch (action)
        {
            case HUD_MOVE:
            {
                Rect(m, x - 2, y - 11, 4, 17, c);
                Tri(m, x - 9, y + 3, 0, x + 9, y + 3, 0, x, y + 14, 0, c);
                break;
            }
            case HUD_ATTACK:
            {
                Circle(m, x, y, 9, 2, c);
                Rect(m, x - 1, y - 14, 2, 7, c);
                Rect(m, x - 1, y + 7, 2, 7, c);
                Rect(m, x - 14, y - 1, 7, 2, c);
                Rect(m, x + 7, y - 1, 7, 2, c);
                break;
            }
            case HUD_HOLD:
            {
                Quad(m, x - 10, y + 11, 0, x + 10, y + 11, 0, x + 8, y - 5, 0, x - 8, y - 5, 0, Shade(c, 0.55f));
                Tri(m, x - 8, y - 5, 0, x + 8, y - 5, 0, x, y - 13, 0, c);
                Rect(m, x - 2, y - 2, 4, 12, c);
                break;
            }
            case HUD_PAUSE:
            {
                Rect(m, x - 8, y - 10, 5, 20, c);
                Rect(m, x + 3, y - 10, 5, 20, c);
                break;
            }
            case HUD_AUTOPLAY:
            {
                Circle(m, x, y + 5, 6, 6, c);
                Quad(m, x - 5, y - 3, 0, x + 5, y - 3, 0, x + 10, y - 12, 0, x - 10, y - 12, 0, c);
                break;
            }
            case HUD_RESTART:
            {
                Circle(m, x, y, 10, 2, c);
                Tri(m, x + 13, y + 7, 0, x + 3, y + 9, 0, x + 11, y + 16, 0, c);
                break;
            }
            case HUD_LESS:
            case HUD_MORE:
            {
                Rect(m, x - 7, y - 1, 14, 2, c);
                if (action == HUD_MORE)
                {
                    Rect(m, x - 1, y - 7, 2, 14, c);
                }

                break;
            }
            case HUD_HOME:
            {
                Tri(m, x - 14, y + 1, 0, x + 14, y + 1, 0, x, y + 13, 0, c);
                Rect(m, x - 9, y - 11, 18, 12, Shade(c, 0.7f));
                Rect(m, x - 2, y - 11, 4, 8, EDGE_DARK);
                break;
            }
            case HUD_OVERVIEW:
            {
                Outline(m, x - 13, y - 10, 26, 22, 2, c);
                Rect(m, x - 2, y - 8, 2, 18, Shade(c, 0.55f));
                Rect(m, x - 10, y + 1, 20, 2, Shade(c, 0.55f));
                break;
            }
            case HUD_ACTION_COUNT:
            {
                break;
            }
        }
    }

    // Upload the CPU batch directly to its owned GPU buffer, without a resource-buffer copy.
    static void Commit(Mesh* mesh)
    {
        mesh->m_Render.m_VertexCount = mesh->m_Vertices.Size();
        if (!mesh->m_Vertices.Empty())
        {
            mesh->m_BufferBytes = mesh->m_Vertices.Size() * sizeof(Vertex);
            dmGraphics::SetVertexBufferData(mesh->m_Buffer, mesh->m_BufferBytes, mesh->m_Vertices.Begin(), dmGraphics::BUFFER_USAGE_STREAM_DRAW);
        }
    }

    // Append one compact unit record; grow geometrically so freed intermediates stay small.
    static void AddInstance(InstanceBatch* batch, const Instance& instance)
    {
        if (batch->m_Instances.Full())
        {
            uint32_t capacity = batch->m_Instances.Capacity();
            batch->m_Instances.OffsetCapacity(capacity ? capacity : 512);
        }

        batch->m_Instances.Push(instance);
    }

    // Replace only the per-instance stream, preserving the shared model's vertex and index buffers.
    static void CommitInstances(InstanceBatch* batch)
    {
        batch->m_Render.m_InstanceCount = batch->m_Instances.Size();
        if (!batch->m_Instances.Empty())
        {
            batch->m_BufferBytes = batch->m_Instances.Size() * sizeof(Instance);
            dmGraphics::SetVertexBufferData(batch->m_Buffer, batch->m_BufferBytes, batch->m_Instances.Begin(), dmGraphics::BUFFER_USAGE_STREAM_DRAW);
        }
    }

    // Hash a lattice corner into repeatable terrain color variation.
    static float TerrainCorner(int32_t x, int32_t z)
    {
        uint32_t bits = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u;
        bits = (bits ^ (bits >> 13)) * 1274126177u;
        return (bits ^ (bits >> 16)) / 4294967295.0f;
    }

    // Smoothly interpolate the hashed corners so terrain patches have continuous edges.
    static float TerrainNoise(float x, float z)
    {
        int32_t ix = (int32_t)floorf(x), iz = (int32_t)floorf(z);
        float tx = x - ix, tz = z - iz;
        tx = tx * tx * (3 - 2 * tx);
        tz = tz * tz * (3 - 2 * tz);
        float a = TerrainCorner(ix, iz), b = TerrainCorner(ix + 1, iz);
        float c = TerrainCorner(ix, iz + 1), d = TerrainCorner(ix + 1, iz + 1);
        return (a + (b - a) * tx) * (1 - tz) + (c + (d - c) * tx) * tz;
    }

    // Blend grass and sand, adding grey ground around the shared blocker layout.
    static Color TerrainColor(float x, float z)
    {
        float detail = TerrainNoise(x / 56, z / 56);
        float sand = fminf(1, fmaxf(0, (TerrainNoise(x / 230 + 8, z / 190 - 3) - 0.53f) * 4));
        Color grass = { 0.26f + detail * 0.08f, 0.43f + detail * 0.10f, 0.16f + detail * 0.06f, 1 };
        Color ground = {
            grass.m_R + (0.74f - grass.m_R) * sand,
            grass.m_G + (0.66f - grass.m_G) * sand,
            grass.m_B + (0.44f - grass.m_B) * sand, 1
        };
        for (uint32_t i = 0; i < OBSTACLE_COUNT; ++i)
        {
            const Obstacle& o = OBSTACLES[i];
            float dx = x - o.m_X, dz = z - o.m_Z;
            float stone = fminf(1, fmaxf(0, (o.m_Radius * (1.5f + detail * 0.3f) - sqrtf(dx * dx + dz * dz)) / (o.m_Radius * 0.6f)));
            ground.m_R += (0.48f + detail * 0.08f - ground.m_R) * stone;
            ground.m_G += (0.52f + detail * 0.08f - ground.m_G) * stone;
            ground.m_B += (0.54f + detail * 0.08f - ground.m_B) * stone;
        }

        return ground;
    }

    // Generate the static ground, rock outcrops and headquarters, then upload them once.
    static void Terrain(Mesh* m)
    {
        const uint32_t side = 96;
        for (uint32_t iz = 0; iz < side; ++iz)
        {
            for (uint32_t ix = 0; ix < side; ++ix)
            {
                float x = -MAP_HALF + ix * 16, z = -MAP_HALF + iz * 16;
                // Adjacent cells share colors at their corners, without tile or triangle seams.
                Color a = TerrainColor(x, z), b = TerrainColor(x + 16, z);
                Color c = TerrainColor(x + 16, z + 16), d = TerrainColor(x, z + 16);
                V(m, x, -1, z, a);
                V(m, x + 16, -1, z, b);
                V(m, x + 16, -1, z + 16, c);
                V(m, x, -1, z, a);
                V(m, x + 16, -1, z + 16, c);
                V(m, x, -1, z + 16, d);
            }
        }

        for (uint32_t i = 0; i < OBSTACLE_COUNT; ++i)
        {
            const Obstacle& o = OBSTACLES[i];
            Color rock = { 0.58f, 0.62f, 0.65f, 1 };
            for (uint32_t j = 0; j < 9; ++j)
            {
                float a = j * PI * 2 / 9, b = (j + 1) * PI * 2 / 9;
                float ax = o.m_X + cosf(a) * o.m_Radius, az = o.m_Z + sinf(a) * o.m_Radius;
                float bx = o.m_X + cosf(b) * o.m_Radius, bz = o.m_Z + sinf(b) * o.m_Radius;
                float tx = o.m_X + cosf(a) * o.m_Radius * 0.4f, tz = o.m_Z + sinf(a) * o.m_Radius * 0.4f;
                float ux = o.m_X + cosf(b) * o.m_Radius * 0.4f, uz = o.m_Z + sinf(b) * o.m_Radius * 0.4f;
                Quad(m, ax, 0, az, bx, 0, bz, ux, o.m_Height, uz, tx, o.m_Height, tz, Shade(rock, 0.63f + 0.05f * j));
                Tri(m, tx, o.m_Height, tz, ux, o.m_Height, uz, o.m_X, o.m_Height + 7, o.m_Z, Shade(rock, 1.15f));
            }
        }

        for (uint32_t team = 0; team < 2; ++team)
        {
            float x = team ? 620 : -620;
            Color color = Team(team);
            Ring(m, x, 0.1f, 0, 53, 1.5f, Shade(color, 0.6f), 48);
            Box(m, x, 0, 0, 28, 4, 28, { 0.18f, 0.18f, 0.14f, 1 });
            Box(m, x, 4, 0, 17, 19, 17, { 0.43f, 0.43f, 0.34f, 1 });
            Box(m, x, 23, 0, 19, 2, 19, color);
            Box(m, x, 25, 0, 7, 14, 7, Shade(color, 0.5f));
            Box(m, x, 39, 0, 8, 2, 8, color);
            for (int32_t s = -1; s <= 1; s += 2)
            {
                Box(m, x + s * 43, 0, 0, 9, 8, 22, { 0.27f, 0.28f, 0.22f, 1 });
                Box(m, x + s * 43, 8, 0, 8, 1, 20, Shade(color, 0.55f));
            }
        }

        Commit(m);
    }

    // Borrow the engine contexts; resource references and GPU objects are acquired by BindGeometry.
    Geometry* CreateGeometry(dmResource::HFactory factory, dmGraphics::HContext graphics)
    {
        Geometry* g = new Geometry();
        g->m_Factory = factory;
        g->m_Graphics = graphics;
        return g;
    }

    // Acquire a material reference and create a native triangle batch using the shared declaration.
    static bool BindMesh(Geometry* g, Mesh* mesh, const char* material)
    {
        if (dmResource::Get(g->m_Factory, material, (void**)&mesh->m_Material) != dmResource::RESULT_OK)
        {
            return false;
        }

        mesh->m_Buffer = dmGraphics::NewVertexBuffer(g->m_Graphics, 0, 0, dmGraphics::BUFFER_USAGE_STREAM_DRAW);
        mesh->m_Render.m_VertexBuffer = mesh->m_Buffer;
        mesh->m_Render.m_VertexDeclaration = g->m_VertexDeclaration;
        mesh->m_Render.m_Material = mesh->m_Material->m_Material;
        mesh->m_Render.m_PrimitiveType = dmGraphics::PRIMITIVE_TRIANGLES;
        mesh->m_Render.m_InstanceCount = 1;
        return true;
    }

    // Attach an owned instance stream to a render object whose prototype geometry remains borrowed.
    static void BindInstances(Geometry* g, InstanceBatch* batch)
    {
        batch->m_Buffer = dmGraphics::NewVertexBuffer(g->m_Graphics, 0, 0, dmGraphics::BUFFER_USAGE_STREAM_DRAW);
        batch->m_Render.m_VertexBuffers[1] = batch->m_Buffer;
        batch->m_Render.m_VertexDeclarations[1] = g->m_InstanceDeclaration;
        batch->m_Render.m_InstanceCount = 0;
    }

    // Convert each eligible persistent item into its already prepared native render object.
    static void Dispatch(const dmRender::RenderListDispatchParams& params)
    {
        if (params.m_Operation != dmRender::RENDER_LIST_OPERATION_BATCH)
        {
            return;
        }

        for (const uint32_t* i = params.m_Begin; i != params.m_End; ++i)
        {
            dmRender::RenderObject* ro = (dmRender::RenderObject*)(uintptr_t)params.m_Buf[*i].m_UserData;
            dmRender::AddToRender(params.m_Context, ro);
        }
    }

    // Load compiled glTF models and construct all GPU buffers without authored .buffer or .mesh files.
    bool BindGeometry(Geometry* g, dmRender::HRenderContext render)
    {
        g->m_RenderContext = render;
        dmGraphics::HVertexStreamDeclaration declaration = dmGraphics::NewVertexStreamDeclaration(g->m_Graphics);
        dmGraphics::AddVertexStream(declaration, "position", 3, dmGraphics::TYPE_FLOAT, false);
        dmGraphics::AddVertexStream(declaration, "color", 4, dmGraphics::TYPE_FLOAT, false);
        g->m_VertexDeclaration = dmGraphics::NewVertexDeclaration(g->m_Graphics, declaration);
        dmGraphics::DeleteVertexStreamDeclaration(declaration);

        // Match the SDK's static RigModelVertex layout, including unused tangent and UV streams.
        declaration = dmGraphics::NewVertexStreamDeclaration(g->m_Graphics);
        dmGraphics::AddVertexStream(declaration, "position", 3, dmGraphics::TYPE_FLOAT, false);
        dmGraphics::AddVertexStream(declaration, "normal", 3, dmGraphics::TYPE_FLOAT, false);
        dmGraphics::AddVertexStream(declaration, "tangent", 4, dmGraphics::TYPE_FLOAT, false);
        dmGraphics::AddVertexStream(declaration, "color", 4, dmGraphics::TYPE_FLOAT, false);
        dmGraphics::AddVertexStream(declaration, "texcoord0", 2, dmGraphics::TYPE_FLOAT, false);
        dmGraphics::AddVertexStream(declaration, "texcoord1", 2, dmGraphics::TYPE_FLOAT, false);
        g->m_ModelDeclaration = dmGraphics::NewVertexDeclaration(g->m_Graphics, declaration);
        dmGraphics::DeleteVertexStreamDeclaration(declaration);

        declaration = dmGraphics::NewVertexStreamDeclaration(g->m_Graphics, dmGraphics::VERTEX_STEP_FUNCTION_INSTANCE);
        dmGraphics::AddVertexStream(declaration, "instance_position", 4, dmGraphics::TYPE_FLOAT, false);
        dmGraphics::AddVertexStream(declaration, "instance_data", 4, dmGraphics::TYPE_FLOAT, false);
        g->m_InstanceDeclaration = dmGraphics::NewVertexDeclaration(g->m_Graphics, declaration);
        dmGraphics::DeleteVertexStreamDeclaration(declaration);

        Mesh* meshes[] = { &g->m_Terrain, &g->m_Units, &g->m_Overlay, &g->m_OverlayTop, &g->m_SelectionMesh, &g->m_RadarMesh };
        const char* materials[] = { "/render/terrain.materialc", "/render/world.materialc", "/render/overlay.materialc", "/render/overlay_top.materialc", "/render/selection.materialc", "/render/radar.materialc" };
        for (uint32_t i = 0; i < 6; ++i)
        {
            if (!BindMesh(g, meshes[i], materials[i]))
            {
                return false;
            }
        }

        const char* paths[] = {
            "/models/scout.modelc", "/models/assault.modelc", "/models/siege.modelc", "/models/support.modelc",
            "/models/scout_distant.modelc", "/models/assault_distant.modelc", "/models/siege_distant.modelc", "/models/support_distant.modelc"
        };
        for (uint32_t i = 0; i < KIND_COUNT * 2; ++i)
        {
            InstanceBatch* batch = &g->m_Models[i];
            if (dmResource::Get(g->m_Factory, paths[i], (void**)&batch->m_Model) != dmResource::RESULT_OK)
            {
                return false;
            }

            // Each selected glTF mesh is a single static primitive, at the origin with one material.
            if (batch->m_Model->m_Meshes.Size() != 1 || batch->m_Model->m_Materials.Size() != 1)
            {
                dmLogError("Expected one mesh and material in %s", paths[i]);
                return false;
            }

            const dmGameSystem::ModelResourceBuffers* buffers = batch->m_Model->m_Meshes[0].m_Buffers;
            if (buffers->m_RigModelVertexFormat != dmGameSystem::RIG_MODEL_VERTEX_FORMAT_STATIC)
            {
                dmLogError("Expected a static glTF mesh in %s", paths[i]);
                return false;
            }

            dmRender::RenderObject* ro = &batch->m_Render;
            ro->m_Material = batch->m_Model->m_Materials[0].m_Material->m_Material;
            ro->m_VertexBuffer = buffers->m_VertexBuffer;
            ro->m_VertexDeclaration = g->m_ModelDeclaration;
            ro->m_VertexBufferOffsets[0] = buffers->m_VertexBufferOffset;
            ro->m_IndexBuffer = buffers->m_IndexBuffer;
            ro->m_IndexType = buffers->m_IndexBufferElementType;
            ro->m_VertexStart = buffers->m_IndexBufferOffset;
            ro->m_VertexCount = buffers->m_IndexCount;
            ro->m_PrimitiveType = dmGraphics::PRIMITIVE_TRIANGLES;
            BindInstances(g, batch);
        }

        Terrain(&g->m_Terrain);
        Ring(&g->m_SelectionMesh, 0, 0, 0, 1, 0.12f, GREEN, 12);
        Commit(&g->m_SelectionMesh);
        g->m_Selections.m_Render = g->m_SelectionMesh.m_Render;
        BindInstances(g, &g->m_Selections);
        Rect(&g->m_RadarMesh, 0, 0, 1, 1, WHITE);
        Commit(&g->m_RadarMesh);
        g->m_Radar.m_Render = g->m_RadarMesh.m_Render;
        BindInstances(g, &g->m_Radar);

        g->m_Dispatch = dmRender::RegisterRenderListDispatch(g->m_RenderContext, Dispatch, 0, g);
        if (!g->m_Dispatch)
        {
            return false;
        }

        dmRender::RenderObject* objects[] = {
            &g->m_Terrain.m_Render, &g->m_Units.m_Render, &g->m_Overlay.m_Render, &g->m_OverlayTop.m_Render,
            &g->m_Models[0].m_Render, &g->m_Models[1].m_Render, &g->m_Models[2].m_Render, &g->m_Models[3].m_Render,
            &g->m_Models[4].m_Render, &g->m_Models[5].m_Render, &g->m_Models[6].m_Render, &g->m_Models[7].m_Render,
            &g->m_Selections.m_Render, &g->m_Radar.m_Render
        };
        for (uint32_t i = 0; i < 14; ++i)
        {
            g->m_RenderObjects[i] = objects[i];
            g->m_Items[i] = dmRender::NewRenderItem(g->m_RenderContext, g->m_Dispatch, (uint64_t)(uintptr_t)objects[i]);
            if (!g->m_Items[i])
            {
                return false;
            }

            dmRender::SetMaterial(g->m_RenderContext, g->m_Items[i], objects[i]->m_Material);
            dmRender::SetOrder(g->m_RenderContext, g->m_Items[i], dmRender::RENDER_ORDER_AFTER_WORLD, i);
        }

        return true;
    }

    // Release only owned streams and factory references; model geometry belongs to the resource system.
    void DestroyGeometry(Geometry* g)
    {
        for (uint32_t i = 0; i < 14; ++i)
        {
            if (g->m_Items[i])
            {
                dmRender::DeleteRenderItem(g->m_RenderContext, g->m_Items[i]);
            }
        }

        if (g->m_Dispatch)
        {
            dmRender::UnregisterRenderListDispatch(g->m_RenderContext, g->m_Dispatch);
        }

        Mesh* meshes[] = { &g->m_Terrain, &g->m_Units, &g->m_Overlay, &g->m_OverlayTop, &g->m_SelectionMesh, &g->m_RadarMesh };
        for (uint32_t i = 0; i < 6; ++i)
        {
            if (meshes[i]->m_Buffer)
            {
                dmGraphics::DeleteVertexBuffer(meshes[i]->m_Buffer);
            }

            if (meshes[i]->m_Material)
            {
                dmResource::Release(g->m_Factory, meshes[i]->m_Material);
            }
        }

        InstanceBatch* batches[] = { &g->m_Models[0], &g->m_Models[1], &g->m_Models[2], &g->m_Models[3],
                                     &g->m_Models[4], &g->m_Models[5], &g->m_Models[6], &g->m_Models[7], &g->m_Selections, &g->m_Radar };
        for (uint32_t i = 0; i < 10; ++i)
        {
            if (batches[i]->m_Buffer)
            {
                dmGraphics::DeleteVertexBuffer(batches[i]->m_Buffer);
            }

            if (batches[i]->m_Model)
            {
                dmResource::Release(g->m_Factory, batches[i]->m_Model);
            }
        }

        if (g->m_VertexDeclaration)
        {
            dmGraphics::DeleteVertexDeclaration(g->m_VertexDeclaration);
        }

        if (g->m_ModelDeclaration)
        {
            dmGraphics::DeleteVertexDeclaration(g->m_ModelDeclaration);
        }

        if (g->m_InstanceDeclaration)
        {
            dmGraphics::DeleteVertexDeclaration(g->m_InstanceDeclaration);
        }

        delete g;
    }

    // Submit after RenderListBegin and before RenderListEnd, from render-script recording.
    // The engine consumes this list after recording; callbacks only borrow these stable objects.
    void SubmitGeometry(Geometry* g)
    {
        for (uint32_t i = 0; i < 14; ++i)
        {
            dmRender::RenderObject* ro = g->m_RenderObjects[i];
            dmRender::Enable(g->m_RenderContext, g->m_Items[i], ro->m_VertexCount && ro->m_InstanceCount);
        }

        dmRender::SubmitRenderListDispatch(g->m_RenderContext, g->m_Dispatch);
    }

    // Refresh visible-unit batches, battle effects and the console from the finished snapshot.
    void Present(Geometry* g, Battle* b, bool dragging, float x0, float y0, float x1, float y1)
    {
        Mesh* m = &g->m_Units;
        m->m_Vertices.SetSize(0);
        g->m_Visible = 0;
        for (uint32_t i = 0; i < KIND_COUNT * 2; ++i)
        {
            g->m_Models[i].m_Instances.SetSize(0);
        }

        g->m_Selections.m_Instances.SetSize(0);
        g->m_Radar.m_Instances.SetSize(0);
        float ui_scale = GetHudScale(b->m_Camera.m_Width);
        float pixels = b->m_Camera.m_Height / b->m_Camera.m_Span;
        for (uint32_t i = 0; i < b->m_Count; ++i)
        {
            const Unit& u = b->m_Snapshot[i];
            if (u.m_Health <= 0)
            {
                continue;
            }

            float x = u.m_Position.m_Values[0], z = u.m_Position.m_Values[2], sx, sy;
            float dx = u.m_DirectionX, dz = u.m_DirectionZ;
            if (fabsf(dx) + fabsf(dz) < 0.1f)
            {
                dx = u.m_Team ? -1 : 1;
                dz = 0;
            }

            Instance instance = { x, z, dx, dz, (float)u.m_Team, u.m_Selected ? 1.0f : 0.0f, u.m_Flash, ui_scale };
            AddInstance(&g->m_Radar, instance);
            WorldToScreen(&b->m_Camera, x, 2, z, &sx, &sy);
            if (sx < -24 || sx > b->m_Camera.m_Width + 24 || sy < 0 || sy > b->m_Camera.m_Height + 24)
            {
                continue;
            }

            ++g->m_Visible;
            float radius = SPECS[u.m_Kind].m_Radius;
            instance.m_Scale = 1;
            uint32_t lod = radius * pixels < 3.6f ? KIND_COUNT : 0;
            AddInstance(&g->m_Models[u.m_Kind + lod], instance);
            if (u.m_Selected)
            {
                instance.m_Scale = radius + 1.4f;
                AddInstance(&g->m_Selections, instance);
            }

            if (u.m_Flash > 0 && u.m_Target != NO_TARGET && (i % 4 == 0 || pixels > 2))
            {
                float tx, tz;
                if (u.m_Target < b->m_Count)
                {
                    tx = b->m_Snapshot[u.m_Target].m_Position.m_Values[0];
                    tz = b->m_Snapshot[u.m_Target].m_Position.m_Values[2];
                }
                else
                {
                    tx = u.m_Target == MAX_UNITS ? -620 : 620;
                    tz = 0;
                }

                Beam(m, x, 3, z, tx, 3, tz, u.m_Kind == SIEGE ? 0.9f : 0.35f, u.m_Kind == SUPPORT ? GREEN : WHITE);
            }
        }

        for (uint32_t r = 0; r < RELAY_COUNT; ++r)
        {
            const Relay& relay = b->m_Relays[r];
            Color c = relay.m_Owner < 0 ? WHITE : Team(relay.m_Owner);
            Ring(m, relay.m_X, 0.25f, relay.m_Z, 48, 0.7f, Shade(c, 0.4f), 48);
            Ring(m, relay.m_X, 0.3f, relay.m_Z, 12, 2, c, 24);
            Box(m, relay.m_X, 0, relay.m_Z, 4, 3, 4, { 0.41f, 0.40f, 0.32f, 1 });
            Box(m, relay.m_X, 3, relay.m_Z, 1.4f, 18, 1.4f, Shade(c, 0.7f));
            Pyramid(m, relay.m_X, 21, relay.m_Z, 4, 5, c);
            if (fabsf(relay.m_Control) > 0.01f)
            {
                Ring(m, relay.m_X, 0.4f, relay.m_Z, 15 + fabsf(relay.m_Control) * 8, 0.7f, relay.m_Control < 0 ? SELENE : HELIOS, 24);
            }
        }

        if (b->m_CommandAge < 1.4f)
        {
            float radius = 6 + b->m_CommandAge * 20;
            Ring(m, b->m_CommandX, 0.5f, b->m_CommandZ, radius, 0.8f, GREEN, 32);
        }

        Commit(m);
        for (uint32_t i = 0; i < KIND_COUNT * 2; ++i)
        {
            CommitInstances(&g->m_Models[i]);
        }

        CommitInstances(&g->m_Selections);
        CommitInstances(&g->m_Radar);
        m = &g->m_Overlay;
        m->m_Vertices.SetSize(0);
        float width = b->m_Camera.m_Width / ui_scale, height = b->m_Camera.m_Height / ui_scale;
        Frame(m, 0, height - 82, width, 82, false, PLATE);
        Frame(m, 0, 0, width, 206, false, PLATE);
        Fasteners(m, 0, 0, width, 206);
        Frame(m, 8, height - 72, 204, 62, false, Shade(PLATE, 0.82f));
        Frame(m, 222, height - 72, 274, 62, true, DISPLAY);
        Frame(m, width - 496, height - 72, 274, 62, true, DISPLAY);
        Frame(m, width - 212, height - 72, 204, 62, false, Shade(PLATE, 0.82f));
        Rect(m, 233, height - 34, 5, 21, SELENE);
        Rect(m, width - 485, height - 34, 5, 21, HELIOS);
        for (uint32_t r = 0; r < RELAY_COUNT; ++r)
        {
            float x = width * 0.5f - 52 + r * 26;
            Circle(m, x, height - 64, 6, 6, EDGE_DARK);
            Circle(m, x, height - 64, 4, 4, b->m_Relays[r].m_Owner < 0 ? EDGE_LIGHT : Team(b->m_Relays[r].m_Owner));
        }

        Frame(m, 8, 8, 194, 190, false, PLATE);
        Frame(m, 22, 22, 166, 166, true, DISPLAY);
        Rect(m, 18, 188, 96, 16, PLATE);
        Frame(m, 210, 8, 490, 190, false, PLATE);
        Frame(m, 708, 8, 308, 190, false, PLATE);
        Frame(m, 1024, 8, width - 1032, 190, false, PLATE);
        Frame(m, 1040, 132, width - 1160, 32, true, DISPLAY);
        if (ui_scale != 1)
        {
            for (uint32_t i = 0; i < m->m_Vertices.Size(); ++i)
            {
                m->m_Vertices[i].m_X *= ui_scale;
                m->m_Vertices[i].m_Y *= ui_scale;
            }
        }

        Commit(m);
        m = &g->m_OverlayTop;
        m->m_Vertices.SetSize(0);
        for (uint32_t r = 0; r < RELAY_COUNT; ++r)
        {
            const Relay& relay = b->m_Relays[r];
            float x = 24 + (relay.m_X + MAP_HALF) / 1536 * 162, y = 24 + (MAP_HALF - relay.m_Z) / 1536 * 162;
            Rect(m, x - 2, y - 2, 4, 4, relay.m_Owner < 0 ? WHITE : Team(relay.m_Owner));
        }

        float left, top, right, bottom;
        ScreenToGround(&b->m_Camera, 0, b->m_Camera.m_Height - 83 * ui_scale, &left, &top);
        ScreenToGround(&b->m_Camera, b->m_Camera.m_Width, 206 * ui_scale, &right, &bottom);
        float mx = fmaxf(24, 24 + (left + MAP_HALF) / 1536 * 162), my = fmaxf(24, 24 + (MAP_HALF - bottom) / 1536 * 162);
        float mr = fminf(186, 24 + (right + MAP_HALF) / 1536 * 162), mt = fminf(186, 24 + (MAP_HALF - top) / 1536 * 162);
        if (mr > mx && mt > my)
        {
            Outline(m, mx, my, mr - mx, mt - my, 1, WHITE);
        }

        Frame(m, 237, height - 66, 244, 12, true, EDGE_DARK);
        Rect(m, 240, height - 63, 238 * b->m_Stats.m_BaseHealth[0] / 1000, 6, SELENE);
        Frame(m, width - 481, height - 66, 244, 12, true, EDGE_DARK);
        Rect(m, width - 478, height - 63, 238 * b->m_Stats.m_BaseHealth[1] / 1000, 6, HELIOS);
        for (uint32_t kind = 0; kind < KIND_COUNT; ++kind)
        {
            float x = 226 + kind * 116;
            bool selected = b->m_Stats.m_SelectedKinds[kind] > 0;
            bool hover = x1 / ui_scale >= x && x1 / ui_scale < x + 104 && y1 / ui_scale >= 80 && y1 / ui_scale < 164;
            Frame(m, x, 80, 104, 84, selected, Shade(PLATE, selected ? 0.85f : (hover ? 1.15f : 1.0f)));
            Frame(m, x + 6, 105, 92, 52, true, DISPLAY);
            UnitIcon(m, kind, x + 52, 127);
            Frame(m, x + 8, 142, 16, 16, true, Shade(PLATE, 0.65f));
            Rect(m, x + 56, 108, 40, 18, DISPLAY);
            if (selected)
            {
                Outline(m, x + 4, 84, 96, 76, 1, Shade(SELENE, 0.75f));
            }
        }

        for (uint32_t a = 0; a < HUD_ACTION_COUNT; ++a)
        {
            HudAction action = (HudAction)a;
            HudRect r = GetHudButtonRect(action, width);
            bool enabled = a > HUD_HOLD || b->m_SelectedCount > 0;
            bool active = enabled && ((action == HUD_ATTACK && b->m_AttackMove) || (action == HUD_PAUSE && b->m_Paused));
            bool hover = x1 / ui_scale >= r.m_X && x1 / ui_scale < r.m_X + r.m_Width && y1 / ui_scale >= r.m_Y && y1 / ui_scale < r.m_Y + r.m_Height;
            Frame(m, r.m_X, r.m_Y, r.m_Width, r.m_Height, active, Shade(PLATE, enabled ? (hover ? 1.15f : 1.0f) : 0.85f));
            float icon_y = a <= HUD_RESTART || a >= HUD_HOME ? r.m_Y + 37 : r.m_Y + 16;
            if (action == HUD_PAUSE && b->m_Paused)
            {
                float x = r.m_X + r.m_Width * 0.5f;
                Tri(m, x - 7, icon_y - 11, 0, x - 7, icon_y + 11, 0, x + 10, icon_y, 0, SELENE);
            }
            else
            {
                CommandIcon(m, action, r.m_X + r.m_Width * 0.5f, icon_y, enabled ? (active ? SELENE : WHITE) : EDGE_LIGHT);
            }

            if (a <= HUD_RESTART || a >= HUD_HOME)
            {
                float key_width = action == HUD_PAUSE || action == HUD_MOVE || a >= HUD_HOME ? 28.0f : 20.0f;
                Frame(m, r.m_X + r.m_Width - key_width - 4, r.m_Y + r.m_Height - 17, key_width, 13, true, Shade(PLATE, 0.75f));
            }
        }

        if (b->m_Winner >= 0)
        {
            float x = width * 0.5f - 300, y = height * 0.5f - 20;
            Frame(m, x, y, 600, 164, false, PLATE);
            Frame(m, x + 6, y + 6, 588, 152, true, DISPLAY);
            Rect(m, x + 20, y + 138, 560, 4, Team(b->m_Winner));
        }

        if (dragging)
        {
            float x = fminf(x0, x1) / ui_scale, y = fminf(y0, y1) / ui_scale, w = fabsf(x1 - x0) / ui_scale, h = fabsf(y1 - y0) / ui_scale;
            Rect(m, x, y, w, h, { 0.54f, 0.67f, 0.30f, 0.10f });
            Outline(m, x, y, w, h, 1.5f, GREEN);
        }

        if (ui_scale != 1)
        {
            for (uint32_t i = 0; i < m->m_Vertices.Size(); ++i)
            {
                m->m_Vertices[i].m_X *= ui_scale;
                m->m_Vertices[i].m_Y *= ui_scale;
            }
        }

        Commit(m);
    }

}
