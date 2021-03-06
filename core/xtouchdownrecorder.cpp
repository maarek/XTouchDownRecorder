/*
XTouchDownRecorder

BSD 2-Clause License

Copyright (c) 2018, Wei Shuai <cpuwolf@gmail.com>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <math.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#endif

#pragma comment(lib, "wldap32.lib" )
#pragma comment(lib, "crypt32.lib" )
#pragma comment(lib, "Ws2_32.lib")
#include <curl/curl.h>

#if defined(__APPLE__) || defined(__unix__)
#ifndef BOOL
#define BOOL unsigned char
#define TRUE 1
#define FALSE 0
#endif
#endif

#include <XPLMPlugin.h>
#include <XPLMDisplay.h>
#include <XPLMGraphics.h>
#include <XPLMDataAccess.h>
#include <XPLMUtilities.h>
#include <XPLMProcessing.h>
#include <XPLMMenus.h>
#include <XPLMNavigation.h>
#include <XPWidgets.h>
#include <XPStandardWidgets.h>
#include <XPWidgetUtils.h>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <Carbon/Carbon.h>
#else
#include <stdlib.h>
#include <GL/gl.h>
#endif

#include "lightworker.h"
#include "cef3.h"
#include <jsmn.h>

#define _PROVER_ "V9e"
#define _PRONAMEVER_ "XTouchDownRecorder " _PROVER_ " (" __DATE__ ")"

static BOOL uploadfile(char * path);

#define MAX_TABLE_ELEMENTS 500
#define CURVE_LEN 2

static XPLMDataRef gearFRef,gForceRef,vertSpeedRef,pitchRef,elevatorRef,engRef,aglRef,
tmRef,gndSpeedRef,latRef,longRef,tailRef,icaoRef, totalWeightRef,fpsRef,vxRef ;


typedef struct
{
	unsigned int start;
	unsigned int end;
	unsigned int size;

	float *touchdown_vs_table;
	float *touchdown_g_table;
	float *touchdown_pch_table;
	BOOL *touchdown_air_table;
	float *touchdown_elev_table;
	float *touchdown_eng_table;
	float *touchdown_agl_table;
	float *touchdown_tm_table;
	float *touchdown_gs_table;
	float *touchdown_gf_table;
	float *touchdown_tw_table;
	float *touchdown_vx_table;

	float pbuffer[1];
}XTDData;

enum
{
	TOUCHDOWN_VS_IDX = 0,
	TOUCHDOWN_G_IDX,
	TOUCHDOWN_PCH_IDX,
	TOUCHDOWN_AIR_IDX,
	TOUCHDOWN_ELEV_IDX,
	TOUCHDOWN_ENG_IDX,
	TOUCHDOWN_AGL_IDX,
	TOUCHDOWN_TM_IDX,
	TOUCHDOWN_GS_IDX,
	TOUCHDOWN_GF_IDX,
	TOUCHDOWN_TW_IDX,
	TOUCHDOWN_VX_IDX,
	MAX_TOUCHDOWN_IDX
};

#define _XTDDATASIZE ((MAX_TABLE_ELEMENTS * sizeof(float) * MAX_TOUCHDOWN_IDX) + sizeof(XTDData))

static XTDData *datarealtm, *datacopy;


#define _BUFFER_DELETE(xtddata) xtddata->size=0;xtddata->start = 0;xtddata->end = 0;
//#define BUFFER_DELETE() _BUFFER_DELETE(datarealtm);

#define _BUFFER_EMPTY(xtddata) (xtddata->size==0)
//#define BUFFER_EMPTY() _BUFFER_EMPTY(datarealtm);

#define _BUFFER_FULL(xtddata) (xtddata->size==MAX_TABLE_ELEMENTS)
//#define BUFFER_FULL() _BUFFER_FULL(datarealtm)

/* set value firstly, then increase index */
#define _BUFFER_INSERT_BACK(xtddata) if(xtddata->end < MAX_TABLE_ELEMENTS) {\
								xtddata->end++;} if(xtddata->end==MAX_TABLE_ELEMENTS) {\
									xtddata->end = 0;\
								} \
								if(xtddata->size < MAX_TABLE_ELEMENTS) {\
									xtddata->size++;\
								} else {\
									if(xtddata->start < MAX_TABLE_ELEMENTS) {\
										xtddata->start++;} if(xtddata->start==MAX_TABLE_ELEMENTS) {\
											xtddata->start = 0;}\
								}
//#define BUFFER_INSERT_BACK() _BUFFER_INSERT_BACK(datarealtm);

#define _BUFFER_GO_START(xtddata,idx,tmp_count)  tmp_count=xtddata->size; idx=xtddata->start;
//#define BUFFER_GO_START(idx,tmp_count) _BUFFER_GO_START(datarealtm,idx,tmp_count)

#define BUFFER_GO_IS_END(idx,tmp_count)  (tmp_count<=0)

#define BUFFER_GO_NEXT(idx,tmp_count) if(idx < MAX_TABLE_ELEMENTS) {\
								idx++;} if(idx==MAX_TABLE_ELEMENTS) {\
									idx = 0;\
								} tmp_count--;\

#define BUFFER_PT_UPDATE(pd) \
	pd->touchdown_vs_table = pd->pbuffer + (TOUCHDOWN_VS_IDX * MAX_TABLE_ELEMENTS);\
	pd->touchdown_g_table = pd->pbuffer + (TOUCHDOWN_G_IDX * MAX_TABLE_ELEMENTS);\
	pd->touchdown_pch_table = pd->pbuffer + (TOUCHDOWN_PCH_IDX * MAX_TABLE_ELEMENTS);\
	pd->touchdown_air_table = (BOOL *)(pd->pbuffer + (TOUCHDOWN_AIR_IDX * MAX_TABLE_ELEMENTS));\
	pd->touchdown_elev_table = pd->pbuffer + (TOUCHDOWN_ELEV_IDX * MAX_TABLE_ELEMENTS);\
	pd->touchdown_eng_table = pd->pbuffer + (TOUCHDOWN_ENG_IDX * MAX_TABLE_ELEMENTS);\
	pd->touchdown_agl_table = pd->pbuffer + (TOUCHDOWN_AGL_IDX * MAX_TABLE_ELEMENTS);\
	pd->touchdown_tm_table = pd->pbuffer + (TOUCHDOWN_TM_IDX * MAX_TABLE_ELEMENTS);\
	pd->touchdown_gs_table = pd->pbuffer + (TOUCHDOWN_GS_IDX * MAX_TABLE_ELEMENTS);\
	pd->touchdown_gf_table = pd->pbuffer + (TOUCHDOWN_GF_IDX * MAX_TABLE_ELEMENTS);\
	pd->touchdown_tw_table = pd->pbuffer + (TOUCHDOWN_TW_IDX * MAX_TABLE_ELEMENTS);\
	pd->touchdown_vx_table = pd->pbuffer + (TOUCHDOWN_VX_IDX * MAX_TABLE_ELEMENTS);

static void toggle_touchdown();
static XTDData * XTDMalloc()
{
	XTDData * pd;
	pd = (XTDData *)malloc(_XTDDATASIZE);
	if(!pd) {
		XPLMDebugString("XTouchDownRecorder:malloc error!\n");
		return NULL;
	}
	memset(pd, 0, _XTDDATASIZE);
	BUFFER_PT_UPDATE(pd);
	return pd;
}

static void XTDCopy(XTDData * dst, XTDData * src)
{
	XTDData * pd = dst;
	memcpy(dst, src, _XTDDATASIZE);
	BUFFER_PT_UPDATE(pd);
}

static float lastVS = 1.0;
static float lastG = 1.0;
static float lastPitch = 1.0;
static BOOL lastAir = FALSE;
static float lastElev = 0.0;
static float lastEng = 0.0;
static float lastAgl = 0.0;
static float lastTm = 0.0;
static float lastGs = 0.0;

static float lastTotalKg = 0.0;
static float lastGearN = 0.0;
static float lastVx=0.0;

#define _TD_CHART_HEIGHT 200
#define _TD_CHART_WIDTH (MAX_TABLE_ELEMENTS*CURVE_LEN)

typedef struct
{
	int px;
	int py;
	BOOL agree;
}XTDConfig;

typedef struct
{
	int posx;
	int posy;
	int width;
	int height;
	BOOL in;
}XTDWinBox;
typedef struct
{
	XTDWinBox win;
	XTDWinBox close;
	XTDWinBox link;
}XTDWin;

typedef struct
{
	int xpVer;
	int xplmVer;
	XPLMHostApplicationID hostID;

	XPLMWindowID g_win;
	XPLMMenuID tdr_menu;

	XPLMCommandRef ToggleCommand;
	char g_xppath[512];
	char g_xpperfpath[512];

	BOOL collect_touchdown_data;
	unsigned int show_touchdown_counter;
	BOOL IsTouchDown;
	unsigned int ground_counter;
	unsigned int taxi_counter;
	unsigned int air_counter;
	float XPTouchDownTM;
	float XPTouchDownWeight;
	float XPTouchDownFpm;
	float XPTouchDownLoad;
	time_t touchTime;
	char local_time[50];

	char logAirportId[50];
	char logAirportName[256];
	char logAircraftTail[50];
	char logAircraftIcao[40];
	char landingString[128];

	char g_NewsString[128];
	char g_NewsLink[128];
	char g_NewsClickLink[128];

	struct lightworker* worker;
	BOOL lightworkerexit;

	XTDConfig conf;
	XTDWin * winref;
	XPWidgetID AgreeMainWidget;
	int counterafttd;

	BOOL curl_disable_ssl_verify;
	float XPTouchDownLat;
	float XPTouchDownLon;

	struct cefui *pcef;
}XTDInfo;

XTDInfo * g_info;
static int XPluginStartBH(void);

