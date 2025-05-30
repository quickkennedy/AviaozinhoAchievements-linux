/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
//gl_sky.c

#include "quakedef.h"

#define	MAX_CLIP_VERTS 64

float Fog_GetDensity(void);
float *Fog_GetColor(void);

extern	int	rs_skypolys; // for r_speeds readout
extern	int	rs_skypasses; // for r_speeds readout

float	skyflatcolor[3];
float	skymins[2][6], skymaxs[2][6];

qboolean skyroom_drawing;
qboolean skyroom_drawn;
qboolean skyroom_enabled;
vec4_t skyroom_origin;
vec4_t skyroom_orientation;

char	skybox_name[1024]; //name of current skybox, or "" if no skybox
qboolean externalskyloaded; // woods #fastsky2

gltexture_t	*skybox_textures[6];
gltexture_t	*solidskytexture, *alphaskytexture;

cvar_t r_fastsky = {"r_fastsky", "0", CVAR_ARCHIVE};
cvar_t r_fastskycolor = {"r_fastskycolor", "", CVAR_ARCHIVE}; // woods #fastskycolor
cvar_t r_sky_quality = {"r_sky_quality", "12", CVAR_NONE};
cvar_t r_skyalpha = {"r_skyalpha", "1", CVAR_NONE};
cvar_t r_skyfog = {"r_skyfog","0.5",CVAR_ARCHIVE};
cvar_t r_skyspeed = {"r_skyspeed","1",CVAR_ARCHIVE}; // woods #skyspeed

int		skytexorder[6] = {0,2,1,3,4,5}; //for skybox

vec3_t	skyclip[6] = {
	{1,1,0},
	{1,-1,0},
	{0,-1,1},
	{0,1,1},
	{1,0,1},
	{-1,0,1}
};

int	st_to_vec[6][3] =
{
	{3,-1,2},
	{-3,1,2},
	{1,3,2},
	{-1,-3,2},
	{-2,-1,3},		// straight up
	{2,-1,-3}		// straight down
};

int	vec_to_st[6][3] =
{
	{-2,3,1},
	{2,3,-1},
	{1,3,2},
	{-1,3,-2},
	{-2,-1,3},
	{-2,1,-3}
};

float	skyfog; // ericw

//==============================================================================
//
//  INIT
//
//==============================================================================

/*
=============
Sky_LoadTexture

A sky texture is 256*128, with the left side being a masked overlay
==============
*/
void Sky_LoadTexture (qmodel_t *mod, texture_t *mt, enum srcformat fmt, unsigned int srcwidth, unsigned int height)
{
	char		texturename[64];
	int			i, p, r, g, b, count;
	byte		*src;
	byte	*front_data;
	byte	*back_data;
	unsigned	*rgba;
	int rows, columns;
	int bb,bw,bh;
	int width = srcwidth/2;

	TexMgr_BlockSize(fmt, &bb, &bw, &bh);
	columns = (width+bw-1) / bw;
	rows = (height+bh-1) / bh;

	front_data = Hunk_AllocName (bb*columns*rows*2, "skytex");
	back_data = front_data+bb*columns*rows;

	src = (byte *)(mt+1);

// extract back layer and upload
	for (i=0 ; i<rows ; i++)
		memcpy(back_data+bb*i*columns, src+bb*(i*columns*2 + columns), columns*bb);

	q_snprintf(texturename, sizeof(texturename), "%s:%s_back", mod->name, mt->name);
	mt->gltexture = solidskytexture = TexMgr_LoadImage (mod, texturename, width, height, fmt, back_data, "", (src_offset_t)back_data, TEXPREF_NONE);

// extract front layer and upload
	for (i=0 ; i<rows ; i++)
		memcpy(front_data+bb*i*columns, src+bb*(i*columns*2), columns*bb);
	if (fmt == SRC_INDEXED)
	{	//the lame texmgr only knows one transparent index...
		for (i=0 ; i<width*height ; i++)
		{
			if (front_data[i] == 0)
				front_data[i] = 255;
		}
	}
	q_snprintf(texturename, sizeof(texturename), "%s:%s_front", mod->name, mt->name);
	mt->fullbright = alphaskytexture = TexMgr_LoadImage (mod, texturename, width, height, fmt, front_data, "", (src_offset_t)front_data, TEXPREF_ALPHA);

	r = g = b = count = 0;

	const char* skycolor_str = r_fastskycolor.string;
	plcolour_t sky_color = CL_PLColours_Parse(skycolor_str);
	byte* rgb_temp; // temporary pointer for RGB values
	byte rgb[3]; // local array to copy RGB values safely

	int use_default = sky_color.type == 0;

	if (!use_default) // If custom color is set
	{
		rgb_temp = CL_PLColours_ToRGB(&sky_color);
		if (rgb_temp) // copy the RGB values to a local array for safe usage
		{
			rgb[0] = rgb_temp[0];
			rgb[1] = rgb_temp[1];
			rgb[2] = rgb_temp[2];

			r = rgb[0];
			g = rgb[1];
			b = rgb[2];
		}
		else
		{
			r = g = b = 0.0f; // fallback to black if RGB conversion fails
		}
		skyflatcolor[0] = (float)r / 255.0f;
		skyflatcolor[1] = (float)g / 255.0f;
		skyflatcolor[2] = (float)b / 255.0f;
	}
// calculate r_fastsky color based on average of all opaque foreground colors, if we can.
	else if (fmt == SRC_INDEXED)
	{
		for (i=0 ; i<width*height ; i++)
		{
			p = src[i];
			if (p != 0)
			{
				rgba = &d_8to24table[p];
				r += ((byte *)rgba)[0];
				g += ((byte *)rgba)[1];
				b += ((byte *)rgba)[2];
				count++;
			}
		}

		skyflatcolor[0] = (float)r/(count*255);
		skyflatcolor[1] = (float)g/(count*255);
		skyflatcolor[2] = (float)b/(count*255);
	}
}

