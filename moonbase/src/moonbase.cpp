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
#include "geometry.h"
#include <dmsdk/sdk.h>
#include <dmsdk/engine/engine_event.hpp>
#include <dmsdk/hid/hid.h>
#include <dmsdk/physics/physics_event.h>
#include <btBulletDynamicsCommon.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace
{
    struct Native
    {
        HContextRegistry     m_Registry;
        Moon::Battle*        m_Battle;
        Moon::Geometry*      m_Geometry;
        dmHID::HContext      m_Hid;
        float                m_MouseX, m_MouseY, m_DragX, m_DragY, m_PixelScale;
        float                m_OrderX, m_OrderZ;
        float                m_ZoomTarget;
        float                m_FrameMs, m_FrameAverage;
        uint64_t             m_LastFrame;
        uint32_t             m_Frames, m_PhysicsSteps;
        bool                 m_Up, m_Down, m_Left, m_Right, m_Shift;
        bool                 m_Dragging, m_MapDragging, m_OrderPending, m_OrderAttack;
        bool                 m_Bound;
        uint32_t             m_CaptureFrame, m_CaptureWidth, m_CaptureHeight;
        bool                 m_Offscreen, m_SmokeTest, m_SmokeFailed;
        uint32_t             m_InputBatches, m_InputSelections, m_Orders, m_RayHits;
        const char*          m_CapturePath;
        dmGraphics::HContext m_Graphics;
    };
    static float Clamp(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    static Native* GetNative(lua_State* L)
    {
        return (Native*)lua_touserdata(L, lua_upvalueindex(1));
    }

    static int Bind(lua_State* L)
    {
        Native* n = GetNative(L);
        if (!n->m_Bound)
        {
            dmRender::HRenderContext render = (dmRender::HRenderContext)ContextRegistryGet(n->m_Registry, RENDER_CONTEXT_NAME);
            if (!Moon::BindGeometry(n->m_Geometry, render))
            {
                Moon::DestroyGeometry(n->m_Geometry);
                n->m_Geometry = 0;
                return luaL_error(L, "MoonBase could not bind native rendering resources");
            }

            n->m_Bound = true;
        }

        return 0;
    }

    // Collection finalization runs before the renderer is deleted, so release its registrations here.
    static int Unbind(lua_State* L)
    {
        Native* n = GetNative(L);
        if (n->m_Geometry)
        {
            Moon::DestroyGeometry(n->m_Geometry);
            n->m_Geometry = 0;
        }

        n->m_Bound = false;
        return 0;
    }

    static int Present(lua_State* L)
    {
        Native* n = GetNative(L);
        if (n->m_Bound)
        {
            Moon::Present(n->m_Geometry, n->m_Battle, n->m_Dragging, n->m_DragX, n->m_DragY, n->m_MouseX, n->m_MouseY);
        }

        return 0;
    }

    // Called while the render script records its frame, before render-list consumption.
    static int Submit(lua_State* L)
    {
        Native* n = GetNative(L);
        if (n->m_Bound)
        {
            Moon::SubmitGeometry(n->m_Geometry);
        }

        return 0;
    }

    static int Camera(lua_State* L)
    {
        Native* n = GetNative(L);
        Moon::Camera* c = &n->m_Battle->m_Camera;
        n->m_PixelScale = fmaxf(1, (float)luaL_checknumber(L, 3));
        c->m_Width = fmaxf(1, (float)luaL_checknumber(L, 1));
        c->m_Height = fmaxf(1, (float)luaL_checknumber(L, 2));
        lua_pushnumber(L, c->m_X);
        lua_pushnumber(L, c->m_Z);
        lua_pushnumber(L, c->m_Span);
        return 3;
    }

    static void Number(lua_State* L, const char* name, double value)
    {
        lua_pushnumber(L, value);
        lua_setfield(L, -2, name);
    }

    static int Stats(lua_State* L)
    {
        Native* n = GetNative(L);
        Moon::Battle* b = n->m_Battle;
        lua_createtable(L, 0, 29);
        Number(L, "count", b->m_Count);
        Number(L, "cyan", b->m_Stats.m_Alive[0]);
        Number(L, "amber", b->m_Stats.m_Alive[1]);
        Number(L, "selected", b->m_SelectedCount);
        Number(L, "visible", n->m_Geometry->m_Visible);
        Number(L, "cyan_hp", b->m_Stats.m_BaseHealth[0]);
        Number(L, "amber_hp", b->m_Stats.m_BaseHealth[1]);
        Number(L, "cyan_relays", b->m_Stats.m_Relays[0]);
        Number(L, "amber_relays", b->m_Stats.m_Relays[1]);
        Number(L, "cyan_kills", b->m_Stats.m_Kills[0]);
        Number(L, "amber_kills", b->m_Stats.m_Kills[1]);
        Number(L, "simulation_ms", b->m_Stats.m_SimulationMs);
        Number(L, "frame_ms", n->m_FrameAverage);
        Number(L, "delta_ms", n->m_FrameMs);
        Number(L, "time", b->m_Time);
        Number(L, "winner", b->m_Winner);
        Number(L, "smoke_failed", n->m_SmokeFailed ? 1 : 0);
        Number(L, "physics_steps", n->m_PhysicsSteps);
        Number(L, "autoplay", b->m_Autoplay ? 1 : 0);
        Number(L, "paused", b->m_Paused ? 1 : 0);
        Number(L, "attack_move", b->m_AttackMove ? 1 : 0);
        Number(L, "scout", b->m_Stats.m_SelectedKinds[0]);
        Number(L, "assault", b->m_Stats.m_SelectedKinds[1]);
        Number(L, "siege", b->m_Stats.m_SelectedKinds[2]);
        Number(L, "support", b->m_Stats.m_SelectedKinds[3]);
        Number(L, "width", b->m_Camera.m_Width);
        Number(L, "height", b->m_Camera.m_Height);
        return 1;
    }

    static void Reset(Native* n, uint32_t count)
    {
        if (!Moon::Reset(n->m_Battle, count))
        {
            dmLogError("MoonBase could not reset %u units", count);
        }

        n->m_OrderPending = n->m_Dragging = n->m_MapDragging = false;
    }

    static void ActivateHudButton(Native* n, Moon::HudAction action)
    {
        Moon::Battle* b = n->m_Battle;
        switch (action)
        {
            case Moon::HUD_MOVE:
            {
                b->m_AttackMove = false;
                break;
            }
            case Moon::HUD_ATTACK:
            {
                b->m_AttackMove = true;
                break;
            }
            case Moon::HUD_HOLD:
            {
                Moon::Stop(b);
                break;
            }
            case Moon::HUD_PAUSE:
            {
                b->m_Paused = !b->m_Paused;
                break;
            }
            case Moon::HUD_AUTOPLAY:
            {
                b->m_Autoplay = !b->m_Autoplay;
                break;
            }
            case Moon::HUD_RESTART:
            {
                Reset(n, b->m_Count);
                break;
            }
            case Moon::HUD_LESS:
            {
                uint32_t count = b->m_Count;
                if (count > 32768)
                {
                    count = count > 65536 ? count - 32768 : 32768;
                }
                else
                {
                    count = count >= 1024 ? count / 2 : 512;
                }

                Reset(n, count);
                break;
            }
            case Moon::HUD_MORE:
            {
                uint32_t count = b->m_Count < 32768 ? b->m_Count * 2 : b->m_Count + 32768;
                Reset(n, count < Moon::MAX_UNITS ? count : Moon::MAX_UNITS);
                break;
            }
            case Moon::HUD_HOME:
            {
                b->m_Camera.m_X = -480;
                b->m_Camera.m_Z = 0;
                b->m_Camera.m_Span = n->m_ZoomTarget = 500;
                break;
            }
            case Moon::HUD_OVERVIEW:
            {
                b->m_Camera.m_X = b->m_Camera.m_Z = 0;
                b->m_Camera.m_Span = n->m_ZoomTarget = 1250;
                break;
            }
            case Moon::HUD_ACTION_COUNT:
            {
                break;
            }
        }
    }

    static void MapCamera(Native* n)
    {
        float scale = Moon::GetHudScale(n->m_Battle->m_Camera.m_Width);
        n->m_Battle->m_Camera.m_X = Clamp((n->m_MouseX / scale - 24) / 162 * 1536 - 768, -700, 700);
        n->m_Battle->m_Camera.m_Z = Clamp(768 - (n->m_MouseY / scale - 24) / 162 * 1536, -700, 700);
    }

    static void LeftPress(Native* n)
    {
        Moon::Battle* b = n->m_Battle;
        float scale = Moon::GetHudScale(b->m_Camera.m_Width);
        float x = n->m_MouseX / scale, y = n->m_MouseY / scale;
        if (x >= 24 && x <= 186 && y >= 24 && y <= 186)
        {
            n->m_MapDragging = true;
            MapCamera(n);
            return;
        }

        if (y >= 80 && y <= 164 && x >= 226 && x < 690)
        {
            int32_t kind = (int32_t)((x - 226) / 116);
            if (x - 226 - kind * 116 < 104)
            {
                Moon::Select(b, 0, 0, 0, 0, n->m_Shift, kind);
            }

            return;
        }

        for (uint32_t i = 0; i < Moon::HUD_ACTION_COUNT; ++i)
        {
            Moon::HudAction action = (Moon::HudAction)i;
            Moon::HudRect r = Moon::GetHudButtonRect(action, b->m_Camera.m_Width / scale);
            if (x >= r.m_X && x < r.m_X + r.m_Width && y >= r.m_Y && y < r.m_Y + r.m_Height)
            {
                if (action > Moon::HUD_HOLD || b->m_SelectedCount)
                {
                    if (action <= Moon::HUD_HOLD)
                    {
                        b->m_Autoplay = false;
                    }

                    ActivateHudButton(n, action);
                }

                return;
            }
        }

        if (y < 206 || y > b->m_Camera.m_Height / scale - 83)
        {
            return;
        }

        n->m_Dragging = true;
        n->m_DragX = n->m_MouseX;
        n->m_DragY = n->m_MouseY;
    }

    static void OnInput(Native* n, HInputContext input)
    {
        ++n->m_InputBatches;
        Moon::Battle* b = n->m_Battle;
        float ui_scale = Moon::GetHudScale(b->m_Camera.m_Width);
        dmHID::HMouse mouse = dmHID::GetMouse(n->m_Hid, 0);
        dmHID::MousePacket packet;
        if (mouse && dmHID::GetMousePacket(mouse, &packet))
        {
            n->m_MouseX = (float)packet.m_PositionX / n->m_PixelScale;
            n->m_MouseY = b->m_Camera.m_Height - (float)packet.m_PositionY / n->m_PixelScale;
        }

        if (n->m_MapDragging)
        {
            MapCamera(n);
        }

        InputActionIterator it = InputIterateActions(input);
        while (InputActionIterateNext(&it))
        {
            dmhash_t id = InputActionGetId(&it);
            bool down = InputActionGetValue(&it) > 0, pressed = InputActionIsPressed(&it), released = InputActionIsReleased(&it);
            if (id == dmHashString64("pan_up"))
            {
                n->m_Up = down;
            }
            else if (id == dmHashString64("pan_down"))
            {
                n->m_Down = down;
            }
            else if (id == dmHashString64("pan_left"))
            {
                n->m_Left = down;
            }
            else if (id == dmHashString64("pan_right"))
            {
                n->m_Right = down;
            }
            else if (id == dmHashString64("shift"))
            {
                n->m_Shift = down;
            }
            else if (id == dmHashString64("select"))
            {
                if (pressed)
                {
                    LeftPress(n);
                }

                if (released)
                {
                    if (n->m_Dragging)
                    {
                        Moon::Select(b, n->m_DragX, n->m_DragY, n->m_MouseX, n->m_MouseY, n->m_Shift, -1);
                        ++n->m_InputSelections;
                    }

                    n->m_Dragging = n->m_MapDragging = false;
                }
            }
            else if (id == dmHashString64("order") && pressed && n->m_MouseY >= 206 * ui_scale && n->m_MouseY < b->m_Camera.m_Height - 83 * ui_scale)
            {
                Moon::ScreenToGround(&b->m_Camera, n->m_MouseX, n->m_MouseY, &n->m_OrderX, &n->m_OrderZ);
                n->m_OrderPending = true;
                n->m_OrderAttack = b->m_AttackMove;
                b->m_AttackMove = false;
            }
            else if (id == dmHashString64("zoom_in") && down)
            {
                n->m_ZoomTarget = Clamp(n->m_ZoomTarget * 0.9f, 150, 1800);
            }
            else if (id == dmHashString64("zoom_out") && down)
            {
                n->m_ZoomTarget = Clamp(n->m_ZoomTarget / 0.9f, 150, 1800);
            }
            else if (pressed)
            {
                if (id == dmHashString64("autoplay"))
                {
                    ActivateHudButton(n, Moon::HUD_AUTOPLAY);
                }
                else if (id == dmHashString64("pause"))
                {
                    ActivateHudButton(n, Moon::HUD_PAUSE);
                }
                else if (id == dmHashString64("attack"))
                {
                    ActivateHudButton(n, Moon::HUD_ATTACK);
                }
                else if (id == dmHashString64("stop"))
                {
                    ActivateHudButton(n, Moon::HUD_HOLD);
                }
                else if (id == dmHashString64("all"))
                {
                    Moon::Select(b, 0, 0, 0, 0, n->m_Shift, Moon::KIND_COUNT);
                }
                else if (id == dmHashString64("scouts"))
                {
                    Moon::Select(b, 0, 0, 0, 0, n->m_Shift, Moon::SCOUT);
                }
                else if (id == dmHashString64("assault"))
                {
                    Moon::Select(b, 0, 0, 0, 0, n->m_Shift, Moon::ASSAULT);
                }
                else if (id == dmHashString64("siege"))
                {
                    Moon::Select(b, 0, 0, 0, 0, n->m_Shift, Moon::SIEGE);
                }
                else if (id == dmHashString64("support"))
                {
                    Moon::Select(b, 0, 0, 0, 0, n->m_Shift, Moon::SUPPORT);
                }
                else if (id == dmHashString64("more"))
                {
                    ActivateHudButton(n, Moon::HUD_MORE);
                }
                else if (id == dmHashString64("less"))
                {
                    ActivateHudButton(n, Moon::HUD_LESS);
                }
                else if (id == dmHashString64("restart"))
                {
                    ActivateHudButton(n, Moon::HUD_RESTART);
                }
                else if (id == dmHashString64("home"))
                {
                    ActivateHudButton(n, Moon::HUD_HOME);
                }
                else if (id == dmHashString64("overview"))
                {
                    ActivateHudButton(n, Moon::HUD_OVERVIEW);
                }
            }
        }
    }

    static dmExtension::Result Initialize(dmExtension::Params* params)
    {
        Native* n = (Native*)dmExtension::ParamsGetUserData(params);
        n->m_Registry = dmExtension::ParamsGetContextRegistry(params);
        HDataStoreContext store = (HDataStoreContext)ContextRegistryGet(dmExtension::ParamsGetContextRegistry(params), DATASTORE_CONTEXT_NAME);
        if (!store)
        {
            dmLogError("MoonBase requires engine.datastore=1 and the DataStore/event SDK branch");
            return dmExtension::RESULT_INIT_ERROR;
        }

        int32_t count = ConfigFileGetInt(params->m_ConfigFile, "moonbase.units", 32768);
        if (count < 512 || count > (int32_t)Moon::MAX_UNITS || (count & 1))
        {
            dmLogError("moonbase.units must be even and between 512 and %u", Moon::MAX_UNITS);
            return dmExtension::RESULT_INIT_ERROR;
        }

        n->m_PixelScale = 1;
        n->m_Battle = Moon::Create(store, (uint32_t)count);
        if (!n->m_Battle)
        {
            return dmExtension::RESULT_INIT_ERROR;
        }

        n->m_Battle->m_Camera.m_X = ConfigFileGetFloat(params->m_ConfigFile, "moonbase.camera_x", 0);
        n->m_Battle->m_Camera.m_Z = ConfigFileGetFloat(params->m_ConfigFile, "moonbase.camera_z", 0);
        n->m_Battle->m_Camera.m_Span = Clamp(ConfigFileGetFloat(params->m_ConfigFile, "moonbase.camera_span", 920), 150, 1800);
        n->m_ZoomTarget = n->m_Battle->m_Camera.m_Span;

        n->m_Hid = (dmHID::HContext)ContextRegistryGet(dmExtension::ParamsGetContextRegistry(params), HID_CONTEXT_NAME);
        n->m_Graphics = (dmGraphics::HContext)ContextRegistryGet(dmExtension::ParamsGetContextRegistry(params), GRAPHICS_CONTEXT_NAME);
        n->m_Geometry = Moon::CreateGeometry(params->m_ResourceFactory, n->m_Graphics);
        n->m_SmokeTest = ConfigFileGetInt(params->m_ConfigFile, "moonbase.smoke_test", 0) != 0;
        n->m_Offscreen = ConfigFileGetInt(params->m_ConfigFile, "moonbase.offscreen", 0) != 0;
        n->m_CaptureWidth = ConfigFileGetInt(params->m_ConfigFile, "moonbase.offscreen_width", 1440);
        n->m_CaptureHeight = ConfigFileGetInt(params->m_ConfigFile, "moonbase.offscreen_height", 900);
        n->m_CaptureFrame = ConfigFileGetInt(params->m_ConfigFile, "moonbase.capture_frame", 0);
        n->m_CapturePath = ConfigFileGetString(params->m_ConfigFile, "moonbase.capture_path", "moonbase.ppm");
        n->m_Graphics = (dmGraphics::HContext)ContextRegistryGet(dmExtension::ParamsGetContextRegistry(params), GRAPHICS_CONTEXT_NAME);
        static const luaL_Reg methods[] = { { "bind", Bind }, { "unbind", Unbind }, { "present", Present }, { "submit", Submit }, { "camera", Camera }, { "stats", Stats }, { 0, 0 } };
        lua_newtable(params->m_L);
        for (const luaL_Reg* method = methods; method->name; ++method)
        {
            lua_pushlightuserdata(params->m_L, n);
            lua_pushcclosure(params->m_L, method->func, 1);
            lua_setfield(params->m_L, -2, method->name);
        }

        lua_setglobal(params->m_L, "moonbase");
        dmLogInfo("MoonBase: %u units, 1536 x 1536 map, DataStore workers, native input and Bullet picking", n->m_Battle->m_Count);
        return dmExtension::RESULT_OK;
    }

    // Drive the SDK's device packets before resolved input dispatch, without OS events.
    static void SmokeInput(Native* n)
    {
        dmHID::HMouse mouse = dmHID::GetMouse(n->m_Hid, 0);
        dmHID::HKeyboard keyboard = dmHID::GetKeyboard(n->m_Hid, 0);
        float height = n->m_Battle->m_Camera.m_Height;
        if (n->m_Frames == 30)
        {
            dmHID::SetMousePosition(mouse, 100, (int32_t)height - 260);
            dmHID::SetMouseButton(mouse, dmHID::MOUSE_BUTTON_LEFT, true);
        }

        if (n->m_Frames == 31 || n->m_Frames == 32)
        {
            dmHID::SetMousePosition(mouse, 710, (int32_t)height - 680);
            dmHID::SetMouseButton(mouse, dmHID::MOUSE_BUTTON_LEFT, n->m_Frames == 31);
        }

        if (n->m_Frames == 40 || n->m_Frames == 41)
        {
            dmHID::SetMousePosition(mouse, 820, (int32_t)height - 450);
            dmHID::SetMouseButton(mouse, dmHID::MOUSE_BUTTON_RIGHT, n->m_Frames == 40);
        }

        if (n->m_Frames == 50)
        {
            n->m_SmokeFailed = !n->m_Battle->m_SelectedCount || !n->m_InputSelections || !n->m_Orders || !n->m_RayHits;
            dmLogInfo("MoonBase native input smoke: %s batches=%u drag_selections=%u selected=%u orders=%u terrain_hits=%u",
                      n->m_SmokeFailed ? "FAIL" : "PASS", n->m_InputBatches, n->m_InputSelections, n->m_Battle->m_SelectedCount, n->m_Orders, n->m_RayHits);
        }

        if (n->m_Frames == 60 || n->m_Frames == 61)
        {
            dmHID::SetKey(keyboard, dmHID::KEY_TAB, n->m_Frames == 60);
        }

        if (n->m_Frames == 65)
        {
            if (!n->m_Battle->m_Autoplay)
            {
                n->m_SmokeFailed = true;
            }

            dmLogInfo("MoonBase keyboard smoke: %s", n->m_Battle->m_Autoplay ? "PASS" : "FAIL");
        }

        const uint32_t click_frames[] = { 70, 73, 76, 79, 82 };
        const Moon::HudAction click_actions[] = { Moon::HUD_PAUSE, Moon::HUD_PAUSE, Moon::HUD_AUTOPLAY, Moon::HUD_ATTACK, Moon::HUD_MOVE };
        float scale = Moon::GetHudScale(n->m_Battle->m_Camera.m_Width);
        for (uint32_t i = 0; i < 5; ++i)
        {
            if (n->m_Frames != click_frames[i] && n->m_Frames != click_frames[i] + 1)
            {
                continue;
            }

            Moon::HudRect r = Moon::GetHudButtonRect(click_actions[i], n->m_Battle->m_Camera.m_Width / scale);
            dmHID::SetMousePosition(mouse, (int32_t)((r.m_X + r.m_Width * 0.5f) * scale), (int32_t)(height - (r.m_Y + r.m_Height * 0.5f) * scale));
            dmHID::SetMouseButton(mouse, dmHID::MOUSE_BUTTON_LEFT, n->m_Frames == click_frames[i]);
        }

        if ((n->m_Frames == 72 && !n->m_Battle->m_Paused) ||
            (n->m_Frames == 75 && n->m_Battle->m_Paused) ||
            (n->m_Frames == 78 && n->m_Battle->m_Autoplay) ||
            (n->m_Frames == 81 && !n->m_Battle->m_AttackMove) ||
            (n->m_Frames == 84 && n->m_Battle->m_AttackMove))
        {
            n->m_SmokeFailed = true;
        }

        if (n->m_Frames == 86)
        {
            dmLogInfo("MoonBase HUD smoke: %s scale=%.3f", n->m_SmokeFailed ? "FAIL" : "PASS", scale);
        }

        // Check camera controls, map picking and unit portraits after the button checks.
        if (n->m_Frames == 105 || n->m_Frames == 106 || n->m_Frames == 108 || n->m_Frames == 109)
        {
            Moon::HudAction action = n->m_Frames < 108 ? Moon::HUD_HOME : Moon::HUD_OVERVIEW;
            Moon::HudRect r = Moon::GetHudButtonRect(action, n->m_Battle->m_Camera.m_Width / scale);
            dmHID::SetMousePosition(mouse, (int32_t)((r.m_X + r.m_Width * 0.5f) * scale), (int32_t)(height - (r.m_Y + r.m_Height * 0.5f) * scale));
            dmHID::SetMouseButton(mouse, dmHID::MOUSE_BUTTON_LEFT, n->m_Frames == 105 || n->m_Frames == 108);
        }

        if ((n->m_Frames == 107 && n->m_Battle->m_Camera.m_Span != 500) ||
            (n->m_Frames == 110 && n->m_Battle->m_Camera.m_Span != 1250))
        {
            n->m_SmokeFailed = true;
        }

        if (n->m_Frames == 111 || n->m_Frames == 112)
        {
            dmHID::SetMousePosition(mouse, (int32_t)(70 * scale), (int32_t)(height - 140 * scale));
            dmHID::SetMouseButton(mouse, dmHID::MOUSE_BUTTON_LEFT, n->m_Frames == 111);
        }

        if (n->m_Frames == 113 && (fabsf(n->m_Battle->m_Camera.m_X + 331.85f) > 12 || fabsf(n->m_Battle->m_Camera.m_Z + 331.85f) > 12))
        {
            n->m_SmokeFailed = true;
        }

        if (n->m_Frames == 114 || n->m_Frames == 115)
        {
            dmHID::SetMousePosition(mouse, (int32_t)(278 * scale), (int32_t)(height - 130 * scale));
            dmHID::SetMouseButton(mouse, dmHID::MOUSE_BUTTON_LEFT, n->m_Frames == 114);
        }

        if (n->m_Frames == 118)
        {
            if (!n->m_Battle->m_SelectedCount || n->m_Battle->m_SelectedCount != n->m_Battle->m_Stats.m_SelectedKinds[Moon::SCOUT])
            {
                n->m_SmokeFailed = true;
            }

            dmLogInfo("MoonBase HUD navigation smoke: %s scale=%.3f", n->m_SmokeFailed ? "FAIL" : "PASS", scale);
        }

        if (n->m_Frames == 119 || n->m_Frames == 120)
        {
            dmHID::SetKey(keyboard, dmHID::KEY_TAB, n->m_Frames == 119);
        }

        if (n->m_Frames == 121 && !n->m_Battle->m_Autoplay)
        {
            n->m_SmokeFailed = true;
        }

        if (n->m_Frames == 122 || n->m_Frames == 123)
        {
            Moon::HudRect r = Moon::GetHudButtonRect(Moon::HUD_ATTACK, n->m_Battle->m_Camera.m_Width / scale);
            dmHID::SetMousePosition(mouse, (int32_t)((r.m_X + r.m_Width * 0.5f) * scale), (int32_t)(height - (r.m_Y + r.m_Height * 0.5f) * scale));
            dmHID::SetMouseButton(mouse, dmHID::MOUSE_BUTTON_LEFT, n->m_Frames == 122);
        }

        if (n->m_Frames == 125)
        {
            if (n->m_Battle->m_Autoplay || !n->m_Battle->m_AttackMove)
            {
                n->m_SmokeFailed = true;
            }

            dmLogInfo("MoonBase HUD command takeover smoke: %s", n->m_SmokeFailed ? "FAIL" : "PASS");
        }

        if (n->m_Frames == 129)
        {
            Reset(n, 32768);
        }

        const uint32_t population_frames[] = { 130, 133, 136, 139, 142 };
        const uint32_t population_counts[] = { 65536, 98304, 131072, 98304, 65536 };
        for (uint32_t i = 0; i < 5; ++i)
        {
            if (n->m_Frames == population_frames[i] || n->m_Frames == population_frames[i] + 1)
            {
                Moon::HudAction action = i < 3 ? Moon::HUD_MORE : Moon::HUD_LESS;
                Moon::HudRect r = Moon::GetHudButtonRect(action, n->m_Battle->m_Camera.m_Width / scale);
                dmHID::SetMousePosition(mouse, (int32_t)((r.m_X + r.m_Width * 0.5f) * scale), (int32_t)(height - (r.m_Y + r.m_Height * 0.5f) * scale));
                dmHID::SetMouseButton(mouse, dmHID::MOUSE_BUTTON_LEFT, n->m_Frames == population_frames[i]);
            }

            if (n->m_Frames == population_frames[i] + 2 && n->m_Battle->m_Count != population_counts[i])
            {
                n->m_SmokeFailed = true;
            }
        }

        if (n->m_Frames == 145)
        {
            dmLogInfo("MoonBase population steps smoke: %s units=%u", n->m_SmokeFailed ? "FAIL" : "PASS", n->m_Battle->m_Count);
        }

        // Keep the injected cumulative counter aligned with the polled device packet.
        if (n->m_Frames >= 150 && n->m_Frames <= 175)
        {
            dmHID::SetMouseWheel(mouse, n->m_Frames < 160 ? 1 : 0);
        }

        if (n->m_Frames == 153 && (fabsf(n->m_ZoomTarget - 1125) > 0.1f || n->m_Battle->m_Camera.m_Span >= 1250 || n->m_Battle->m_Camera.m_Span < 1125))
        {
            n->m_SmokeFailed = true;
        }

        if (n->m_Frames == 164 && (fabsf(n->m_ZoomTarget - 1250) > 0.1f || n->m_Battle->m_Camera.m_Span > 1250 || n->m_Battle->m_Camera.m_Span <= 1125))
        {
            n->m_SmokeFailed = true;
        }

        if (n->m_Frames == 175)
        {
            dmLogInfo("MoonBase zoom smoke: %s span=%.3f target=%.3f", n->m_SmokeFailed ? "FAIL" : "PASS", n->m_Battle->m_Camera.m_Span, n->m_ZoomTarget);
        }

        if (n->m_Frames == 180)
        {
            n->m_Battle->m_Stats.m_BaseHealth[1] = 0;
        }

        if (n->m_Frames == 181)
        {
            n->m_SmokeFailed |= n->m_Battle->m_Winner != 0;
            dmLogInfo("MoonBase victory smoke: %s", n->m_SmokeFailed ? "FAIL" : "PASS");
        }

    }

    static dmExtension::Result Update(dmExtension::Params* params)
    {
        Native* n = (Native*)dmExtension::ParamsGetUserData(params);
        float dt = 0;
        dmExtension::ParamsGetFloat(params, dmExtension::PARAM_DT, &dt);
        Moon::SetDeltaTime(n->m_Battle, dt);
        Moon::Camera* c = &n->m_Battle->m_Camera;
        // Exponential easing keeps wheel zoom responsive and consistent across frame rates.
        c->m_Span += (n->m_ZoomTarget - c->m_Span) * (1 - expf(-14 * dt));
        if (fabsf(n->m_ZoomTarget - c->m_Span) < 0.05f)
        {
            c->m_Span = n->m_ZoomTarget;
        }

        float speed = dt * c->m_Span * 0.7f;
        c->m_X = Clamp(c->m_X + (n->m_Right - n->m_Left) * speed, -700, 700);
        c->m_Z = Clamp(c->m_Z + (n->m_Down - n->m_Up) * speed, -700, 700);
        uint64_t now = dmTime::GetMonotonicTime();
        if (n->m_LastFrame)
        {
            n->m_FrameMs = (now - n->m_LastFrame) * 0.001f;
            n->m_FrameAverage = n->m_FrameAverage ? n->m_FrameAverage * 0.95f + n->m_FrameMs * 0.05f : n->m_FrameMs;

        }

        n->m_LastFrame = now;
        ++n->m_Frames;
        if (n->m_SmokeTest)
        {
            SmokeInput(n);
        }

        return dmExtension::RESULT_OK;
    }

    static void OnEvent(dmExtension::Params* params, dmhash_t id, const void* data, uint32_t size)
    {
        Native* n = (Native*)dmExtension::ParamsGetUserData(params);
        if (!n->m_Battle)
        {
            return;
        }

        if (id == ENGINE_EVENT_INPUT_ACTIONS && size == sizeof(EngineInputActionsEvent))
        {
            OnInput(n, ((const EngineInputActionsEvent*)data)->m_Input);
        }
        else if (id == PHYSICS_EVENT_POST_STEP && size == sizeof(PhysicsEvent))
        {
            ++n->m_PhysicsSteps;
            if (n->m_OrderPending)
            {
                const PhysicsEvent* event = (const PhysicsEvent*)data;
                // The world and query results are borrowed only for this broadcast.
                // Defold owns stepping, bodies, callbacks and their lifetime.
                const btDiscreteDynamicsWorld* world = (const btDiscreteDynamicsWorld*)event->m_World;
                btVector3 from(n->m_OrderX, 1000, n->m_OrderZ), to(n->m_OrderX, -100, n->m_OrderZ);
                btCollisionWorld::ClosestRayResultCallback hit(from, to);
                hit.m_collisionFilterGroup = btBroadphaseProxy::AllFilter;
                hit.m_collisionFilterMask = btBroadphaseProxy::AllFilter;
                world->rayTest(from, to, hit);
                float x = n->m_OrderX, z = n->m_OrderZ;
                if (hit.hasHit())
                {
                    x = hit.m_hitPointWorld.x();
                    z = hit.m_hitPointWorld.z();
                    ++n->m_RayHits;
                }

                Moon::Order(n->m_Battle, x, z, n->m_OrderAttack);
                ++n->m_Orders;
                n->m_OrderPending = false;
            }
        }
        else if (id == ENGINE_WINDOW_EVENT && size == sizeof(EngineWindowEvent))
        {
            const EngineWindowEvent* event = (const EngineWindowEvent*)data;
            if (event->m_Type == ENGINE_WINDOW_EVENT_FOCUS_LOST)
            {
                n->m_Up = n->m_Down = n->m_Left = n->m_Right = n->m_Shift = false;
                n->m_Dragging = n->m_MapDragging = false;
            }
        }
    }

    static dmExtension::Result Capture(dmExtension::Params* params)
    {
        Native* n = (Native*)dmExtension::ParamsGetUserData(params);
        if (!n->m_CaptureFrame || n->m_Frames != n->m_CaptureFrame)
        {
            return dmExtension::RESULT_OK;
        }

        uint32_t width = dmGraphics::GetWindowWidth(n->m_Graphics), height = dmGraphics::GetWindowHeight(n->m_Graphics);
        if (n->m_Offscreen)
        {
            width = n->m_CaptureWidth;
            height = n->m_CaptureHeight;
        }

        uint32_t size = width * height * 4;
        unsigned char* pixels = (unsigned char*)malloc(size);
        if (!pixels)
        {
            return dmExtension::RESULT_OK;
        }

        dmGraphics::ReadPixels(n->m_Graphics, 0, 0, width, height, pixels, size);
        FILE* file = fopen(n->m_CapturePath, "wb");
        if (file)
        {
            fprintf(file, "P6\n%u %u\n255\n", width, height);
            for (uint32_t y = 0; y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    // This Metal SDK renders offscreen textures in the opposite vertical orientation.
                    unsigned char* p = pixels + ((n->m_Offscreen ? height - 1 - y : y) * width + x) * 4;
                    unsigned char rgb[] = { p[2], p[1], p[0] };
                    fwrite(rgb, 1, 3, file);
                }
            }

            fclose(file);
            dmLogInfo("MoonBase captured frame %u to %s", n->m_Frames, n->m_CapturePath);
        }
        else
        {
            dmLogError("Could not open capture path: %s", n->m_CapturePath);
        }

        free(pixels);
        return dmExtension::RESULT_OK;
    }

    static dmExtension::Result Finalize(dmExtension::Params* params)
    {
        Native* n = (Native*)dmExtension::ParamsGetUserData(params);
        if (n->m_Geometry)
        {
            Moon::DestroyGeometry(n->m_Geometry);
        }

        Moon::Destroy(n->m_Battle);
        n->m_Geometry = 0;
        n->m_Battle = 0;
        return dmExtension::RESULT_OK;
    }

    static dmExtension::Result Register(dmExtension::HExtension extension)
    {
        Native* n = new Native();
        memset(n, 0, sizeof(*n));
        dmExtension::SetUserData(extension, n);
        dmExtension::SetInitializeFn(extension, Initialize);
        dmExtension::SetUpdateFn(extension, Update);
        dmExtension::SetFinalizeFn(extension, Finalize);
        dmExtension::SetEventFn(extension, OnEvent);
        dmExtension::SetPostRenderFn(extension, Capture);
        return dmExtension::RESULT_OK;
    }

    static dmExtension::Result Unregister(dmExtension::HExtension extension)
    {
        delete (Native*)dmExtension::GetUserData(extension);
        return dmExtension::RESULT_OK;
    }

}
DM_DECLARE_EXTENSION(MoonBase, Register, Unregister)