static bool CEF_ready()
{
	return ((g_info->pcef) && (g_info->pcef->isinit));
}

static BOOL check_ground(float n)
{
	if ( n != 0.0f ) {
		return TRUE;
	} else return FALSE;
}

static BOOL is_on_ground()
{
	return check_ground(XPLMGetDataf(gearFRef));
}

static BOOL is_taxing()
{
	/*push back ground speed is < 1.5ms*/
	float speed = XPLMGetDataf(gndSpeedRef);
	if((speed > 2.0f) && (speed < 10.0f)) {
		return TRUE;
	}
	return FALSE;
}

static float get_max_val(XTDData * pd, float mytable[])
{
	int k,tmpc;
	/*-- calculate max data*/
	float max_data = 0.0f;

	_BUFFER_GO_START(pd,k,tmpc);
	while(!BUFFER_GO_IS_END(k,tmpc)) {
		float el = mytable[k];
		if (fabs(el) > fabs(max_data)) {
			max_data = el;
		}
		BUFFER_GO_NEXT(k,tmpc);
	}
	return max_data;
}

void collect_flight_data()
{
	float engtb[4];
	XTDData * pd = datarealtm;
	int iw = pd->end;

	lastVS = XPLMGetDataf(vertSpeedRef);
	lastG = XPLMGetDataf(gForceRef);
	lastPitch = XPLMGetDataf(pitchRef);
	lastAir = check_ground(XPLMGetDataf(gearFRef));
	lastElev = XPLMGetDataf(elevatorRef);
	lastAgl = XPLMGetDataf(aglRef);
	lastTm = XPLMGetDataf(tmRef);
	XPLMGetDatavf(engRef,engtb,0,3);
	lastEng = engtb[0];
	lastGs = XPLMGetDataf(gndSpeedRef);
	lastTotalKg = XPLMGetDataf(totalWeightRef);
	lastGearN = XPLMGetDataf(gearFRef);
	lastVx = XPLMGetDataf(vxRef);

	/*-- fill the table */
	pd->touchdown_vs_table[iw] = lastVS;
	pd->touchdown_g_table[iw] = lastG;
	pd->touchdown_pch_table[iw] = lastPitch;
	pd->touchdown_air_table[iw] = lastAir;
	pd->touchdown_elev_table[iw] = lastElev;
	pd->touchdown_eng_table[iw] = lastEng;
	pd->touchdown_agl_table[iw] = lastAgl;
	pd->touchdown_tm_table[iw] = lastTm;
	pd->touchdown_gs_table[iw] = lastGs;

	pd->touchdown_gf_table[iw] = lastGearN;
	pd->touchdown_tw_table[iw] = lastTotalKg;
	pd->touchdown_vx_table[iw] = lastVx;

	_BUFFER_INSERT_BACK(pd);

}

static int	dummy_wheel_cb(XPLMWindowID in_window_id, int x, int y, int wheel, int clicks, void * in_refcon)
{ return 0; }

static void keycb(XPLMWindowID inWindowID, char inKey, XPLMKeyFlags inFlags,
				   char inVirtualKey, void *inRefcon, int losingFocus)
{

}
static BOOL InBox(XTDWinBox * box, int x, int y)
{
	if ((x >= box->posx) && (x <= box->posx + box->width) &&
		(y <= box->posy ) && (y >= box->posy - box->height)) {
		box->in = TRUE;
	}
	else box->in = FALSE;
	return box->in;
}

static XPLMCursorStatus cursorcb(XPLMWindowID inWindowID,int x,int y,void * inRefcon)
{
	XTDWin * ref = (XTDWin *)inRefcon;
	int left, top, right, bottom;
	XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);

	if (InBox(&(ref->link), x, y)) {
		return xplm_CursorHidden;
	}

	if(CEF_ready()){
			CEF_mousemove(g_info->pcef, x-left, top-y);
	}
	return xplm_CursorDefault;
}
static int rmousecb(XPLMWindowID inWindowID, int x, int y,
	XPLMMouseStatus inMouse, void *inRefcon)
{
	int ret = 0;
	int left, top, right, bottom;
	XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);
	XTDWinBox winbox;
	winbox.posx=left;
	winbox.posy = top;
	winbox.width=right-left;
	winbox.height=top-bottom;
	if (InBox(&winbox, x, y)) {
		toggle_touchdown();
		ret =1;
	}
	return ret;
}
static int mousecb(XPLMWindowID inWindowID, int x, int y,
				   XPLMMouseStatus inMouse, void *inRefcon)
{
	int ret = 0;
	static int lastMouseX, lastMouseY;
	XTDWin * ref = (XTDWin *)inRefcon;

	int left, top, right, bottom;
	XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);
	XTDWinBox winbox;
	winbox.posx = left;
	winbox.posy = top;
	winbox.width = right-left;
	winbox.height = top-bottom;
	switch (inMouse) {
	case xplm_MouseDown:
#ifndef XPLM300
		if (InBox(&(ref->close), x, y)) {
			g_info->show_touchdown_counter = 0;
		} 
#endif

		if(CEF_ready()){
			CEF_mouseclick(g_info->pcef, x-left, top-y,false);
		}
		ret = 1;
		
		lastMouseX = x;
		lastMouseY = y;
		break;
	case xplm_MouseUp:
		if(CEF_ready()){
			CEF_mouseclick(g_info->pcef, x-left, top-y,true);
			ret = 1;
		}

		break;
#ifndef XPLM300
	case xplm_MouseDrag:
		ref->win.posx += x - lastMouseX;
		ref->win.posy += y - lastMouseY;
		XPLMSetWindowGeometry(inWindowID, ref->win.posx, ref->win.posy,
				ref->win.posx + _TD_CHART_WIDTH, ref->win.posy - _TD_CHART_HEIGHT);
		lastMouseX = x;
		lastMouseY = y;
		/* update close button*/
		ref->close.posx = ref->win.posx + ref->win.width - ref->close.width;
		ref->close.posy = ref->win.posy;

		if(CEF_ready()){
			CEF_update();
		}
		break;
#endif
	}
	return ret;
}

static void draw_line(float r,float g, float b, float alpha, float width, int x1, int y1, int x2, int y2)
{
	glDisable(GL_TEXTURE_2D);
	glColor3f(r, g, b);
	glLineWidth(width);
	glBegin(GL_LINES);
	glVertex2i(x1, y1);
	glVertex2i(x2, y2);
	glEnd();
	glEnable(GL_TEXTURE_2D);
}

static int draw_curve(float mytable[], float cr, float cg, float cb,
	char * text_to_print,
	int x_text_start, int y_text_start, int x_orig, int y_orig,
	int x_start,int y_start,
	float max_axis, float max_data)
{
	int k,tmpc;
	float color[] = { cr, cg, cb };

	/*-- print text*/
	int x_text = x_text_start;
	int y_text = y_text_start;
	int width_text_to_print = (int)(floor(XPLMMeasureString(xplmFont_Proportional, text_to_print, (int)strlen(text_to_print))));
	XPLMDrawString(color, x_text, y_text, text_to_print, NULL, xplmFont_Proportional);
	x_text = x_text + width_text_to_print;
	/*-- draw line*/
	int x_tmp = x_start;
	int y_tmp = y_start;
	int x_max_tmp = x_tmp;
	glDisable(GL_TEXTURE_2D);
	glColor3f(cr, cg, cb);
	glLineWidth(1);
	glBegin(GL_LINE_STRIP);
	_BUFFER_GO_START(datarealtm,k,tmpc);
	float p;
	int y_height,y1,ymax=y_orig + _TD_CHART_HEIGHT;
	int draw_max_counter = 0;
	float max_axis_plus=(float)fabs(max_axis);
	while(!BUFFER_GO_IS_END(k,tmpc)) {
		p = mytable[k];
		if(max_axis_plus<0.000001f) {
			/*avoid division 0 exception*/
			y_height = 0;
		} else {
			y_height = (int)round(p / max_axis_plus * _TD_CHART_HEIGHT);
		}

		y1 = y_tmp + y_height;
		if(y1 < y_orig) {
			glVertex2i(x_tmp, y_orig);
		} else if (y1 > ymax) {
			glVertex2i(x_tmp, ymax);
		} else {
			glVertex2i(x_tmp, y_tmp + y_height);
		}
		if (p == max_data) {
			if (draw_max_counter == 0) {
				x_max_tmp = x_tmp;
			}
			draw_max_counter = draw_max_counter + 1;
		}
		x_tmp = x_tmp + 2;
		BUFFER_GO_NEXT(k,tmpc);
	}
	glEnd();
	glEnable(GL_TEXTURE_2D);
	draw_line(cr,cg,cb,1,1,x_max_tmp, y_orig, x_max_tmp, y_orig + _TD_CHART_HEIGHT);

	return x_text;
}