/*
=============
Sky_LoadTextureQ64

Quake64 sky textures are 32*64
==============
*/
void Sky_LoadTextureQ64 (qmodel_t *mod, texture_t *mt)
{
	char		texturename[64];
	unsigned	i, p, r, g, b, count, halfheight, *rgba;
	byte		*front, *back, *front_rgba;

	if (mt->width != 32 || mt->height != 64)
	{
		Con_DWarning ("Q64 sky texture %s is %d x %d, expected 32 x 64\n", mt->name, mt->width, mt->height);
		if (mt->width < 1 || mt->height < 2)
			return;
	}

	// pointers to both layer textures
	halfheight = mt->height / 2;
	front = (byte *)(mt+1);
	back = (byte *)(mt+1) + mt->width*halfheight;
	front_rgba = (byte *) Hunk_AllocName (4*mt->width*halfheight, "q64_skytex");

	// Normal indexed texture for the back layer
	q_snprintf(texturename, sizeof(texturename), "%s:%s_back", mod->name, mt->name);
	mt->gltexture = solidskytexture = TexMgr_LoadImage (mod, texturename, mt->width, halfheight, SRC_INDEXED, back, "", (src_offset_t)back, TEXPREF_NONE);

	// front layer, convert to RGBA and upload
	p = r = g = b = count = 0;

	const char* skycolor_str = r_fastskycolor.string;
	plcolour_t sky_color = CL_PLColours_Parse(skycolor_str);
	byte* rgb_temp; // temporary pointer for RGB values
	byte rgb[3]; // local array to copy RGB values safely

	int use_default = sky_color.type == 0;

	if (!use_default)
	{
		rgb_temp = CL_PLColours_ToRGB(&sky_color);
		if (rgb_temp) // copy the RGB values to a local array for safe usage
		{
			rgb[0] = rgb_temp[0];
			rgb[1] = rgb_temp[1];
			rgb[2] = rgb_temp[2];

			skyflatcolor[0] = (float)rgb[0] / 255.0f;
			skyflatcolor[1] = (float)rgb[1] / 255.0f;
			skyflatcolor[2] = (float)rgb[2] / 255.0f;
		}
		else
		{
			skyflatcolor[0] = skyflatcolor[1] = skyflatcolor[2] = 0.0f; // fallback to black if RGB conversion fails
		}
	}
	else
	{
		for (i = mt->width * halfheight; i != 0; i--)
		{
			rgba = &d_8to24table[*front++];

			// RGB
			front_rgba[p++] = ((byte*)rgba)[0];
			front_rgba[p++] = ((byte*)rgba)[1];
			front_rgba[p++] = ((byte*)rgba)[2];
			// Alpha
			front_rgba[p++] = 128; // this look ok to me!

			// Fast sky
			r += ((byte *)rgba)[0];
			g += ((byte *)rgba)[1];
			b += ((byte *)rgba)[2];
			count++;
		}
	}

	q_snprintf(texturename, sizeof(texturename), "%s:%s_front", mod->name, mt->name);
	mt->fullbright = alphaskytexture = TexMgr_LoadImage (mod, texturename, mt->width, halfheight, SRC_RGBA, front_rgba, "", (src_offset_t)front_rgba, TEXPREF_ALPHA);

	if (use_default)
	{
		// calculate r_fastsky color based on average of all opaque foreground colors
		skyflatcolor[0] = (float)r/(count*255);
		skyflatcolor[1] = (float)g/(count*255);
		skyflatcolor[2] = (float)b/(count*255);
	}
}

/*
=============
Sky_LoadExternalTextures -- woods #extsky
Load external sky textures
==============
*/
qboolean Sky_LoadExternalTextures (qmodel_t* mod, texture_t* mt)
{
	if (r_fastsky.value == 1)
		return false;
	
	char texturename_back[MAX_OSPATH], texturename_front[MAX_OSPATH];
	char mapname[MAX_OSPATH];
	byte* back_data = NULL, * front_data = NULL;
	int fwidth_back = 0, fheight_back = 0, fwidth_front = 0, fheight_front = 0;
	enum srcformat rfmt_back = SRC_EXTERNAL, rfmt_front = SRC_EXTERNAL;
	qboolean malloced_back = false, malloced_front = false;

	int mark = Hunk_LowMark();

	COM_StripExtension(mod->name + 5, mapname, sizeof(mapname));

	q_snprintf(texturename_back, sizeof(texturename_back), "textures/%s/%s_back", mapname, mt->name);
	back_data = Image_LoadImage(texturename_back, &fwidth_back, &fheight_back, &rfmt_back, &malloced_back);
	if (!back_data) {
		q_snprintf(texturename_back, sizeof(texturename_back), "textures/%s_back", mt->name);
		back_data = Image_LoadImage(texturename_back, &fwidth_back, &fheight_back, &rfmt_back, &malloced_back);
	}

	q_snprintf(texturename_front, sizeof(texturename_front), "textures/%s/%s_front", mapname, mt->name);
	front_data = Image_LoadImage(texturename_front, &fwidth_front, &fheight_front, &rfmt_front, &malloced_front);
	if (!front_data) {
		q_snprintf(texturename_front, sizeof(texturename_front), "textures/%s_front", mt->name);
		front_data = Image_LoadImage(texturename_front, &fwidth_front, &fheight_front, &rfmt_front, &malloced_front);
	}

	if (back_data && front_data) // If both textures loaded successfully
	{
		mt->gltexture = solidskytexture = TexMgr_LoadImage(mod, texturename_back, fwidth_back, fheight_back, rfmt_back, back_data, texturename_back, 0, TEXPREF_NONE);
		mt->fullbright = alphaskytexture = TexMgr_LoadImage(mod, texturename_front, fwidth_front, fheight_front, rfmt_front, front_data, texturename_front, 0, TEXPREF_ALPHA);

		if (malloced_back) free(back_data);
		if (malloced_front) free(front_data);
		Hunk_FreeToLowMark(mark);
		externalskyloaded = true; // #fastsky2
		return true; // success: both textures loaded
	}

	if (malloced_back) free(back_data);
	if (malloced_front) free(front_data);
	Hunk_FreeToLowMark(mark);

	externalskyloaded = false; // #fastsky2
	return false;
}

