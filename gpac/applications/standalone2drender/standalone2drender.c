/*
 *					GPAC Multimedia Framework
 *
 *			Authors: Cyril Concolato
 *				Copyright (c) 2005-200X ENST
 *					All rights reserved
 *
 *  This file is part of GPAC / standalone 2D rendering lib (render2D + FT + RAW OUT + soft raster )
 *
 *  GPAC is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  GPAC is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *   
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

#include "standalone2drender.h"
#include <gpac/options.h>
#include "render2d.h"
#include "stacks2d.h"
#include "visualsurface2d.h"
#include "ft_font.h"

void SR_ResetFrameRate(GF_Renderer *);
void *EVG_LoadRenderer();
void *NewVideoOutput();
void *NewVisualRenderer();
GF_Err R2D_GetSurfaceAccess(VisualSurface2D *surf);
void R2D_ReleaseSurfaceAccess(VisualSurface2D *surf);
Bool R2D_SupportsFormat(VisualSurface2D *surf, u32 pixel_format);
void R2D_DrawBitmap(VisualSurface2D *surf, struct _gf_sr_texture_handler *txh, GF_IRect *clip, GF_Rect *unclip);
GF_FontRaster *FT_Load();
void FT_Delete(GF_FontRaster *);

static GF_Err SA2DR_InitFontEngine(GF_FontRaster *dr)
{
	FTBuilder *ftpriv = (FTBuilder *)dr->priv;

	/*inits freetype*/
	if (FT_Init_FreeType(&ftpriv->library) ) return GF_IO_ERR;

	/*remove the final delimiter*/
    ftpriv->font_dir = "C:\\WINDOWS\\Fonts";
	strcpy(ftpriv->font_serif,"Arial");
	strcpy(ftpriv->font_sans,"Times New Roman");
	strcpy(ftpriv->font_fixed,"Courier New");
	return GF_OK;
}

static void SA2DR_SetFontEngine(GF_Renderer *sr)
{
	GF_FontRaster *ifce = FT_Load();

	/*cannot init font engine*/
	if (SA2DR_InitFontEngine(ifce) != GF_OK) {
		FT_Delete(ifce);
		return;
	}
	sr->font_engine = ifce;
}

static GF_Err SA2DR_LoadRenderer(GF_VisualRenderer *vr, GF_Renderer *compositor)
{
	Render2D *sr;
	if (vr->user_priv) return GF_BAD_PARAM;

	sr = malloc(sizeof(Render2D));
	if (!sr) return GF_OUT_OF_MEM;
	memset(sr, 0, sizeof(Render2D));

	sr->compositor = compositor;

	sr->strike_bank = gf_list_new();
	sr->surfaces_2D = gf_list_new();

	sr->top_effect = malloc(sizeof(RenderEffect2D));
	memset(sr->top_effect, 0, sizeof(RenderEffect2D));
	sr->top_effect->sensors = gf_list_new();
	sr->sensors = gf_list_new();
	
	/*and create main surface*/
	sr->surface = NewVisualSurface2D();
	sr->surface->GetSurfaceAccess = R2D_GetSurfaceAccess;
	sr->surface->ReleaseSurfaceAccess = R2D_ReleaseSurfaceAccess;

	sr->surface->DrawBitmap = R2D_DrawBitmap;
	sr->surface->SupportsFormat = R2D_SupportsFormat;
	sr->surface->render = sr;
	sr->surface->pixel_format = 0;
	gf_list_add(sr->surfaces_2D, sr->surface);

	sr->zoom = sr->scale_x = sr->scale_y = 1.0;
	vr->user_priv = sr;

	/*load options*/
	//sr->top_effect->trav_flags |= TF_RENDER_DIRECT;
	sr->scalable_zoom = 1;
	sr->enable_yuv_hw = 0;
	return GF_OK;
}

#ifdef DANAE
SR_SetRenderingSession(void *sr, void*session)
{
	((GF_Renderer*)sr)->danae_session = session;
}
#endif

GF_Renderer *SR_NewStandaloneRenderer()
{
	GF_GLConfig cfg, *gl_cfg;
	GF_Renderer *tmp;
	SAFEALLOC(tmp, sizeof(GF_Renderer))
	SAFEALLOC(tmp->user, sizeof(GF_User))

	tmp->visual_renderer = NewVisualRenderer();

	memset(&cfg, 0, sizeof(cfg));
	cfg.double_buffered = 1;
	gl_cfg = tmp->visual_renderer->bNeedsGL ? &cfg : NULL;

	tmp->video_out = NewVideoOutput();
	tmp->video_out->evt_cbk_hdl = tmp;
	tmp->video_out->on_event = NULL;

	tmp->r2d = EVG_LoadRenderer();

	/*and init*/
	if (SA2DR_LoadRenderer(tmp->visual_renderer, tmp) != GF_OK) {
		tmp->video_out->Shutdown(tmp->video_out);
		free(tmp);
		return NULL;
	}

	tmp->mx = gf_mx_new();
	tmp->textures = gf_list_new();
	tmp->frame_rate = 30.0;	
	tmp->frame_duration = 33;
	tmp->time_nodes = gf_list_new();
	tmp->events = gf_list_new();
	tmp->ev_mx = gf_mx_new();
	
	SR_ResetFrameRate(tmp);	
	
	/*set font engine if any*/
	SA2DR_SetFontEngine(tmp);
	
	tmp->extra_scenes = gf_list_new();
	tmp->interaction_level = GF_INTERACT_NORMAL | GF_INTERACT_INPUT_SENSOR;
	tmp->antiAlias = GF_ANTIALIAS_FULL;
	return tmp;
}