static int gettouchdownanddraw(XTDData * pd, int idx, float * pfpm, float pg[],int x, int y, BOOL isdraw)
{
	int k,tmpc;
	int x_tmp = x;
	int iter_times=0;
	BOOL iter_start=FALSE;
	_BUFFER_GO_START(pd,k,tmpc);
	/*get interval seconds*/
	int last_k = k;
	float zero_tm = pd->touchdown_tm_table[idx];
	float zero_agl = pd->touchdown_agl_table[idx];
	float s = (float)floor(zero_tm - pd->touchdown_tm_table[k]);
	float fpm,sum_fpm=0.0f;
	float max_g=0.f,min_g=0.f;
	int g_count = 0;
	float delta_tm_expect;
	float resfpm;
	BOOL gforce_done=FALSE;

	while(!BUFFER_GO_IS_END(k,tmpc)) {
		float delta_tm = zero_tm - pd->touchdown_tm_table[k];
		if((delta_tm - s) <= 0.0001f) {
			/*align with 1 second*/
			if(s - 1.0f < 0.0001f) {
				/*1sec before touch*/
				iter_start = TRUE;
				delta_tm_expect = delta_tm;
				last_k = k;
			}
#ifdef XTDR_DEBUG_CHART			
			/*draw second axis*/
			if (isdraw) {
				draw_line(0, 0, 0, 1, 3, x_tmp, y, x_tmp, y + _TD_CHART_HEIGHT);
			}
#endif			
			/*goto next second*/
			s-=1.0f;
		}
		/*caculate descent rate*/
		if((iter_start) && (iter_times < 2) && (delta_tm-delta_tm_expect<=0.0001f)) {
			fpm=(pd->touchdown_agl_table[k]-zero_agl)/delta_tm*196.850394f;
			sum_fpm += fpm;
			delta_tm_expect= delta_tm/1.5f;
			iter_times++;
		}
		/*caculate G force*/
		if((delta_tm <= 0.0f)&&(delta_tm > -0.28f)) {
				max_g += pd->touchdown_g_table[k];
				g_count++;
		} else if((!gforce_done)&&(delta_tm <= -0.28f)&&(delta_tm > -1.0f)) {
			if(pd->touchdown_g_table[k] > 1.0f) {
				max_g += pd->touchdown_g_table[k];
				g_count++;
			} else {gforce_done=TRUE;}
		}
		/*calulate G force*/
		float tmp_g = pd->touchdown_gf_table[k] / (9.8f*pd->touchdown_tw_table[k]);
		if (tmp_g > min_g) {
			min_g = tmp_g;
		}

		x_tmp = x_tmp + 2;
		BUFFER_GO_NEXT(k,tmpc);
	}

	if(iter_times == 0) {
		_BUFFER_GO_START(pd,k,tmpc);
		float delta_tm = zero_tm - pd->touchdown_tm_table[k];
		resfpm = (pd->touchdown_agl_table[k]-zero_agl)/delta_tm*196.850394f;
	} else {
		resfpm =  sum_fpm/iter_times;
	}
	if(!isnan(fpm)) {
		*pfpm = resfpm;
	} else {
		*pfpm=0.00001f;
	}

	pg[0] = max_g/g_count;
	pg[1] = min_g;

	return last_k;
}
static int drawtouchdownpoints(XTDData * pd, int x, int y, BOOL isdraw)
{
	int k,tmpc;
	int touchtimes = 0;
	/*-- draw touch point vertical lines*/
	int x_tmp = x;
	
	_BUFFER_GO_START(pd,k,tmpc);
	BOOL last_air_recorded = pd->touchdown_air_table[k];
	float last_air_tm = pd->touchdown_tm_table[k];
	BOOL b;
	while(!BUFFER_GO_IS_END(k,tmpc)) {
		b = pd->touchdown_air_table[k];
		if(b != last_air_recorded) {
			if(b) {
				/* skip small debounce */
				if (pd->touchdown_tm_table[k] - last_air_tm > 0.5f) {
#ifdef XTDR_DEBUG_CHART					
					if (isdraw) {
						/* draw vertical line */
						draw_line(1, 1, 1, 1, 3, x_tmp, y + (_TD_CHART_HEIGHT / 4), x_tmp, y + (_TD_CHART_HEIGHT * 3 / 4));
					}
#endif					
					touchtimes++;
				}

			} else {
				last_air_tm = pd->touchdown_tm_table[k];
			}
		}
		x_tmp = x_tmp + 2;
		last_air_recorded = b;
		BUFFER_GO_NEXT(k,tmpc);
	}
	return touchtimes;
}

static int getfirsttouchdownpointidx(XTDData * pd)
{
	int k,tmpc;
	char tmpbuf[128];
	_BUFFER_GO_START(pd,k,tmpc);
	BOOL last_air_recorded = pd->touchdown_air_table[k];
	float last_agl_recorded = pd->touchdown_agl_table[k];
	float last_air_tm = pd->touchdown_tm_table[k];
	BOOL b;
	float max_agl_recorded = get_max_val(pd, pd->touchdown_agl_table);
	float max_gs_recorded = get_max_val(pd, pd->touchdown_gs_table);
	/* touchdown Ground Speed must greater than 10kts: ignore annoying plane load touch down */
	if(max_gs_recorded < 5.3f) {
		sprintf(tmpbuf, "XTouchDownRecorder: Bad max GS %f mps\n", max_gs_recorded);
		XPLMDebugString(tmpbuf);
		return -1;
	}
	/* touchdown at least from AGL 1.0 meter to Ground: ignore annoying plane load touch down */
	if(max_agl_recorded < 0.2f) {
		sprintf(tmpbuf, "XTouchDownRecorder: Bad max AGL %f\n", max_agl_recorded);
		XPLMDebugString(tmpbuf);
		return -1;
	}
	while(!BUFFER_GO_IS_END(k,tmpc)) {
		/*if aircraft has been reloaded, there will be jump in time */
		if (fabs(pd->touchdown_tm_table[k] - last_air_tm) > 5.0f) {
			XPLMDebugString("XTouchDownRecorder: Bad time jump\n");
			return -1;
		}
		b = pd->touchdown_air_table[k];
		if(b != last_air_recorded) {
			if(b) {
				
				g_info->IsTouchDown = TRUE;
				g_info->XPTouchDownTM = pd->touchdown_tm_table[k];
				g_info->XPTouchDownWeight = pd->touchdown_tw_table[k];
				return k;
			}
		}
		last_air_recorded = b;
		last_agl_recorded = pd->touchdown_agl_table[k];
		last_air_tm = pd->touchdown_tm_table[k];
		BUFFER_GO_NEXT(k,tmpc);
	}
	return -1;
}

/*text_buf is a buffer, not a pointer*/
static BOOL analyzeTouchDown(XTDData * pd, char *text_buf, int x, int y, BOOL isdraw)
{
	int touch_idx = getfirsttouchdownpointidx(pd);

	/* print landing load data */
	float landingVS, landingG[2];

	if (touch_idx >= 0) {
		float landingPitch = pd->touchdown_pch_table[touch_idx];
		float landingGs = pd->touchdown_gs_table[touch_idx];

		memset(g_info->landingString, 0, sizeof(g_info->landingString));

		gettouchdownanddraw(pd, touch_idx, &landingVS, landingG, x, y, isdraw);
		g_info->XPTouchDownFpm = landingVS;
		g_info->XPTouchDownLoad = landingG[0];
		/*-- draw touch point vertical lines*/
		int bouncedtimes = drawtouchdownpoints(pd, x, y, isdraw);
		char *text_to_print = text_buf;
		sprintf(text_to_print, "%.01fFpm %.02fDegree %.01fKnots", landingVS,
			landingPitch, landingGs*1.943844f);
		/*update content for file output*/
		strcat(g_info->landingString, text_to_print);
		return TRUE;
	}
	return FALSE;
}

static void ChangingColor(float ccolor[])
{
	static unsigned int bcolor = 0;
	bcolor+=12;
	if (bcolor > 0xFFFFFF) { bcolor=0; }
	ccolor[0] = (float)(bcolor & 0xFF)/255.0f;
	ccolor[1] = (float)((bcolor>>8) & 0xFF)/255.0f;
	ccolor[2] = (float)((bcolor>>16) & 0xFF)/255.0f;
}

static void drawcb(XPLMWindowID inWindowID, void *inRefcon)
{
	XTDData * pd = datarealtm;
	/*-- draw background first*/
	int x, y;
	int left, top, right, bottom;
	float color[] = { 1.0, 1.0, 1.0 };
	char text_buf[256];
	XTDWin * ref = (XTDWin *)inRefcon;

	// Mandatory: We *must* set the OpenGL state before drawing
	// (we can't make any assumptions about it)
	XPLMSetGraphicsState(
						 0 /* no fog */,
						 0 /* 0 texture units */,
						 0 /* no lighting */,
						 0 /* no alpha testing */,
						 1 /* do alpha blend */,
						 1 /* do depth testing */,
						 0 /* no depth writing */
						 );

	XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);
	ref->win.posx = left;
	ref->win.posy = top;

#ifndef XPLM300	
	XPLMDrawTranslucentDarkBox(left, top, right, bottom);
#endif	

	x = left;
	y = bottom;
#ifdef XTDR_DEBUG_CHART	
	/*-- draw center line*/
	draw_line(0, 0, 0, 1, 3, x, y + (_TD_CHART_HEIGHT / 2), x + (MAX_TABLE_ELEMENTS * 2), y + (_TD_CHART_HEIGHT / 2));
#endif

	int x_text = x + 5;
	int y_text = y + 4;
#if 0
	/* print landing load data */
	float landingVS, landingG[2];

	int touch_idx = getfirsttouchdownpointidx(pd);
	if (touch_idx >= 0) {
		float landingPitch = pd->touchdown_pch_table[touch_idx];
		float landingGs = pd->touchdown_gs_table[touch_idx];

		memset(g_info->landingString, 0, sizeof(g_info->landingString));

		gettouchdownanddraw(pd, touch_idx, &landingVS, landingG, x, y, TRUE);
		g_info->XPTouchDownFpm = landingVS;
		g_info->XPTouchDownLoad = landingG[0];//(landingG[0] > landingG[1]? landingG[0]: landingG[1]);

		/*-- draw touch point vertical lines*/
		int bouncedtimes = drawtouchdownpoints(pd, x, y, TRUE);

		char *text_to_print = text_buf;
		sprintf(text_to_print, "%.01fFpm %.02fG %.02fDegree %.01fKnots %s", landingVS, landingG[0],
			landingPitch, landingGs*1.943844f, (bouncedtimes > 1 ? "Bounced" : ""));
		/*update content for file output*/
		strcat(g_info->landingString, text_to_print);

		int width_text_to_print = (int)floor(XPLMMeasureString(xplmFont_Proportional, text_to_print, (int)strlen(text_to_print)));
#ifdef XTDR_DEBUG_CHART		
		XPLMDrawString(color, x_text, y_text, text_to_print, NULL, xplmFont_Proportional);
#endif		
		x_text = x_text + width_text_to_print;
	}