/*
==================
Sky_LoadSkyBox
==================
*/
const char	*suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
void Sky_LoadSkyBox (const char *name)
{
	int		i, mark, width, height;
	char	filename[MAX_OSPATH];
	byte	*data;
	qboolean nonefound = true;
	qboolean malloced;

	if (strcmp(skybox_name, name) == 0)
		return; //no change

	//purge old textures
	for (i=0; i<6; i++)
	{
		if (skybox_textures[i] && skybox_textures[i] != notexture)
			TexMgr_FreeTexture (skybox_textures[i]);
		skybox_textures[i] = NULL;
	}

	//turn off skybox if sky is set to ""
	if (name[0] == 0)
	{
		skybox_name[0] = 0;
		return;
	}

	//load textures
	for (i=0; i<6; i++)
	{
		enum srcformat fmt;
		mark = Hunk_LowMark ();
		q_snprintf (filename, sizeof(filename), "gfx/env/%s%s", name, suf[i]);
		data = Image_LoadImage (filename, &width, &height, &fmt, &malloced);
		if (data)
		{
			skybox_textures[i] = TexMgr_LoadImage (cl.worldmodel, filename, width, height, fmt, data, filename, 0, TEXPREF_NONE);
			nonefound = false;
		}
		else
		{
			//Con_Printf ("Couldn't load skybox %s\n", filename); // woods
			skybox_textures[i] = notexture;
		}
		if (malloced)
			free(data);
		Hunk_FreeToLowMark (mark);
	}

	if (nonefound) // woods, verbose missing sky + limit spam
	{
		int length = strlen(filename);
		filename[length - 2] = '\0';
		Con_Printf("this map uses an external sky, could't load skybox %s\n", filename);
	}

	if (nonefound) // go back to scrolling sky if skybox is totally missing
	{
		for (i=0; i<6; i++)
		{
			if (skybox_textures[i] && skybox_textures[i] != notexture)
				TexMgr_FreeTexture (skybox_textures[i]);
			skybox_textures[i] = NULL;
		}
		skybox_name[0] = 0;
		return;
	}

	q_strlcpy(skybox_name, name, sizeof(skybox_name));
}

/*
=================
Sky_ClearAll

Called on map unload/game change to avoid keeping pointers to freed data
=================
*/
void Sky_ClearAll (void)
{
	int i;

	skyroom_enabled = false;
	skybox_name[0] = 0;
	for (i=0; i<6; i++)
		skybox_textures[i] = NULL;
	solidskytexture = NULL;
	alphaskytexture = NULL;
}

/*
=================
Sky_NewMap
=================
*/
void Sky_NewMap (void)
{
	char	key[128], value[4096];
	const char	*data;

	skyfog = r_skyfog.value;

	//
	// read worldspawn (this is so ugly, and shouldn't it be done on the server?)
	//
	data = cl.worldmodel->entities;
	if (!data)
		return; //FIXME: how could this possibly ever happen? -- if there's no
	// worldspawn then the sever wouldn't send the loadmap message to the client

	data = COM_Parse(data);
	if (!data) //should never happen
		return; // error
	if (com_token[0] != '{') //should never happen
		return; // error
	while (1)
	{
		data = COM_Parse(data);
		if (!data)
			return; // error
		if (com_token[0] == '}')
			break; // end of worldspawn
		if (com_token[0] == '_')
			q_strlcpy(key, com_token + 1, sizeof(key));
		else
			q_strlcpy(key, com_token, sizeof(key));
		while (key[0] && key[strlen(key)-1] == ' ') // remove trailing spaces
			key[strlen(key)-1] = 0;
		data = COM_Parse(data);
		if (!data)
			return; // error
		q_strlcpy(value, com_token, sizeof(value));

		if (!strcmp("sky", key))
			Sky_LoadSkyBox(value);
		else if (!strcmp("skyroom", key))
		{	//"_skyroom" "X Y Z". ideally the gamecode would do this with an entity, but people want to use the vanilla gamecode from 1996 for some reason.
			const char *t = COM_Parse(value);
			skyroom_origin[0] = atof(com_token);
			t = COM_Parse(t);
			skyroom_origin[1] = atof(com_token);
			t = COM_Parse(t);
			skyroom_origin[2] = atof(com_token);
			t = COM_Parse(t);
			skyroom_origin[3] = atof(com_token);
			skyroom_enabled = true;

			t = COM_Parse(t);
			skyroom_orientation[3] = atof(com_token);
			t = COM_Parse(t);
			skyroom_orientation[0] = atof(com_token);
			t = COM_Parse(t);
			skyroom_orientation[1] = atof(com_token);
			t = COM_Parse(t);
			skyroom_orientation[2] = atof(com_token);
		}

		else if (!strcmp("skyfog", key))
			skyfog = atof(value);

#if 1 /* also accept non-standard keys */
		else if (!strcmp("skyname", key)) //half-life
			Sky_LoadSkyBox(value);
		else if (!strcmp("qlsky", key)) //quake lives
			Sky_LoadSkyBox(value);
#endif
	}
}

