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

#ifndef MOONBASE_GEOMETRY_H
#define MOONBASE_GEOMETRY_H
#include "simulation.h"
#include <dmsdk/dlib/array.h>
#include <dmsdk/resource/resource.h>
#include <dmsdk/graphics/graphics.h>
#include <dmsdk/render/render.h>

namespace dmGameSystem
{
    struct ModelResource;
    struct MaterialResource;
}

namespace Moon
{
    struct Vertex
    {
        float m_X, m_Y, m_Z, m_R, m_G, m_B, m_A;
    };
    struct Color
    {
        float m_R, m_G, m_B, m_A;
    };
    struct Mesh
    {
        dmArray<Vertex>                 m_Vertices;
        dmGraphics::HVertexBuffer       m_Buffer;
        uint32_t                        m_BufferBytes;
        dmGameSystem::MaterialResource* m_Material;
        dmRender::RenderObject          m_Render;
    };
    // Two vec4 attributes per unit: position/facing and team/selection/flash/scale.
    struct Instance
    {
        float m_X, m_Z, m_DirectionX, m_DirectionZ;
        float m_Team, m_Selected, m_Flash, m_Scale;
    };
    struct InstanceBatch
    {
        dmArray<Instance>            m_Instances;
        dmGraphics::HVertexBuffer    m_Buffer;
        uint32_t                     m_BufferBytes;
        dmGameSystem::ModelResource* m_Model;
        dmRender::RenderObject       m_Render;
    };
    struct Geometry
    {
        dmResource::HFactory            m_Factory;
        dmGraphics::HContext            m_Graphics;
        dmRender::HRenderContext        m_RenderContext;
        dmGraphics::HVertexDeclaration  m_VertexDeclaration;
        dmGraphics::HVertexDeclaration  m_ModelDeclaration;
        dmGraphics::HVertexDeclaration  m_InstanceDeclaration;
        Mesh                            m_Terrain, m_Units, m_Overlay, m_OverlayTop;
        Mesh                            m_SelectionMesh, m_RadarMesh;
        InstanceBatch                   m_Models[KIND_COUNT * 2];
        InstanceBatch                   m_Selections, m_Radar;
        dmRender::HRenderItemDispatch   m_Dispatch;
        dmRender::HRenderItem           m_Items[14];
        dmRender::RenderObject*         m_RenderObjects[14];
        uint32_t                        m_Visible;
    };
    enum HudAction
    {
        HUD_MOVE, HUD_ATTACK, HUD_HOLD, HUD_PAUSE, HUD_AUTOPLAY,
        HUD_RESTART, HUD_LESS, HUD_MORE, HUD_HOME, HUD_OVERVIEW, HUD_ACTION_COUNT
    };
    struct HudRect
    {
        float m_X, m_Y, m_Width, m_Height;
    };
    // The command console uses a 1440-wide canvas; the battle camera uses screen coordinates.
    float   GetHudScale(float screen_width);
    HudRect GetHudButtonRect(HudAction action, float canvas_width);

    Geometry* CreateGeometry(dmResource::HFactory factory, dmGraphics::HContext graphics);
    void      DestroyGeometry(Geometry* geometry);
    bool      BindGeometry(Geometry* geometry, dmRender::HRenderContext render);
    void      Present(Geometry* geometry, Battle* battle, bool dragging, float x0, float y0, float x1, float y1);
    void      SubmitGeometry(Geometry* geometry);
}
#endif