#endif
	sprintf(text_buf, "offline");		
	XPLMDrawString(color, x_text, y_text, text_buf, NULL, xplmFont_Proportional);

#ifdef XTDR_DEBUG_CHART
	/*start a new line*/
	x_text = x + 5;
	y_text = y + 16;
	/*-- now draw the chart line green
	float max_vs_recorded = get_max_val(touchdown_vs_table);
	sprintf(text_buf, "Max %.02ffpm ", max_vs_recorded);
	x_text = draw_curve(touchdown_vs_table, 0.0f,1.0f,0.0f, text_buf, x_text, y_text, x, y, x, y + (_TD_CHART_HEIGHT / 2), max_vs_recorded*2.0f, max_vs_recorded);
	*/

	/*-- now draw the chart line red
	float max_g_axis = 1.8f;
	float max_g_recorded = get_max_val(touchdown_g_table);
	sprintf(text_buf, "Max %.02fG ", max_g_recorded);
	x_text = draw_curve(touchdown_g_table, 1,0.68f,0.78f, text_buf, x_text, y_text, x, y, x, y, max_g_axis, max_g_recorded);
	*/

	/*-- now draw the chart line light blue*/
	float max_pch_recorded = get_max_val(pd, pd->touchdown_pch_table);
	sprintf(text_buf, "Max pitch %.02fDegree ", max_pch_recorded);
	x_text = draw_curve(pd->touchdown_pch_table, 0.6f, 0.85f, 0.87f, text_buf, x_text, y_text, x, y, x, y + (_TD_CHART_HEIGHT / 2), max_pch_recorded*2.0f, max_pch_recorded);

	/*-- now draw the chart line orange
	float max_elev_axis = 2.0;
	float max_elev_recorded = get_max_val(touchdown_elev_table);
	sprintf(text_buf, "Max elevator %.02f%% ", max_elev_recorded*100.0f);
	x_text = draw_curve(touchdown_elev_table, 1.0f,0.49f,0.15f, text_buf, x_text, y_text, x, y, x, y + (_TD_CHART_HEIGHT / 2), max_elev_recorded, max_elev_recorded);
	*/
	/*-- now draw the chart line yellow*/
	float max_eng_recorded = get_max_val(pd, pd->touchdown_eng_table);
	sprintf(text_buf, "Max eng %.02f%% ", max_eng_recorded*100.0f);
	x_text = draw_curve(pd->touchdown_eng_table, 1.0f, 1.0f, 0.0f, text_buf, x_text, y_text, x, y, x, y + (_TD_CHART_HEIGHT / 2), max_eng_recorded*2.0f, max_eng_recorded);

	/*-- now draw the chart line red*/
	float max_agl_recorded = get_max_val(pd, pd->touchdown_agl_table);
	sprintf(text_buf, "Max AGL %.02fM ", max_agl_recorded);
	x_text = draw_curve(pd->touchdown_agl_table, 1.0f, 0.1f, 0.1f, text_buf, x_text, y_text, x, y, x, y + (_TD_CHART_HEIGHT / 2), max_agl_recorded*2.0f, max_agl_recorded);

	/*-- now draw the chart line blue*/
	float max_gs_recorded = get_max_val(pd, pd->touchdown_gs_table);
	sprintf(text_buf, "Max %.02fknots ", max_gs_recorded*1.943844f);
	x_text = draw_curve(pd->touchdown_gs_table, 0.24f, 0.35f, 0.8f, text_buf, x_text, y_text, x, y, x, y, max_gs_recorded, max_gs_recorded);
#endif

#ifdef XTDR_DEBUG_CHART	
	/*-- title*/
	color[0] = 1.0;
	color[1] = 1.0;
	color[2] = 1.0;
	strcpy(text_buf, _PRONAMEVER_" by cpuwolf ");
	x_text = x + 50;
	y_text = y + _TD_CHART_HEIGHT - 15;
	
	XPLMDrawString(color, x_text, y_text, text_buf, NULL, xplmFont_Proportional);

	int width_text_to_print = (int)floor(XPLMMeasureString(xplmFont_Proportional, text_buf, (int)strlen(text_buf)));

	x_text = x_text + width_text_to_print;

	/* draw link*/
	ref->link.posx = x_text;
	ref->link.posy = y_text + 15;
	strcpy(text_buf, g_info->g_NewsString);
	float * colr=color;
	if (ref->link.in) {
		color[0] = 0.0;
	} else {
		float ccolor[3];
		ChangingColor(ccolor);
		colr = ccolor;

	}
	
	ref->link.width = (int)floor(XPLMMeasureString(xplmFont_Proportional, text_buf, (int)strlen(text_buf)));
	ref->link.height = 15;

	XPLMDrawString(colr, x_text, y_text, text_buf, NULL, xplmFont_Proportional);
#endif
	if(CEF_ready() && (g_info->pcef->ceftxt)){
		int screen_x, screen_y;
		int width_= right-left;
		int height_=top-bottom;
		#define MARGIN_SIZE 0
		XPLMGetScreenSize(&screen_x, &screen_y);
		XPLMSetGraphicsState(1, 1, 0, 1, 1, 1, 1);

		glBindTexture(GL_TEXTURE_2D, *(g_info->pcef->ceftxt));

		glBegin(GL_QUADS);
		glTexCoord2f(0.0, 1.0);
		glVertex2f(left, bottom);
		glTexCoord2f(0.0, 0.0);
		glVertex2f(left, top);
		glTexCoord2f(1.0, 0.0);
		glVertex2f(right,top);
		glTexCoord2f(1.0, 1.0);
		glVertex2f(right, bottom);
		glEnd();

		glBindTexture(GL_TEXTURE_2D, 0);

	}
	CEF_update();
#ifndef XPLM300	
	/*-- draw close button on top-right*/
	glDisable(GL_TEXTURE_2D);
	glColor3f(1.0, 1.0, 1.0);
	glBegin(GL_LINES);
	glVertex2i(right - 1, top - 1);
	glVertex2i(right - 10, top - 10);
	glVertex2i(right - 10, top - 1);
	glVertex2i(right - 1, top - 10);
	glEnd();
	glEnable(GL_TEXTURE_2D);
#endif

}



static float flightcb(float inElapsedSinceLastCall,
	float inElapsedTimeSinceLastFlightLoop, int inCounter,
	void *inRefcon)
{
	float ret = -1.0f;

	if(CEF_ready()){
		CEF_update();
	}

	if(g_info->collect_touchdown_data) {
		collect_flight_data();
	} else ret = 0.3f;
	/*-- dont draw when the function isn't wanted*/
	if(g_info->show_touchdown_counter <= 0) {
		XPLMSetWindowIsVisible(g_info->g_win, 0);
	} else {
		XPLMSetWindowIsVisible(g_info->g_win, 1);
	}

	return ret;
}
static int create_json_ff(FILE *ofile, const char * label, float d)
{
	return fprintf(ofile, "\"%s\": %f,\n", label, d);
}
static int create_json_f(FILE *ofile, const char * label, float d)
{
	return fprintf(ofile, "\"%s\": %.02f,\n", label, d);
}
static int create_json_int(FILE *ofile, const char * label, int d)
{
	return fprintf(ofile, "\"%s\": %d,\n", label, d);
}
static int create_json_str(FILE *ofile, const char * label, const char * str)
{
	fprintf(ofile, "\"%s\": \"", label);
	fwrite(str, strlen(str), 1, ofile);
	return fprintf(ofile, "\",\n", label);
}
/*function template*/
#define CREATE_JSON_ARRAY(ofile,_label,name,mytable,fmt,base) \
	int ret; \
	int k, tmpc; \
	fprintf(ofile, "{\"label\": \"%s\",\n\"name\": \"%s\",\n\"data\": [", _label, name); \
	_BUFFER_GO_START(datacopy,k, tmpc); \
	while (!BUFFER_GO_IS_END(k, tmpc)) { \
		ret = fprintf(ofile, fmt, mytable[k]-base); \
		if (ret <= 0) return ret; \
		BUFFER_GO_NEXT(k, tmpc); \
		if(!BUFFER_GO_IS_END(k, tmpc)) {fprintf(ofile, ",");} \
	} \
	ret = fprintf(ofile, "]}"); \
	return ret;