/*
=================
Sky_SkyCommand_f
=================
*/
void Sky_SkyCommand_f (void)
{
	switch (Cmd_Argc())
	{
	case 1:
		Con_Printf("\"sky\" is \"%s\"\n", skybox_name);
		break;
	case 2:
		Sky_LoadSkyBox(Cmd_Argv(1));
		break;
	default:
		Con_Printf("usage: sky <skyname>\n");
	}
}

static void Sky_SkyRoomCommand_f (void)
{
	switch (Cmd_Argc())
	{
	case 1:
		if (skyroom_enabled)
			Con_Printf("\"skyroom\" is \"%f %f %f %f %f %f %f %f\"\n", skyroom_origin[0],skyroom_origin[1],skyroom_origin[2],skyroom_origin[3], skyroom_orientation[3],skyroom_orientation[0],skyroom_orientation[1],skyroom_orientation[2]);
		else
			Con_Printf("\"skyroom\" is \"\"\n");
		break;
	case 4:	//xyz
	case 5:	//xyz paralax
	case 6:	//+speed
	case 7:	//+axis_x
	case 8:	//+axis_y
	case 9:	//+axis_z
		skyroom_enabled = true;
		skyroom_origin[0] = atof(Cmd_Argv(1));
		skyroom_origin[1] = atof(Cmd_Argv(2));
		skyroom_origin[2] = atof(Cmd_Argv(3));
		skyroom_origin[3] = atof(Cmd_Argv(4));	//paralax

		skyroom_orientation[3] = atof(Cmd_Argv(5));	//speed
		skyroom_orientation[0] = atof(Cmd_Argv(6));
		skyroom_orientation[1] = atof(Cmd_Argv(7));
		skyroom_orientation[2] = atof(Cmd_Argv(8));
		break;
	case 2:	//x
		if (!*Cmd_Argv(1) || !q_strcasecmp(Cmd_Argv(1), "off"))
		{
			skyroom_enabled = false;
			break;
		}
		//fallthrough
	case 3:	//xy
	default:
		Con_Printf("usage: skyroom origin_x origin_y origin_z paralax_scale speed axis_x axis_y axis_z\n");
	}
}

/*
====================
R_SetSkyfog_f -- ericw
====================
*/
static void R_SetSkyfog_f (cvar_t *var)
{
// clear any skyfog setting from worldspawn
	skyfog = var->value;
}

/*
===============
SKy_Color_Completion_f -- woods #iwtabcomplete #fastskycolor
===============
*/
static void SKy_Color_Completion_f(cvar_t* cvar, const char* partial)
{
	Con_AddToTabList("0x465f6b", partial, "blue", NULL); // #demolistsort add arg
	Con_AddToTabList("0x211529", partial, "purple", NULL); // #demolistsort add arg
	Con_AddToTabList("0x000000", partial, "black", NULL); // #demolistsort add arg
	Con_AddToTabList("0x1e1e1e", partial, "dark grey", NULL); // #demolistsort add arg
	Con_AddToTabList("\"\"", partial, NULL, NULL); // #demolistsort add arg for literal ""

	for (int i = 1; i <= 13; i++)
	{
		char num_str[3];
		snprintf(num_str, sizeof(num_str), "%d", i);
		Con_AddToTabList(num_str, partial, NULL, NULL); // #demolistsort add arg
	}

	return;
}

/*
=============
Sky_Init
=============
*/
void Sky_Init (void)
{
	int		i;

	Cvar_RegisterVariable (&r_fastsky);
	Cvar_RegisterVariable (&r_fastskycolor); // woods #fastskycolor
	Cvar_SetCompletion (&r_fastskycolor, &SKy_Color_Completion_f); // woods #iwtabcomplete #fastskycolor
	Cvar_RegisterVariable (&r_sky_quality);
	Cvar_RegisterVariable (&r_skyalpha);
	Cvar_RegisterVariable (&r_skyfog);
	Cvar_SetCallback (&r_skyfog, R_SetSkyfog_f);
	Cvar_RegisterVariable (&r_skyspeed); // woods #skyspeed

	Cmd_AddCommand ("sky",Sky_SkyCommand_f);
	Cmd_AddCommand ("skyroom",Sky_SkyRoomCommand_f);

	skybox_name[0] = 0;
	for (i=0; i<6; i++)
		skybox_textures[i] = NULL;
}

//==============================================================================
//
//  PROCESS SKY SURFS
//
//==============================================================================