static void create_json_array_join(FILE *ofile)
{
	fprintf(ofile, ",\n");
}
static int create_json_arrayf(FILE *ofile, const char * label, const char * name, float mytable[])
{
	CREATE_JSON_ARRAY(ofile, label, name, mytable, "%.02f", 0.f);
}
static int create_json_arrayfb(FILE *ofile, const char * label, const char * name, float mytable[],float base)
{
	CREATE_JSON_ARRAY(ofile, label, name, mytable, "%.02f", base);
}
static int create_json_arrayd(FILE *ofile, const char * label, const char * name, BOOL mytable[])
{
	CREATE_JSON_ARRAY(ofile, label, name, mytable, "%d", 0);
}
static void create_json_file(char * path, struct tm *tblock)
{
	XTDData * pd = datacopy;
	FILE *ofile;
	char tmbuf[50];
	char pathbuf[512];
	int i;
	ofile = fopen(path, "a");
	if (ofile) {
		fprintf(ofile, "{\n");

		/*write header*/
		create_json_int(ofile, "xtd_xp_ver", g_info->xpVer);
		create_json_str(ofile, "xtd_ver", _PROVER_);
		strncpy(pathbuf, g_info->g_xppath, sizeof(pathbuf)-1);
		for(i=0; i < strlen(pathbuf);i++){
			if (pathbuf[i] == '\\') { pathbuf[i] = '/'; }
		}
		create_json_str(ofile, "xtd_xp_path", pathbuf);
		#if defined(__APPLE__)
		create_json_str(ofile, "xtd_os", "mac");
		#elif defined(__unix__)
		create_json_str(ofile, "xtd_os", "lin");
		#else
		create_json_str(ofile, "xtd_os", "win");
		#endif
		create_json_str(ofile, "xtd_acf_icao", g_info->logAircraftIcao);
		create_json_str(ofile, "xtd_acf_tail", g_info->logAircraftTail);
		create_json_str(ofile, "xtd_apt_icao", g_info->logAirportId);
		create_json_str(ofile, "xtd_apt_name", g_info->logAirportName);
		create_json_int(ofile, "xtd_touch_tw", (int)floor(g_info->XPTouchDownWeight));
		create_json_f(ofile, "xtd_touch_vs", g_info->XPTouchDownFpm);
		create_json_f(ofile, "xtd_touch_g", g_info->XPTouchDownLoad);
		strftime(tmbuf, sizeof(tmbuf), "%F %X", tblock);
		create_json_str(ofile, "xtd_touch_tm", tmbuf);
		strftime(tmbuf, sizeof(tmbuf), "%z", tblock);
		create_json_str(ofile, "xtd_tmzone", tmbuf);
		create_json_ff(ofile, "xtd_touch_lat", g_info->XPTouchDownLat);
		create_json_ff(ofile, "xtd_touch_lon", g_info->XPTouchDownLon);
		/*write main data array */
		fprintf(ofile, "\"main\": [\n");
		create_json_arrayfb(ofile, "time(s)", "touchdown_tm_table", pd->touchdown_tm_table, g_info->XPTouchDownTM);
		create_json_array_join(ofile);
		create_json_arrayd(ofile, "is ground", "touchdown_air_table", pd->touchdown_air_table);
		create_json_array_join(ofile);
		create_json_arrayf(ofile, "feet per min", "touchdown_vs_table", pd->touchdown_vs_table);
		create_json_array_join(ofile);
		create_json_arrayf(ofile, "G force(G)", "touchdown_g_table", pd->touchdown_g_table);
		create_json_array_join(ofile);
		create_json_arrayf(ofile, "pitch(degree)", "touchdown_pch_table", pd->touchdown_pch_table);
		create_json_array_join(ofile);
		create_json_arrayf(ofile, "elevator(%)", "touchdown_elev_table", pd->touchdown_elev_table);
		create_json_array_join(ofile);
		create_json_arrayf(ofile, "engine(%)", "touchdown_eng_table", pd->touchdown_eng_table);
		create_json_array_join(ofile);
		create_json_arrayf(ofile, "AGL(meter)", "touchdown_agl_table", pd->touchdown_agl_table);
		create_json_array_join(ofile);
		create_json_arrayf(ofile, "ground speed(meter/s)", "touchdown_gs_table", pd->touchdown_gs_table);
		create_json_array_join(ofile);
		create_json_arrayf(ofile, "total weight(Kg)", "touchdown_tw_table", pd->touchdown_tw_table);
		create_json_array_join(ofile);
		create_json_arrayf(ofile, "gear force(N)", "touchdown_gf_table", pd->touchdown_gf_table);
		create_json_array_join(ofile);
		create_json_arrayf(ofile, "side speed(meter/s)", "touchdown_vx_table", pd->touchdown_vx_table);
		fprintf(ofile, "]\n");
		/*write end*/
		fprintf(ofile, "}");
		fclose(ofile);
	}
	else {
		XPLMDebugString("XTouchDownRecorder: data json exporting error\n");
	}
}

static int write_csv_file_bool(FILE *ofile,BOOL mytable[], const char * title)
{
	int ret;
	int k,tmpc;
	fprintf(ofile, "%s,", title);
	_BUFFER_GO_START(datacopy,k,tmpc);
	while(!BUFFER_GO_IS_END(k,tmpc)) {
		ret=fprintf(ofile, "%d,", mytable[k]);
		if(ret <= 0) return ret;
		BUFFER_GO_NEXT(k,tmpc);
	}
	ret=fprintf(ofile, "\n");
	return ret;
}
static int write_csv_file(FILE *ofile,float mytable[], const char * title)
{
	int ret;
	int k,tmpc;
	fprintf(ofile, "%s,", title);
	_BUFFER_GO_START(datacopy,k,tmpc);
	while(!BUFFER_GO_IS_END(k,tmpc)) {
		ret=fprintf(ofile, "%.02f,", mytable[k]);
		if(ret <= 0) return ret;
		BUFFER_GO_NEXT(k,tmpc);
	}
	ret=fprintf(ofile, "\n");
	return ret;
}
static void create_csv_file(char * path)
{
	XTDData * pd = datacopy;
	FILE *ofile;
	//static char tmbuf[100];

	ofile = fopen(path, "a");
	if (ofile) {
		/*write main data*/
		write_csv_file(ofile, pd->touchdown_tm_table, "\"time(s)\"");
		write_csv_file_bool(ofile, pd->touchdown_air_table, "\"is air\"");
		write_csv_file(ofile, pd->touchdown_vs_table, "\"(fpm)\"");
		write_csv_file(ofile, pd->touchdown_g_table, "\"G force(G)\"");
		write_csv_file(ofile, pd->touchdown_pch_table, "\"pitch(degree)\"");
		write_csv_file(ofile, pd->touchdown_elev_table, "\"elevator(%)\"");
		write_csv_file(ofile, pd->touchdown_eng_table, "\"engine(%)\"");
		write_csv_file(ofile, pd->touchdown_agl_table, "\"AGL(meter)\"");
		write_csv_file(ofile, pd->touchdown_gs_table, "\"ground speed(meter/s)\"");
		write_csv_file(ofile, pd->touchdown_tw_table, "\"total weight(Kg)\"");
		write_csv_file(ofile, pd->touchdown_gf_table, "\"gear force(N)\"");
		write_csv_file(ofile, pd->touchdown_vx_table, "\"side speed(meter/s)\"");
		fclose(ofile);
	}
	else {
		XPLMDebugString("XTouchDownRecorder: data exporting error\n");
	}
}
static int movefile(char * srcfile, char * dstfile)
{
	size_t len = 0;
	char buffer[512];
	FILE* in = fopen(srcfile, "rb");
	if (in == NULL)
	{
		XPLMDebugString("XTouchDownRecorder: movefile skip\n");
		in = NULL;
	} else {
		FILE* out = fopen(dstfile, "a+b");
		if (out) {
			while ((len = fread(buffer, 512, 1, in)) > 0)
			{
				fwrite(buffer, 512, 1, out);
			}
			fclose(out);
		}
		fclose(in);

		if (remove(srcfile))
		{
			XPLMDebugString("XTouchDownRecorder: movefile done\n");
		} else {
			XPLMDebugString("XTouchDownRecorder: movefile:delete error\n");
		}
	}
	return 0;
}

static BOOL tryuploadfile(char * path)
{
	int trytimes=5;
	do {
		/*upload file*/
		if (uploadfile(path)) {
			remove(path);
			return TRUE;
		}
		lightworker_sleep(2000);
		trytimes--;
	} while(trytimes > 0);
	return FALSE;
}
static void enumfolder_async(void);
static void enumfolder()
{
	char tmpbuf[256];
	char fullpath[256];
	char * path = g_info->g_xppath;
	int txtlen;
#if defined(_WIN32)
	HANDLE hFind;
	WIN32_FIND_DATA FindFileData;

	sprintf(tmpbuf, "%sXTD*.json", path);
	if((hFind = FindFirstFile(tmpbuf, &FindFileData)) != INVALID_HANDLE_VALUE) {
	    do{
	        sprintf(fullpath, "XTouchDownRecorder: enumfolder %s%s\n", path, FindFileData.cFileName);
			XPLMDebugString(fullpath);
			sprintf(fullpath, "%s%s", path, FindFileData.cFileName);
			if(tryuploadfile(fullpath)) {
				txtlen=strlen(fullpath);
				fullpath[txtlen-5]=0;
				strcat(fullpath, ".csv");
				remove(fullpath);
				sprintf(fullpath, "XTouchDownRecorder: found %s\n", fullpath);
				XPLMDebugString(fullpath);
			}
			enumfolder_async();
			FindClose(hFind);
			return;
	    } while(FindNextFile(hFind, &FindFileData));
	    FindClose(hFind);
	}
#else
	DIR *dp;
	struct dirent *ep;     
	dp = opendir(path);
	char * ename;

	if (dp != NULL)
	{
		while (ep = readdir(dp)){
			ename = ep->d_name;
			sprintf(tmpbuf, "XTouchDownRecorder: enumfolder %s\n", ep->d_name);
			XPLMDebugString(tmpbuf);
			txtlen=strlen(ename);
			if(strncmp(ename,"XTD-",4)==0) {
				ename = ep->d_name+txtlen-5;
				if(strncmp(ename,".json",5)==0) {
					sprintf(fullpath, "XTouchDownRecorder: enumfolder %s%s\n", path, ep->d_name);
					XPLMDebugString(fullpath);
					sprintf(fullpath, "%s%s", path, ep->d_name);
					if(tryuploadfile(fullpath)) {
						txtlen=strlen(fullpath);
						fullpath[txtlen-5]=0;
						strcat(fullpath, ".csv");
						remove(fullpath);
						sprintf(fullpath, "XTouchDownRecorder: found %s\n", fullpath);
						XPLMDebugString(fullpath);
					}
					enumfolder_async();
					break;
				}
			}
		}

		closedir (dp);
	}
	else
		XPLMDebugString("XTouchDownRecorder: cannot find path\n");
#endif
}

static void append_log_file()
{
	char path[512];
	FILE *ofile;
	sprintf(path, "%sXTouchDownRecorderLog.txt", g_info->g_xppath);
	ofile = fopen(path, "a");
	if (ofile) {
		fprintf(ofile, "%s [%s][%s] %s %s %s\n", g_info->local_time, g_info->logAircraftIcao,
			g_info->logAircraftTail, g_info->logAirportId, g_info->logAirportName,
			g_info->g_NewsClickLink);
		fclose(ofile);
	}
	else {
		XPLMDebugString("XTouchDownRecorder: XTouchDownRecorderLog.txt open error\n");
	}
}

static void trimtail(char * str)
{
	char *end;
	// Trim trailing space
	end = str + strlen(str) - 1;
	while(end > str && isspace((unsigned char)*end)) end--;
	// Write new null terminator character
	end[1] = '\0';
}

/* work thread context, so you are free */
static void write_log_file()
{
	struct tm *loc_time_tm,*gm_time_tm;
	struct tm time_tm,gtime_tm;
	int num;
	char tmbuf[100], path[512];
	loc_time_tm = localtime(&g_info->touchTime);
	memcpy(&time_tm, loc_time_tm, sizeof(time_tm));
	loc_time_tm = &time_tm;
	gm_time_tm = gmtime(&g_info->touchTime);
	memcpy(&gtime_tm, gm_time_tm, sizeof(time_tm));
	gm_time_tm = &gtime_tm;

	float lat = XPLMGetDataf(latRef);
	float lon = XPLMGetDataf(longRef);
	g_info->XPTouchDownLat = lat;
	g_info->XPTouchDownLon = lon;
	XPLMNavRef navref = XPLMFindNavAid(NULL, NULL, &lat, &lon, NULL, xplm_Nav_Airport);

	if (XPLM_NAV_NOT_FOUND != navref)
		XPLMGetNavAidInfo(navref, NULL, &lat, &lon, NULL, NULL, NULL, g_info->logAirportId,
			g_info->logAirportName, NULL);
	else {
		g_info->logAirportId[0] = 0;
		g_info->logAirportName[0] = 0;
	}

	num = XPLMGetDatab(tailRef, g_info->logAircraftTail, 0, 49);
	g_info->logAircraftTail[num] = 0;
	trimtail(g_info->logAircraftTail);

	num = XPLMGetDatab(icaoRef, g_info->logAircraftIcao, 0, 39);
	g_info->logAircraftIcao[num] = 0;
	trimtail(g_info->logAircraftIcao);

	sprintf(path, "%sXTouchDownRecorderLog.txt", g_info->g_xppath);
	/*back compatible */
	movefile("XTouchDownRecorderLog.txt", path);
	strftime(tmbuf, sizeof(tmbuf), "%c", loc_time_tm);
	strncpy(g_info->local_time, tmbuf, sizeof(g_info->local_time));

#if 0
	/*reuse tmbuf, generating file name*/
	strftime(tmbuf, sizeof(tmbuf), "XTD-%F-%H%M%S.csv", loc_time_tm);
	sprintf(path, "%s%s", g_info->g_xppath, tmbuf);
	create_csv_file(path);
#endif
	XPLMDebugString("XTouchDownRecorder: writing json\n");
	/*reuse tmbuf, generating file name*/
	strftime(tmbuf, sizeof(tmbuf), "XTD-%F-%H%M%S.json", loc_time_tm);
	sprintf(path, "%s%s", g_info->g_xppath, tmbuf);
	//XPLMDebugString(tmbuf);
	create_json_file(path, gm_time_tm);

	tryuploadfile(path);
	XPLMDebugString("XTouchDownRecorder: up done\n");
}

static void write_config_file()
{
	char path[512];
	sprintf(path, "%sXTouchDownRecorder.cfg", g_info->g_xpperfpath);
	FILE* out = fopen(path, "wb");
	if (out) {
		fwrite(&g_info->conf, sizeof(XTDConfig), 1, out);
		fclose(out);
	}
}

static BOOL read_config_file()
{
	char path[512];
	sprintf(path, "%sXTouchDownRecorder.cfg", g_info->g_xpperfpath);
	FILE* in = fopen(path, "rb");
	if (in) {
		fread(&g_info->conf, sizeof(XTDConfig), 1, in);
		fclose(in);
		return TRUE;
	}
	return FALSE;
}
static void write_log_file_async()
{
	lightworker_queue_put_single(g_info->worker,1105, NULL, NULL);
}
static void enumfolder_async()
{
	lightworker_queue_put_single(g_info->worker,1051, NULL, NULL);
}

static BOOL getnetinfodone()
{
	return (strlen(g_info->g_NewsString) > 1)?TRUE:FALSE;
}
static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
	if (tok->type == JSMN_STRING && (int) strlen(s) == tok->end - tok->start &&
			strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
		return 0;
	}
	return -1;
}

static void strnsub(char *buffer, char *str, int chars)
{
    int n;

    if (buffer)
    {
        for (n = 0; ((n < chars) && (str[n] != 0)) ; n++) buffer[n] = str[n];
        buffer[n] = 0;
    }
}
static size_t httpcb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
#if 1
	size_t len = size*nmemb;
	char tmpbuf[128];
	char value[256];
	jsmn_parser p;
	jsmntok_t t[10];
	jsmn_init(&p);
	int i,r = jsmn_parse(&p, ptr, len, t, sizeof(t)/sizeof(t[0]));
	if (r < 0) {
		sprintf(tmpbuf, "XTouchDownRecorder: httpcb json error %d\n", r);
    	XPLMDebugString(tmpbuf);
		return 1;
	}
	/* Assume the top-level element is an object */
	if (r < 1 || t[0].type != JSMN_OBJECT) {
		XPLMDebugString("XTouchDownRecorder: Object expected\n");
		return 1;
	}
	/* Loop over all keys of the root object */
	for (i = 1; i < r; i++) {
		if (jsoneq(ptr, &t[i], "showurl") == 0) {
			/* We may use strndup() to fetch string value */
			strnsub(value, ptr + t[i+1].start, t[i+1].end-t[i+1].start);
			strcpy(g_info->g_NewsLink, value);
			sprintf(tmpbuf, "XTouchDownRecorder: - showurl: %s\n", value);
			if(CEF_ready()) {
				CEF_url(g_info->pcef, g_info->g_NewsLink);
			}
			i++;
		} else if (jsoneq(ptr, &t[i], "clickurl") == 0) {
			strnsub(value, ptr + t[i+1].start, t[i+1].end-t[i+1].start);
			strcpy(g_info->g_NewsClickLink, value);
			sprintf(tmpbuf, "XTouchDownRecorder: - clickurl: %s\n", value);
			i++;
		} else if (jsoneq(ptr, &t[i], "msg") == 0) {
			strnsub(value, ptr + t[i+1].start, t[i+1].end-t[i+1].start);
			strcpy(g_info->g_NewsString, value);
			sprintf(tmpbuf, "XTouchDownRecorder: - msg: %s\n", value);
			i++;
		} else {
			strnsub(value, ptr + t[i].start, t[i].end-t[i].start);
			sprintf(tmpbuf, "XTouchDownRecorder: Unexpected key: %s\n", value);
			i++;
		}
		XPLMDebugString(tmpbuf);
	}
	
#else
    size_t len = size*nmemb;
	char *p_n;
    if(len < sizeof(g_info->g_NewsString)) {
		memcpy(g_info->g_NewsString, ptr, len);
    } else {
		memcpy(g_info->g_NewsString, ptr, sizeof(g_info->g_NewsString) - 2);
    }
	/*new string terminal*/
    p_n = strchr(g_info->g_NewsString, '\n');
	if (p_n) {
		if (p_n < g_info->g_NewsString + len) {
			*p_n = 0;
		}
	}
    p_n = strchr(g_info->g_NewsString, '|');
	if (p_n) {
		if (p_n + 1 < g_info->g_NewsString + len) {
			*p_n = 0;
			strcpy(g_info->g_NewsLink, p_n + 1);
			if(CEF_ready()){
				XPLMDebugString(g_info->g_NewsLink);
				CEF_url(g_info->pcef, g_info->g_NewsLink);
			}

		}
	}
#endif
    return size*nmemb;
}

static void getnetinfo()
{
	CURL *curl;
 	CURLcode res;
	char tmpbuf[90];
 
 	curl_global_init(CURL_GLOBAL_DEFAULT);
 
 	curl = curl_easy_init();
 	if(curl) {
		curl_easy_setopt(curl, CURLOPT_URL, "https://raw.githubusercontent.com/cpuwolf/XTouchDownRecorder/master/news9.txt");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, httpcb);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "Dark Secret XTDR9");
		if(g_info->curl_disable_ssl_verify) {
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
		}
    	res = curl_easy_perform(curl);
    	if(res != CURLE_OK)
  			if(res == CURLE_PEER_FAILED_VERIFICATION ) {
  				g_info->curl_disable_ssl_verify = TRUE;
  			}
			sprintf(tmpbuf, "XTouchDownRecorder: getnetinfo error %d\n", res);
    		XPLMDebugString(tmpbuf);
 
		curl_easy_cleanup(curl);
	}
 
	curl_global_cleanup();
}