/*
=================
Sky_ProjectPoly

update sky bounds
=================
*/
void Sky_ProjectPoly (int nump, vec3_t vecs)
{
	int		i,j;
	vec3_t	v, av;
	float	s, t, dv;
	int		axis;
	float	*vp;

	// decide which face it maps to
	VectorCopy (vec3_origin, v);
	for (i=0, vp=vecs ; i<nump ; i++, vp+=3)
	{
		VectorAdd (vp, v, v);
	}
	av[0] = fabs(v[0]);
	av[1] = fabs(v[1]);
	av[2] = fabs(v[2]);
	if (av[0] > av[1] && av[0] > av[2])
	{
		if (v[0] < 0)
			axis = 1;
		else
			axis = 0;
	}
	else if (av[1] > av[2] && av[1] > av[0])
	{
		if (v[1] < 0)
			axis = 3;
		else
			axis = 2;
	}
	else
	{
		if (v[2] < 0)
			axis = 5;
		else
			axis = 4;
	}

	// project new texture coords
	for (i=0 ; i<nump ; i++, vecs+=3)
	{
		j = vec_to_st[axis][2];
		if (j > 0)
			dv = vecs[j - 1];
		else
			dv = -vecs[-j - 1];

		j = vec_to_st[axis][0];
		if (j < 0)
			s = -vecs[-j -1] / dv;
		else
			s = vecs[j-1] / dv;
		j = vec_to_st[axis][1];
		if (j < 0)
			t = -vecs[-j -1] / dv;
		else
			t = vecs[j-1] / dv;

		if (s < skymins[0][axis])
			skymins[0][axis] = s;
		if (t < skymins[1][axis])
			skymins[1][axis] = t;
		if (s > skymaxs[0][axis])
			skymaxs[0][axis] = s;
		if (t > skymaxs[1][axis])
			skymaxs[1][axis] = t;
	}
}

/*
=================
Sky_ClipPoly
=================
*/
void Sky_ClipPoly (int nump, vec3_t vecs, int stage)
{
	float	*norm;
	float	*v;
	qboolean	front, back;
	float	d, e;
	float	dists[MAX_CLIP_VERTS];
	int		sides[MAX_CLIP_VERTS];
	vec3_t	newv[2][MAX_CLIP_VERTS];
	int		newc[2];
	int		i, j;

	if (nump > MAX_CLIP_VERTS-2)
		Sys_Error ("Sky_ClipPoly: MAX_CLIP_VERTS");
	if (stage == 6) // fully clipped
	{
		Sky_ProjectPoly (nump, vecs);
		return;
	}

	front = back = false;
	norm = skyclip[stage];
	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		d = DotProduct (v, norm);
		if (d > ON_EPSILON)
		{
			front = true;
			sides[i] = SIDE_FRONT;
		}
		else if (d < ON_EPSILON)
		{
			back = true;
			sides[i] = SIDE_BACK;
		}
		else
			sides[i] = SIDE_ON;
		dists[i] = d;
	}

	if (!front || !back)
	{	// not clipped
		Sky_ClipPoly (nump, vecs, stage+1);
		return;
	}

	// clip it
	sides[i] = sides[0];
	dists[i] = dists[0];
	VectorCopy (vecs, (vecs+(i*3)) );
	newc[0] = newc[1] = 0;

	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		switch (sides[i])
		{
		case SIDE_FRONT:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			break;
		case SIDE_BACK:
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		case SIDE_ON:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		}

		if (sides[i] == SIDE_ON || sides[i+1] == SIDE_ON || sides[i+1] == sides[i])
			continue;

		d = dists[i] / (dists[i] - dists[i+1]);
		for (j=0 ; j<3 ; j++)
		{
			e = v[j] + d*(v[j+3] - v[j]);
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}
		newc[0]++;
		newc[1]++;
	}

	// continue
	Sky_ClipPoly (newc[0], newv[0][0], stage+1);
	Sky_ClipPoly (newc[1], newv[1][0], stage+1);
}

/*
================
Sky_ProcessPoly
================
*/
void Sky_ProcessPoly (glpoly_t	*p)
{
	int			i;
	vec3_t		verts[MAX_CLIP_VERTS];

	//draw it
	DrawGLPoly(p);
	rs_brushpasses++;

	//update sky bounds
	if ((r_fastsky.value == 0) ||
		(r_fastsky.value == 2 && (skybox_name[0] || externalskyloaded)))
	{
		for (i = 0; i < p->numverts; i++)
			VectorSubtract(p->verts[i], r_origin, verts[i]);
		Sky_ClipPoly(p->numverts, verts[0], 0);
	}

}

/*
================
Sky_ProcessTextureChains -- handles sky polys in world model
================
*/
void Sky_ProcessTextureChains (void)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;

	if (!r_drawworld_cheatsafe)
		return;

	for (i=0 ; i<cl.worldmodel->numtextures ; i++)
	{
		t = cl.worldmodel->textures[i];

		if (!t || !t->texturechains[chain_world] || !(t->texturechains[chain_world]->flags & SURF_DRAWSKY))
			continue;

		for (s = t->texturechains[chain_world]; s; s = s->texturechain)
			Sky_ProcessPoly (s->polys);
	}
}