static BOOL uploadfile(char * path)
{
	CURL *curl;
	CURLcode res;
	double speed_upload, total_time;
	int ret = FALSE;
	char tmpbuf[90];

	curl_mime *form = NULL;
	curl_mimepart *field = NULL;

	long http_code = 0;

	curl_global_init(CURL_GLOBAL_DEFAULT);

	curl = curl_easy_init();
	if (curl) {
		form = curl_mime_init(curl);

		field = curl_mime_addpart(form);
		curl_mime_name(field, "xtdfile");
		curl_mime_filedata(field, path);

		field = curl_mime_addpart(form);
		curl_mime_name(field, "submit");
		curl_mime_data(field, "send", CURL_ZERO_TERMINATED);

		curl_easy_setopt(curl, CURLOPT_URL, "https://x-plane.vip/xtdr/upload");
		curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
		curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, httpcb);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "Dark Secret XTDR9");
		if(g_info->curl_disable_ssl_verify) {
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
		}
		res = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		sprintf(tmpbuf, "XTouchDownRecorder: status code %d\n", http_code);
		XPLMDebugString(tmpbuf);
		if (res != CURLE_OK) {
			if(res == CURLE_PEER_FAILED_VERIFICATION ) {
  				g_info->curl_disable_ssl_verify = TRUE;
  			}
			sprintf(tmpbuf, "XTouchDownRecorder: upload error %d\n", res);
    			XPLMDebugString(tmpbuf);
			if(http_code == 400) {
				ret = TRUE;
			}
		} else {
			curl_easy_getinfo(curl, CURLINFO_SPEED_UPLOAD, &speed_upload);
			curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_time);
			sprintf(tmpbuf, "XTouchDownRecorder: Upload speed %.0f bytes/sec\n", speed_upload);
			XPLMDebugString(tmpbuf);
			sprintf(tmpbuf, "XTouchDownRecorder: Upload time %.0f sec\n", total_time);
			XPLMDebugString(tmpbuf);
			append_log_file();
			ret = TRUE;
		}

		curl_easy_cleanup(curl);
		curl_mime_free(form);
	}

	curl_global_cleanup();
	return ret;
}


static float secondcb(float inElapsedSinceLastCall,
	float inElapsedTimeSinceLastFlightLoop, int inCounter,
	void *inRefcon)
{
	char tmpbuf[250];
	float fps;
	int counter;

	if (is_on_ground()) {
		if(is_taxing()) {
			g_info->taxi_counter++;
			if (g_info->taxi_counter == 6) {
				if(_BUFFER_FULL(datarealtm)&&(g_info->IsTouchDown)) {
					g_info->show_touchdown_counter = 20;
				}
			}
		}
		g_info->ground_counter = g_info->ground_counter + 1;
		
		if (g_info->ground_counter == g_info->counterafttd) {
			time(&g_info->touchTime);
			/*-- stop data collection*/
			g_info->collect_touchdown_data = FALSE;
			g_info->air_counter = 0;
			XPLMDebugString("XTouchDownRecorder: touchdown\n");
		} else if (g_info->ground_counter == (g_info->counterafttd+1)) {
			XTDCopy(datacopy, datarealtm);
			XPLMDebugString("XTouchDownRecorder: touchdown log\n");
			if(analyzeTouchDown(datacopy, tmpbuf, 0, 0, FALSE)){
				write_log_file_async();
			} else {
				sprintf(tmpbuf, "XTouchDownRecorder: touchdown miss counter=%d\n", g_info->counterafttd);
				XPLMDebugString(tmpbuf);
			}
		}
	} else {
		if (g_info->air_counter == 10) {
			//recalibrate stop time
			fps= XPLMGetDataf(fpsRef)*1000.0f;
			counter = (int)((float)MAX_TABLE_ELEMENTS/2.0f/fps);
			if ((counter > 2) && (counter< g_info->counterafttd)){
				g_info->counterafttd = counter;
				sprintf(tmpbuf, "XTouchDownRecorder: touchdown stop dynamic counter=%d\n", counter);
				XPLMDebugString(tmpbuf);
			}
			g_info->ground_counter = 0;
			g_info->IsTouchDown = FALSE;
			g_info->taxi_counter = 0;
		}
		/*-- in the air*/
		g_info->air_counter =  g_info->air_counter + 1;
		g_info->collect_touchdown_data = TRUE;

	}
	/*-- count down*/
	if (g_info->show_touchdown_counter > 0) {
		g_info->show_touchdown_counter = g_info->show_touchdown_counter - 1;
	}

	return 1.0f;
}

static void toggle_touchdown()
{
	if (g_info->show_touchdown_counter > 0) {
		g_info->show_touchdown_counter = 0;
	} else {
		g_info->show_touchdown_counter = 60;
	}
}
static int ToggleCommandHandler(XPLMCommandRef       inCommand,
								XPLMCommandPhase     inPhase,
								void *               inRefcon)
{
	if (inPhase == xplm_CommandBegin) {
		toggle_touchdown();
	}
	return 0;
}

static void menucb(void *menuRef, void *param)
{
	toggle_touchdown();
}

static void GetXPVer()
{
	XPLMGetVersions(&g_info->xpVer, &g_info->xplmVer, &g_info->hostID);
}

static unsigned int lightworker_job_helper(void *arg)
{

	getnetinfo();
	if (!getnetinfodone()) {
		getnetinfo();
	}

	lightworker_queue_task * task;
	while (!g_info->lightworkerexit) {
		task = lightworker_queue_get_single(g_info->worker);
		if (task) {
			switch (task->msg) {
			case 1105:
				write_log_file();
				break;
			case 1051:
				enumfolder();
				break;
			}
		}
	}
	return 0;
}
int	CreateAgreeWidgetsHandler(
						XPWidgetMessage			inMessage,
						XPWidgetID				inWidget,
						intptr_t				inParam1,
						intptr_t				inParam2)
{
	// Close button pressed, only hide the widget, rather than destropying it.
	if (inMessage == xpMessage_CloseButtonPushed)
	{

		XPHideWidget(inWidget);

		return 1;
	}

	// Test for a button pressed
	if (inMessage == xpMsg_PushButtonPressed)
	{
		g_info->conf.agree = 1;
		XPHideWidget(g_info->AgreeMainWidget);
		return XPluginStartBH();
	}
	return 0;
}
void CreateAgreeWidgets(int x, int y)
{
	int w = 400;
	int h = 240;
	int x2 = x + w;
	int y2 = y - h;
	XPLMWindowID winid;

	XPWidgetID AgreeMainWidget = g_info->AgreeMainWidget = XPCreateWidget(x, y, x2, y2,
					1,	// Visible
					"XTouchDownRecorder User Agreement",	// desc
					1,		// root
					NULL,	// no container
					xpWidgetClass_MainWindow);

	XPSetWidgetProperty(AgreeMainWidget, xpProperty_MainWindowHasCloseBoxes, 1);
#ifdef XPLM300
	winid = XPGetWidgetUnderlyingWindow(AgreeMainWidget);
	//XPLMSetWindowPositioningMode(winid, xplm_WindowPopOut,0);
#endif

	XPCreateWidget(x+50, y-50, x2-50, y-70,
							1, "XTouchDownRecorder will collect your landing data \n", 0, AgreeMainWidget,
							xpWidgetClass_Caption);
	XPCreateWidget(x+50, y-70, x2-50, y-90,
							1, "for improving software,\n", 0, AgreeMainWidget,
							xpWidgetClass_Caption);
	XPCreateWidget(x+50, y-110, x2-50, y-130,
							1, "Click Agree button or Close window for Disagree", 0, AgreeMainWidget,
							xpWidgetClass_Caption);


	XPWidgetID AgreeMainWidgetButton = XPCreateWidget(x+100, y-170, x+240, y-190,
					1, "Agree", 0, AgreeMainWidget,
					xpWidgetClass_Button);

	XPSetWidgetProperty(AgreeMainWidgetButton, xpProperty_ButtonType, xpPushButton);
	XPAddWidgetCallback(AgreeMainWidget, CreateAgreeWidgetsHandler);
	XPShowWidget(AgreeMainWidget);
}

static void creatmainwin(void)
{
	
	XTDWin * ref = g_info->winref;
	if (!g_info->g_win) {
		//ref->win.posx = 10;
		//ref->win.posy = 900;
		ref->win.width = _TD_CHART_WIDTH;
		ref->win.height = _TD_CHART_HEIGHT;
		/*close button*/
		ref->close.width = 10;
		ref->close.height = 10;
		XPLMCreateWindow_t win;
		memset(&win, 0, sizeof(win));
		win.structSize = sizeof(win);
		win.left = ref->win.posx;
		win.top = ref->win.posy;
		win.right = ref->win.posx + ref->win.width;
		win.bottom = ref->win.posy - ref->win.height;
		win.visible = 0;
		win.drawWindowFunc = drawcb;
		win.handleKeyFunc = keycb;
		win.handleMouseClickFunc = mousecb;
#ifdef XPLM300		
		win.handleRightClickFunc = rmousecb;
#endif
		win.handleCursorFunc = cursorcb;			
		win.handleMouseWheelFunc = dummy_wheel_cb;
		win.refcon = ref;
#ifdef XPLM300
		win.layer = xplm_WindowLayerFloatingWindows;
		win.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;
#endif
		g_info->g_win = XPLMCreateWindowEx(&win);
#ifdef XPLM300		
		XPLMSetWindowPositioningMode(g_info->g_win, xplm_WindowPositionFree, -1);
		XPLMSetWindowResizingLimits(g_info->g_win, ref->win.width, ref->win.height, ref->win.width, ref->win.height);
		XPLMSetWindowTitle(g_info->g_win, _PRONAMEVER_);
		//XPWidgetID AgreeMainWidgetButton = XPCreateWidget(x+100, y-170, x+240, y-190,
		//			1, "Agree", 0, AgreeMainWidget,
		//			xpWidgetClass_Button);
#endif
	}
}

PLUGIN_API int XPluginStart(char * outName, char * outSig, char * outDesc)
{
	char path[600], *prefpath;
	const char *csep;

	/* MAC OS */
	XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);
#ifdef XPLM300
	XPLMEnableFeature("XPLM_USE_NATIVE_WIDGET_WINDOWS", 1);
#endif

	/* Plugin details */
	sprintf(outName, _PRONAMEVER_" %s %s", __DATE__ , __TIME__);
	strcpy(outSig, "cpuwolf.xtouchdownrecorder");
	strcpy(outDesc, "More information https://github.com/cpuwolf/");


	g_info = (XTDInfo *)malloc(sizeof(XTDInfo));
	if (!g_info) {
		XPLMDebugString("XTouchDownRecorder:malloc info error!\n");
		return 0;
	}
	memset(g_info, 0, sizeof(XTDInfo));
	g_info->collect_touchdown_data = TRUE;
	g_info->ground_counter = 10;
	g_info->counterafttd = MAX_TABLE_ELEMENTS/2/30;

	/*get XP version info*/
	GetXPVer();

	/* get path*/
	XPLMGetPrefsPath(path);

	csep=XPLMGetDirectorySeparator();
	prefpath = XPLMExtractFileAndPath(path);
	/* copy pref path */
	strcpy(g_info->g_xpperfpath, path);

	size_t len = strlen(path);
	sprintf(path+len,"%c..%c",*csep,*csep);

	sprintf(g_info->g_xpperfpath+len,"%c",*csep);

	strcpy(g_info->g_xppath, path);
	sprintf(path, "XTouchDownRecorder: xp path %s\n", g_info->g_xppath);
	XPLMDebugString(path);

	datarealtm = XTDMalloc();
	if(!datarealtm) {
		XPLMDebugString("XTouchDownRecorder:malloc datarealtm error!\n");
		return 0;
	}

	datacopy = XTDMalloc();
	if (!datacopy) {
		XPLMDebugString("XTouchDownRecorder:malloc datacopy error!\n");
		return 0;
	}


	gearFRef = XPLMFindDataRef("sim/flightmodel/forces/fnrml_gear");
	gForceRef = XPLMFindDataRef("sim/flightmodel2/misc/gforce_normal");
	vertSpeedRef = XPLMFindDataRef("sim/flightmodel/position/vh_ind_fpm2");
	pitchRef = XPLMFindDataRef("sim/flightmodel/position/theta");
	elevatorRef = XPLMFindDataRef("sim/flightmodel2/controls/pitch_ratio");
	engRef = XPLMFindDataRef("sim/flightmodel2/engines/throttle_used_ratio");
	aglRef = XPLMFindDataRef("sim/flightmodel/position/y_agl");
	tmRef = XPLMFindDataRef("sim/time/total_flight_time_sec");

	gndSpeedRef = XPLMFindDataRef("sim/flightmodel/position/groundspeed");
	latRef = XPLMFindDataRef("sim/flightmodel/position/latitude");
	longRef = XPLMFindDataRef("sim/flightmodel/position/longitude");
	tailRef = XPLMFindDataRef("sim/aircraft/view/acf_tailnum");
	icaoRef = XPLMFindDataRef("sim/aircraft/view/acf_ICAO");
	totalWeightRef = XPLMFindDataRef("sim/flightmodel/weight/m_total");
	fpsRef = XPLMFindDataRef("sim/operation/misc/frame_rate_period");
	vxRef = XPLMFindDataRef("sim/flightmodel/forces/vx_air_on_acf");


#if defined(__linux__)
	g_info->curl_disable_ssl_verify = TRUE;
#endif

	XPLMDebugString("XTouchDownRecorder: read config\n");
	XTDWin * ref = (XTDWin *)malloc(sizeof(XTDWin));
	if (!ref) {
		XPLMDebugString("XTouchDownRecorder: win malloc error\n");
		return 0;
	}
	g_info->winref = ref;

	/*load configuration*/
	if(read_config_file()) {
		if ((g_info->conf.px > 0) && (g_info->conf.px < 10000)
			&& (g_info->conf.py > 0) && (g_info->conf.py < 10000)) {
			ref->win.posx = g_info->conf.px;
			ref->win.posy = g_info->conf.py;
		} else {
			ref->win.posx = 10;
			ref->win.posy = 900;
		}
	} else {
		ref->win.posx = 10;
		ref->win.posy = 900;
	}


#if 0	
	if (g_info->conf.agree != 1) {
		CreateAgreeWidgets(ref->win.posx, ref->win.posy);
		return 1;
	}
#endif
	return XPluginStartBH();
}

static int XPluginStartBH()
{
	XPLMMenuID plugins_menu;
	int menuidx, i;
	char path[256],buf[512], pathdbg[128],pathcache[128];
	const char cefpath[]="Resources\\plugins\\XTouchDownRecorder\\64\\xtouchdownrecorder_ui.exe";
	XPLMPluginID addonid = XPLMGetMyID();
	sprintf(path,"XTouchDownRecorder: addon id=%d\n",addonid);
	XPLMDebugString(path);
	XPLMGetPluginInfo(addonid, NULL, path, NULL, NULL);
	sprintf(buf,"XTouchDownRecorder: addon path=%s\n", path);
	XPLMDebugString(buf);
	size_t plen =strlen(path);
	if(plen > 10) {
		for(i=plen-1;i>0;i--){
			char *c= path+i;
			if((*c=='/')||(*c=='\\')){
				*c=0;
				break;
			}
		}
	}
	plen =strlen(path);
	if(plen > 10) {
		strcat(path, "\\xtouchdownrecorder_ui.exe");
	} else {
		strcpy(path, cefpath);
	}
	sprintf(buf,"XTouchDownRecorder: CEF path=%s\n", path);
	XPLMDebugString(buf);

	strcpy(pathdbg, g_info->g_xppath);
	strcat(pathdbg, "\\XTouchDownRecorderdebug.log");
	
	strcpy(pathcache, g_info->g_xppath);
	strcat(pathcache, "\\XTouchDownRecordercache");
#if defined(_WIN32)
	_mkdir(pathcache);
#else 
	mkdir(pathcache, 0733);
#endif	
	
	XPLMDebugString("XTouchDownRecorder: start CEF\n");
	g_info->pcef=CEF_init(_TD_CHART_WIDTH, _TD_CHART_HEIGHT, path, pathdbg, pathcache);
	if(CEF_ready()){
		XPLMDebugString("XTouchDownRecorder: CEF_init OK\n");
	} else {
		XPLMDebugString("XTouchDownRecorder: CEF_init failed\n");
		if(g_info->pcef) {
			sprintf(buf,"XTouchDownRecorder: CEF error code=%d\n", g_info->pcef->errorcode);
		}
		return 0;
	}
	
	/*create a worker*/
	g_info->worker = lightworker_create(lightworker_job_helper, g_info);
	/* register loopback starting at 10s */
	XPLMRegisterFlightLoopCallback(flightcb, -1, g_info->winref);

	/* register loopback in 1s */
	XPLMRegisterFlightLoopCallback(secondcb, 1.0f, NULL);
	XPLMDebugString("XTouchDownRecorder: loop done\n");

	g_info->ToggleCommand = XPLMCreateCommand("cpuwolf/XTouchDownRecorder/Toggle", "Toggle TouchDownRecorder Chart");
	XPLMRegisterCommandHandler(g_info->ToggleCommand, ToggleCommandHandler, 0, NULL);

	plugins_menu = XPLMFindPluginsMenu();
	menuidx = XPLMAppendMenuItem(plugins_menu, "XTouchDownRecorder", NULL, 1);
	g_info->tdr_menu = XPLMCreateMenu("XTouchDownRecorder", plugins_menu, menuidx,
				menucb, NULL);
	XPLMAppendMenuItem(g_info->tdr_menu, "Show/Hide", NULL, 1);
	XPLMDebugString("XTouchDownRecorder: start enum\n");
	enumfolder_async();

	creatmainwin();

	//CEF_update();

	return 1;
}

PLUGIN_API void	XPluginStop(void)
{

	XPLMUnregisterCommandHandler(g_info->ToggleCommand, ToggleCommandHandler, 0, 0);
	XPLMUnregisterFlightLoopCallback(secondcb, NULL);
	XPLMUnregisterFlightLoopCallback(flightcb, NULL);
	g_info->lightworkerexit = TRUE;
	lightworker_destroy(g_info->worker);

	if (g_info->g_win) {
		XTDWin *ref=(XTDWin *)XPLMGetWindowRefCon(g_info->g_win);
		if (ref) {
			/*update config*/
			g_info->conf.px = ref->win.posx;
			g_info->conf.py = ref->win.posy;
			free(ref);
		}
		XPLMDestroyWindow(g_info->g_win);
		//g_win = NULL;
	}

	if(CEF_ready()){
		CEF_deinit(g_info->pcef);
	}
	XPLMDebugString("XTouchDownRecorder: CEF close\n");
	if (g_info->tdr_menu) {
		XPLMClearAllMenuItems(g_info->tdr_menu);
		XPLMDestroyMenu(g_info->tdr_menu);
	}
	if(datarealtm) {
		free(datarealtm);
		//datarealtm = NULL;
	}

	if (datacopy) {
		free(datacopy);
		//datacopy = NULL;
	}

	/*write configuration*/
	write_config_file();
	XPLMDebugString("XTouchDownRecorder: write config\n");

	if (!g_info) {
		free(g_info);
	}
}

PLUGIN_API void XPluginDisable(void)
{
}

PLUGIN_API int XPluginEnable(void)
{
	return 1;
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inForm, int inMessage, void * inParam) 
{
	if (inForm == XPLM_PLUGIN_XPLANE) {
		switch(inMessage) {
		case XPLM_MSG_PLANE_LOADED:
			if (inParam == NULL) {
				_BUFFER_DELETE(datarealtm);
				_BUFFER_DELETE(datacopy);
			}
			break;
		}
	}
}