/*
================
Sky_ProcessEntities -- handles sky polys on brush models
================
*/
void Sky_ProcessEntities (void)
{
	entity_t	*e;
	msurface_t	*s;
	glpoly_t	*p;
	int			i,j,k,mark;
	float		dot;
	qboolean	rotated;
	vec3_t		temp, forward, right, up;

	if (!r_drawentities.value)
		return;

	for (i=0 ; i<cl_numvisedicts ; i++)
	{
		e = cl_visedicts[i];

		if (!e->model || e->model->needload || e->model->type != mod_brush)
			continue;

		if (e->model->submodelof == cl.worldmodel &&
			skipsubmodels &&
			skipsubmodels[e->model->submodelidx>>3]&(1u<<(e->model->submodelidx&7)))
			return;	//its in the scenecache that we're drawing. don't draw it twice (and certainly not the slow way).

		if (R_CullModelForEntity(e))
			continue;

		if (e->alpha == ENTALPHA_ZERO)
			continue;

		VectorSubtract (r_refdef.vieworg, e->origin, modelorg);
		if (e->angles[0] || e->angles[1] || e->angles[2])
		{
			rotated = true;
			AngleVectors (e->angles, forward, right, up);
			VectorCopy (modelorg, temp);
			modelorg[0] = DotProduct (temp, forward);
			modelorg[1] = -DotProduct (temp, right);
			modelorg[2] = DotProduct (temp, up);
		}
		else
			rotated = false;

		s = &e->model->surfaces[e->model->firstmodelsurface];

		for (j=0 ; j<e->model->nummodelsurfaces ; j++, s++)
		{
			if (s->flags & SURF_DRAWSKY)
			{
				dot = DotProduct (modelorg, s->plane->normal) - s->plane->dist;
				if (((s->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
					(!(s->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
				{
					//copy the polygon and translate manually, since Sky_ProcessPoly needs it to be in world space
					mark = Hunk_LowMark();
					p = (glpoly_t *) Hunk_Alloc (sizeof(*s->polys)); //FIXME: don't allocate for each poly
					p->numverts = s->polys->numverts;
					for (k=0; k<p->numverts; k++)
					{
						if (rotated)
						{
							p->verts[k][0] = e->origin[0] + s->polys->verts[k][0] * forward[0]
														  - s->polys->verts[k][1] * right[0]
														  + s->polys->verts[k][2] * up[0];
							p->verts[k][1] = e->origin[1] + s->polys->verts[k][0] * forward[1]
														  - s->polys->verts[k][1] * right[1]
														  + s->polys->verts[k][2] * up[1];
							p->verts[k][2] = e->origin[2] + s->polys->verts[k][0] * forward[2]
														  - s->polys->verts[k][1] * right[2]
														  + s->polys->verts[k][2] * up[2];
						}
						else
							VectorAdd(s->polys->verts[k], e->origin, p->verts[k]);
					}
					Sky_ProcessPoly (p);
					Hunk_FreeToLowMark (mark);
				}
			}
		}
	}
}

//==============================================================================
//
//  RENDER SKYBOX
//
//==============================================================================

/*
==============
Sky_EmitSkyBoxVertex
==============
*/
void Sky_EmitSkyBoxVertex (float s, float t, int axis)
{
	vec3_t		v, b;
	int			j, k;
	float		w, h;

	b[0] = s * gl_farclip.value / sqrt(3.0);
	b[1] = t * gl_farclip.value / sqrt(3.0);
	b[2] = gl_farclip.value / sqrt(3.0);

	for (j=0 ; j<3 ; j++)
	{
		k = st_to_vec[axis][j];
		if (k < 0)
			v[j] = -b[-k - 1];
		else
			v[j] = b[k - 1];
		v[j] += r_origin[j];
	}

	// convert from range [-1,1] to [0,1]
	s = (s+1)*0.5;
	t = (t+1)*0.5;

	// avoid bilerp seam
	w = skybox_textures[skytexorder[axis]]->width;
	h = skybox_textures[skytexorder[axis]]->height;
	s = s * (w-1)/w + 0.5/w;
	t = t * (h-1)/h + 0.5/h;

	t = 1.0 - t;
	glTexCoord2f (s, t);
	glVertex3fv (v);
}

/*
==============
Sky_DrawSkyBox

FIXME: eliminate cracks by adding an extra vert on tjuncs
==============
*/
void Sky_DrawSkyBox (void)
{
	int i;

	for (i=0 ; i<6 ; i++)
	{
		if (skymins[0][i] >= skymaxs[0][i] || skymins[1][i] >= skymaxs[1][i])
			continue;

		GL_Bind (skybox_textures[skytexorder[i]]);

#if 1 /* FIXME: this is to avoid tjunctions until i can do it the right way */
		skymins[0][i] = -1;
		skymins[1][i] = -1;
		skymaxs[0][i] = 1;
		skymaxs[1][i] = 1;
#endif
		glBegin (GL_QUADS);
		Sky_EmitSkyBoxVertex (skymins[0][i], skymins[1][i], i);
		Sky_EmitSkyBoxVertex (skymins[0][i], skymaxs[1][i], i);
		Sky_EmitSkyBoxVertex (skymaxs[0][i], skymaxs[1][i], i);
		Sky_EmitSkyBoxVertex (skymaxs[0][i], skymins[1][i], i);
		glEnd ();

		rs_skypolys++;
		rs_skypasses++;

		if (Fog_GetDensity() > 0 && skyfog > 0)
		{
			float *c;

			c = Fog_GetColor();
			glEnable (GL_BLEND);
			glDisable (GL_TEXTURE_2D);
			glColor4f (c[0],c[1],c[2], CLAMP(0.0f,skyfog,1.0f));

			glBegin (GL_QUADS);
			Sky_EmitSkyBoxVertex (skymins[0][i], skymins[1][i], i);
			Sky_EmitSkyBoxVertex (skymins[0][i], skymaxs[1][i], i);
			Sky_EmitSkyBoxVertex (skymaxs[0][i], skymaxs[1][i], i);
			Sky_EmitSkyBoxVertex (skymaxs[0][i], skymins[1][i], i);
			glEnd ();

			glColor3f (1, 1, 1);
			glEnable (GL_TEXTURE_2D);
			glDisable (GL_BLEND);

			rs_skypasses++;
		}
	}
}

//==============================================================================
//
//  RENDER CLOUDS
//
//==============================================================================

/*
==============
Sky_SetBoxVert
==============
*/
void Sky_SetBoxVert (float s, float t, int axis, vec3_t v)
{
	vec3_t		b;
	int			j, k;

	b[0] = s * gl_farclip.value / sqrt(3.0);
	b[1] = t * gl_farclip.value / sqrt(3.0);
	b[2] = gl_farclip.value / sqrt(3.0);

	for (j=0 ; j<3 ; j++)
	{
		k = st_to_vec[axis][j];
		if (k < 0)
			v[j] = -b[-k - 1];
		else
			v[j] = b[k - 1];
		v[j] += r_origin[j];
	}
}

/*
=============
Sky_GetTexCoord
=============
*/
void Sky_GetTexCoord (vec3_t v, float speed, float *s, float *t)
{
	vec3_t	dir;
	float	length, scroll;

	float clamped_skyspeed = CLAMP(0, r_skyspeed.value, 100); // woods #skyspeed

	VectorSubtract (v, r_origin, dir);
	dir[2] *= 3;	// flatten the sphere

	length = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
	length = sqrt (length);
	length = 6*63/length;

	scroll = cl.time * speed * clamped_skyspeed; // woods #skyspeed
	scroll -= (int)scroll & ~127;

	*s = (scroll + dir[0] * length) * (1.0/128);
	*t = (scroll + dir[1] * length) * (1.0/128);
}

/*
===============
Sky_DrawFaceQuad
===============
*/
void Sky_DrawFaceQuad (glpoly_t *p)
{
	float	s, t;
	float	*v;
	int		i;

	if (gl_mtexable && r_skyalpha.value >= 1.0)
	{
		GL_Bind (solidskytexture);
		GL_EnableMultitexture();
		GL_Bind (alphaskytexture);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);

		glBegin (GL_QUADS);
		for (i=0, v=p->verts[0] ; i<4 ; i++, v+=VERTEXSIZE)
		{
			Sky_GetTexCoord (v, 8, &s, &t);
			GL_MTexCoord2fFunc (GL_TEXTURE0_ARB, s, t);
			Sky_GetTexCoord (v, 16, &s, &t);
			GL_MTexCoord2fFunc (GL_TEXTURE1_ARB, s, t);
			glVertex3fv (v);
		}
		glEnd ();

		GL_DisableMultitexture();

		rs_skypolys++;
		rs_skypasses++;
	}
	else
	{
		GL_Bind (solidskytexture);

		if (r_skyalpha.value < 1.0)
			glColor3f (1, 1, 1);

		glBegin (GL_QUADS);
		for (i=0, v=p->verts[0] ; i<4 ; i++, v+=VERTEXSIZE)
		{
			Sky_GetTexCoord (v, 8, &s, &t);
			glTexCoord2f (s, t);
			glVertex3fv (v);
		}
		glEnd ();

		GL_Bind (alphaskytexture);
		glEnable (GL_BLEND);

		if (r_skyalpha.value < 1.0)
			glColor4f (1, 1, 1, r_skyalpha.value);

		glBegin (GL_QUADS);
		for (i=0, v=p->verts[0] ; i<4 ; i++, v+=VERTEXSIZE)
		{
			Sky_GetTexCoord (v, 16, &s, &t);
			glTexCoord2f (s, t);
			glVertex3fv (v);
		}
		glEnd ();

		glDisable (GL_BLEND);

		rs_skypolys++;
		rs_skypasses += 2;
	}

	if (Fog_GetDensity() > 0 && skyfog > 0)
	{
		float *c;

		c = Fog_GetColor();
		glEnable (GL_BLEND);
		glDisable (GL_TEXTURE_2D);
		glColor4f (c[0],c[1],c[2], CLAMP(0.0f,skyfog,1.0f));

		glBegin (GL_QUADS);
		for (i=0, v=p->verts[0] ; i<4 ; i++, v+=VERTEXSIZE)
			glVertex3fv (v);
		glEnd ();

		glColor3f (1, 1, 1);
		glEnable (GL_TEXTURE_2D);
		glDisable (GL_BLEND);

		rs_skypasses++;
	}
}

/*
==============
Sky_DrawFace
==============
*/

void Sky_DrawFace (int axis)
{
	glpoly_t	*p;
	vec3_t		verts[4];
	int			i, j, start;
	float		di,qi,dj,qj;
	vec3_t		up, right, temp, temp2;

	Sky_SetBoxVert(-1.0, -1.0, axis, verts[0]);
	Sky_SetBoxVert(-1.0,  1.0, axis, verts[1]);
	Sky_SetBoxVert(1.0,   1.0, axis, verts[2]);
	Sky_SetBoxVert(1.0,  -1.0, axis, verts[3]);

	start = Hunk_LowMark ();
	p = (glpoly_t *) Hunk_Alloc(sizeof(glpoly_t));

	VectorSubtract(verts[2],verts[3],up);
	VectorSubtract(verts[2],verts[1],right);

	di = q_max((int)r_sky_quality.value, 1);
	qi = 1.0 / di;
	dj = (axis < 4) ? di*2 : di; //subdivide vertically more than horizontally on skybox sides
	qj = 1.0 / dj;

	for (i=0; i<di; i++)
	{
		for (j=0; j<dj; j++)
		{
			if (i*qi < skymins[0][axis]/2+0.5 - qi || i*qi > skymaxs[0][axis]/2+0.5 ||
				j*qj < skymins[1][axis]/2+0.5 - qj || j*qj > skymaxs[1][axis]/2+0.5)
				continue;

			//if (i&1 ^ j&1) continue; //checkerboard test
			VectorScale (right, qi*i, temp);
			VectorScale (up, qj*j, temp2);
			VectorAdd(temp,temp2,temp);
			VectorAdd(verts[0],temp,p->verts[0]);

			VectorScale (up, qj, temp);
			VectorAdd (p->verts[0],temp,p->verts[1]);

			VectorScale (right, qi, temp);
			VectorAdd (p->verts[1],temp,p->verts[2]);

			VectorAdd (p->verts[0],temp,p->verts[3]);

			Sky_DrawFaceQuad (p);
		}
	}
	Hunk_FreeToLowMark (start);
}

/*
==============
Sky_DrawSkyLayers

draws the old-style scrolling cloud layers
==============
*/
void Sky_DrawSkyLayers (void)
{
	int i;

	if (r_skyalpha.value < 1.0)
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	for (i=0 ; i<6 ; i++)
		if (skymins[0][i] < skymaxs[0][i] && skymins[1][i] < skymaxs[1][i])
			Sky_DrawFace (i);

	if (r_skyalpha.value < 1.0)
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

/*
==============
Sky_DrawSky

called once per frame before drawing anything else
==============
*/
void Sky_DrawSky (void)
{
	int i;

	//in these special render modes, the sky faces are handled in the normal world/brush renderer
	if (r_drawflat_cheatsafe|| r_lightmap_cheatsafe)
		return;

	if (skyroom_drawn)
	{	//Spike: We already drew a skyroom underneath. If we draw an actual sky now then we'll have wasted all that effort.
		//however, if we fiddle with stuff, we can make sure that other surfaces don't draw over it either.

		int			i;
		msurface_t	*s;
		texture_t	*t;

		glColorMask(false,false,false,false);
		glDisable (GL_TEXTURE_2D);
		for (i=0 ; i<cl.worldmodel->numtextures ; i++)
		{
			t = cl.worldmodel->textures[i];

			if (!t || !t->texturechains[chain_world] || !(t->texturechains[chain_world]->flags & SURF_DRAWSKY))
				continue;

			for (s = t->texturechains[chain_world]; s; s = s->texturechain)
			{
				DrawGLPoly(s->polys);
				rs_brushpasses++;
				Sky_ProcessPoly (s->polys);
			}
		}
		glEnable (GL_TEXTURE_2D);
		glColorMask(true,true,true,true);
		return;
	}

	//
	// reset sky bounds
	//
	for (i=0 ; i<6 ; i++)
	{
		skymins[0][i] = skymins[1][i] = FLT_MAX;
		skymaxs[0][i] = skymaxs[1][i] = -FLT_MAX;
	}

	//
	// process world and bmodels: draw flat-shaded sky surfs, and update skybounds
	//
	Fog_DisableGFog ();
	glDisable (GL_TEXTURE_2D);
	if (Fog_GetDensity() > 0)
		glColor3fv (Fog_GetColor());
	else
		glColor3fv (skyflatcolor);
#ifndef SDL_THREADS_DISABLED
	if (((skybox_name[0] || externalskyloaded) && (r_fastsky.value == 2) && RSceneCache_DrawSkySurfDepth()) ||
		(skybox_name[0] && !r_fastsky.value && RSceneCache_DrawSkySurfDepth())) // woods -- #fastsky2
	{	//we have no surfaces to process... fill all sides. its probably still faster.
		for (i=0 ; i<6 ; i++)
		{
			skymins[0][i] = skymins[1][i] = -FLT_MAX;
			skymaxs[0][i] = skymaxs[1][i] = FLT_MAX;
		}
	}
	else
#endif
		Sky_ProcessTextureChains ();
	Sky_ProcessEntities ();
	glColor3f (1, 1, 1);
	glEnable (GL_TEXTURE_2D);

	//
	// render slow sky: cloud layers or skybox
	//
	if ((!r_fastsky.value && !(Fog_GetDensity() > 0 && skyfog >= 1)) ||
		(r_fastsky.value == 2 && (skybox_name[0] || externalskyloaded) && !(Fog_GetDensity() > 0 && skyfog >= 1))) // woods -- #fastsky2 | r_fastsky 2 gives skybox precedence over fastsky, but fallback if not skybox
	{
		glDepthFunc(GL_GEQUAL);
		glDepthMask(0);

		if (skybox_name[0])
			Sky_DrawSkyBox ();
		else
			Sky_DrawSkyLayers ();

		glDepthMask(1);
		glDepthFunc(GL_LEQUAL);
	}

	Fog_EnableGFog ();
}
