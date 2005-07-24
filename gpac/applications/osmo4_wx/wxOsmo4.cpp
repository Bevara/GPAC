/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Copyright (c) Jean Le Feuvre 2000-2005
 *					All rights reserved
 *
 *  This file is part of GPAC / Osmo4 wxWidgets GUI
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
 *		
 */


#include "wxOsmo4.h"
#include "wxGPACControl.h"
#include "fileprops.h"
#include "wx/image.h"
#include <gpac/modules/service.h>
#include <gpac/network.h>
#include <gpac/constants.h>
#include <gpac/options.h>


IMPLEMENT_APP(wxOsmo4App)

#include "osmo4.xpm"
#include <wx/dnd.h>
#include <wx/filename.h>

#include "toolbar.xpm"

#include "Playlist.h"

#ifdef WIN32
#define	FRAME_H	140
#else
#define	FRAME_H	110
#endif



wxString get_pref_browser(GF_Config *cfg)
{
	const char *sOpt = gf_cfg_get_key(cfg, "General", "Browser");
	if (sOpt) return wxString(sOpt, wxConvUTF8);
#ifdef __WXMAC__
	return wxT("safari");
#else
#ifdef WIN32
	return wxT("explorer.exe");
#else
	return wxT("mozilla");
#endif
#endif
}


IMPLEMENT_DYNAMIC_CLASS(wxGPACEvent, wxEvent )

wxGPACEvent::wxGPACEvent(wxWindow* win)
{
    SetEventType(GPAC_EVENT);
    SetEventObject(win);
	gpac_evt.type = 0;
	to_url = wxT("");
}
wxEvent *wxGPACEvent::Clone() const
{
	wxGPACEvent *evt = new wxGPACEvent((wxWindow *) m_eventObject);
	evt->to_url = to_url;
	evt->gpac_evt = gpac_evt;
	return evt;
}



/*open file dlg*/
BEGIN_EVENT_TABLE(OpenURLDlg, wxDialog)
    EVT_BUTTON(ID_URL_GO, OpenURLDlg::OnGo)
END_EVENT_TABLE()

OpenURLDlg::OpenURLDlg(wxWindow *parent, GF_Config *cfg)
             : wxDialog(parent, -1, wxString(wxT("Enter remote presentation location")))
{
#ifndef WIN32
	SetSize(430, 35);
#else
	SetSize(430, 55);
#endif
	Centre();
    m_url = new wxComboBox(this, -1, wxT(""), wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_DROPDOWN);
	m_url->SetSize(0, 2, 340, 18, wxSIZE_AUTO);
    m_go = new wxButton(this, ID_URL_GO, wxT("Go !"));
#ifndef WIN32
	m_go->SetSize(344, 2, 20, 18, wxSIZE_AUTO);
#else
	m_go->SetSize(364, 2, 30, 18, wxSIZE_AUTO);
#endif
	m_urlVal = wxT("");

	m_cfg = cfg;

	const char *sOpt;
	char filename[1024];
	u32 i=0;

	while (1) {
		sprintf(filename, "last_file_%d", i);
		sOpt = gf_cfg_get_key(m_cfg, "General", filename);
		if (!sOpt) break;
		m_url->Append(wxString(sOpt, wxConvUTF8) );
		i++;
	}
}

void OpenURLDlg::OnGo(wxCommandEvent& event)
{
	m_urlVal = m_url->GetValue();
	const char *sOpt;
	char filename[1024];
	u32 i=0;

	while (1) {
		sprintf(filename, "last_file_%d", i);
		sOpt = gf_cfg_get_key(m_cfg, "General", filename);
		if (!sOpt) break;
		if (!strcmp(sOpt, m_urlVal.mb_str(wxConvUTF8))) {
			EndModal(wxID_OK);
			return;
		}
		i++;
	}
	/*add it*/
	if (i<10) {
		gf_cfg_set_key(m_cfg, "General", filename, m_urlVal.mb_str(wxConvUTF8));
	} else {
		gf_cfg_set_key(m_cfg, "General", "last_file_10", m_urlVal.mb_str(wxConvUTF8));
	}
	EndModal(wxID_OK);
}
/*end open file dlg*/



Bool GPAC_EventProc(void *ptr, GF_Event *evt)
{
	wxCommandEvent event;
	wxOsmo4Frame *app = (wxOsmo4Frame *)ptr;

	switch (evt->type) {
	case GF_EVT_DURATION:
		app->m_duration = (u32) (evt->duration.duration*1000);
		app->m_can_seek = evt->duration.can_seek;
		if (app->m_duration<1100) app->m_can_seek = 0;
		app->m_pProg->Enable(app->m_can_seek ? 1 : 0);
		app->m_pPlayList->SetDuration((u32) evt->duration.duration);
		break;
	case GF_EVT_MESSAGE:
	{
		const char *servName;
		if (!evt->message.service || !strcmp(evt->message.service, app->m_pPlayList->GetURL().mb_str(wxConvUTF8))) {
			servName = "main service";
		} else {
			servName = evt->message.service;
		}
		if (!evt->message.message) return 0;

		if (evt->message.error) {
			app->SetStatus(wxString(evt->message.message, wxConvUTF8) + wxT(" (") + wxString(servName, wxConvUTF8) + wxT(")") );
			if (!app->m_connected) app->m_pPlayList->SetDead();
		}
		else if (!app->m_console_off) {
			if (strstr(evt->message.message, "100 %")) {
				app->SetStatus(wxT(""));
			} else {
				app->SetStatus(wxString(evt->message.message, wxConvUTF8) );
			}
		}

		/*log*/
		if (evt->message.error) 
			::wxLogMessage(wxString(evt->message.message, wxConvUTF8) + wxT(" (") + wxString(servName, wxConvUTF8) + wxT(") ") + wxString(gf_error_to_string(evt->message.error), wxConvUTF8) );
		else
			::wxLogMessage(wxString(evt->message.message, wxConvUTF8) + wxT(" (") + wxString(servName, wxConvUTF8) + wxT(")"));
	}
		break;
	
	case GF_EVT_VKEYDOWN:
		if (app->m_can_seek && (evt->key.key_states & GF_KM_ALT)) {
			s32 res;
			switch (evt->key.vk_code) {
			case GF_VK_LEFT:
				res = gf_term_get_time_in_ms(app->m_term) - 5*app->m_duration/100;
				if (res<0) res=0;
				gf_term_play_from_time(app->m_term, res);
				break;
			case GF_VK_RIGHT:
				res = gf_term_get_time_in_ms(app->m_term) + 5*app->m_duration/100;
				if ((u32) res>=app->m_duration) res = 0;
				gf_term_play_from_time(app->m_term, res);
				break;
			case GF_VK_DOWN:
				res = gf_term_get_time_in_ms(app->m_term) - 60000;
				if (res<0) res=0;
				gf_term_play_from_time(app->m_term, res);
				break;
			case GF_VK_UP:
				res = gf_term_get_time_in_ms(app->m_term) + 60000;
				if ((u32) res>=app->m_duration) res = 0;
				gf_term_play_from_time(app->m_term, res);
				break;
			}
		} else if (evt->key.key_states & GF_KM_CTRL) {
			switch (evt->key.vk_code) {
			case GF_VK_LEFT:
				app->m_pPlayList->PlayPrev();
				break;
			case GF_VK_RIGHT:
				app->m_pPlayList->PlayNext();
				break;
			}
		} else {
			switch (evt->key.vk_code) {
			case GF_VK_HOME:
				gf_term_set_option(app->m_term, GF_OPT_NAVIGATION_TYPE, 1);
				break;
			case GF_VK_ESCAPE:
				if (gf_term_get_option(app->m_term, GF_OPT_FULLSCREEN)) 
					gf_term_set_option(app->m_term, GF_OPT_FULLSCREEN, 0);
				break;
			}
		}
		break;

	case GF_EVT_CONNECT:
	{
		wxGPACEvent wxevt(app);
		wxevt.gpac_evt.type = GF_EVT_CONNECT;
		wxevt.gpac_evt.connect.is_connected = evt->connect.is_connected;
		if (!evt->connect.is_connected) app->m_duration = 0;
		app->AddPendingEvent(wxevt);
	}
		break;
	case GF_EVT_NAVIGATE:
	{
		wxGPACEvent wxevt(app);
		wxevt.to_url = wxString(evt->navigate.to_url, wxConvUTF8);
		wxevt.gpac_evt.type = evt->type;
		app->AddPendingEvent(wxevt);
	}
		return 1;

	case GF_EVT_KEYDOWN:
	case GF_EVT_QUIT:
	case GF_EVT_VIEWPOINTS:
	case GF_EVT_STREAMLIST:
	case GF_EVT_SCENESIZE:
	{
		wxGPACEvent wxevt(app);
		wxevt.gpac_evt = *evt;
		app->AddPendingEvent(wxevt);
	}
		break;
	case GF_EVT_LDOUBLECLICK:
		gf_term_set_option(app->m_term, GF_OPT_FULLSCREEN, !gf_term_get_option(app->m_term, GF_OPT_FULLSCREEN));
		return 0;
	case GF_EVT_LEFTDOWN:
	case GF_EVT_RIGHTDOWN:
	case GF_EVT_MIDDLEDOWN:
		app->m_pView->SetFocus();
		break;
	}
	return 0;
}



bool wxOsmo4App::OnInit()
{
	wxFrame *frame = new wxOsmo4Frame();
	frame->Show(TRUE);
	SetTopWindow(frame);
	return true;	
}


class myDropfiles : public wxFileDropTarget
{
public:
	myDropfiles() : wxFileDropTarget() {}
	virtual bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& filenames);
	wxOsmo4Frame *m_pMain;
};

bool myDropfiles::OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& filenames)
{
	u32 count = filenames.GetCount();

	if (count==1) {
		char *ext = strrchr(filenames.Item(0).mb_str(wxConvUTF8) , '.');
		/*if playing and sub d&d, open sub in current presentation*/
		if (m_pMain->m_connected && ext && ( !stricmp(ext, ".srt") || !stricmp(ext, ".sub") || !stricmp(ext, ".ttxt") || !stricmp(ext, ".xml") ) ) {
			m_pMain->AddSubtitle(filenames.Item(0).mb_str(wxConvUTF8) , 1);
			return TRUE;
		}
	}

	for (u32 i=0; i<count; i++) 
		m_pMain->m_pPlayList->QueueURL(filenames.Item(i));

	m_pMain->m_pPlayList->RefreshList();
	m_pMain->m_pPlayList->PlayNext();
	return TRUE;
}

bool GPACLogs::OnFrameClose(wxFrame *frame)
{
	Show(FALSE);
	return 0;
}

#ifdef __WXGTK__
extern "C" {
#ifdef __WXGTK20__
    int gdk_x11_drawable_get_xid( void * );
    void *gdk_x11_drawable_get_xdisplay( void * );
#endif
    void *gtk_widget_get_parent_window( void * );
}
#endif

/*thats a bit ugly, but SDL hack doesnt work properly*/
void wxOsmo4Frame::CheckVideoOut()
{
	const char *sOpt = gf_cfg_get_key(m_user.config, "Video", "DriverName");
	void *os_handle = NULL;
	void *os_display = NULL;
	/*build a child window for embed display*/
	if (stricmp(sOpt, "SDL Video Output")) {
		if (m_user.os_window_handler) return;
		m_bExternalView = 0;

#ifdef __WXGTK__
		GtkWidget* widget = m_pView->GetHandle();
#ifdef __WXGTK20__
		os_handle = (void *)gdk_x11_drawable_get_xid(gtk_widget_get_parent_window(widget));
		os_display = (void *)gdk_x11_drawable_get_xdisplay(gtk_widget_get_parent_window(widget));
#else
		os_handle =  (void *)*(int *)( (char *)gtk_widget_get_parent_window(widget) + 2 * sizeof(void *) );
#endif
		/*FIXME - X11 output is not stable enough*/
		os_handle = NULL;
		os_display = NULL;

#elif defined (WIN32)
		os_handle = m_pView->GetHandle();
#endif

		if (os_handle) {
			m_user.os_window_handler = os_handle;
			m_user.os_display = os_display;
//			m_pSizer->Show(m_pViewSizer, 1);
			m_pView->Show(1);
			SetSize(wxSize(320, 240+FRAME_H));
			SetWindowStyle(wxDEFAULT_FRAME_STYLE);
			DoLayout(320, 240);
			return;
		}
	}
	/*we're using SDL, don't use SDL hack*/
	m_bExternalView = 1;
//	m_pSizer->Show(m_pViewSizer, 0);
	m_user.os_window_handler = 0;
	m_user.os_display = NULL;
	SetSize(wxSize(320,FRAME_H));
	m_pView->SetSize(320,0);
	m_pView->Show(0);
	DoLayout();
	SetWindowStyle(wxDEFAULT_FRAME_STYLE & ~(wxMAXIMIZE_BOX | wxRESIZE_BORDER));
}

Bool wxOsmo4Frame::LoadTerminal()
{
	m_term = NULL;
	memset(&m_user, 0, sizeof(GF_User));

	/*locate exec dir for cfg file*/
    wxPathList pathList;
	wxString currentDir(wxGetCwd());
    wxString abs_gpac_path = wxT("");
	const char *gpac_cfg;

	::wxLogMessage(wxT("Looking for GPAC configuration file"));

#if defined(__WXMAC__) && !defined(__DARWIN__)
    // On Mac, the current directory is the relevant one when the application starts.
    abs_gpac_path = wxGetCwd();
	gpac_cfg = "GPAC.cfg";
#else

#ifdef WIN32
	wxOsmo4App &app = wxGetApp();
	gpac_cfg = "GPAC.cfg";
	/*locate exe*/
    if (wxIsAbsolutePath(app.argv[0])) {
        abs_gpac_path = wxPathOnly(app.argv[0]);
	} else {
		if (currentDir.Last() != wxFILE_SEP_PATH) currentDir += wxFILE_SEP_PATH;
		abs_gpac_path = currentDir + app.argv[0];
		if (wxFileExists(abs_gpac_path)) {
			abs_gpac_path = wxPathOnly(abs_gpac_path);
		} else {
			abs_gpac_path = wxT("");
			pathList.AddEnvList(wxT("PATH"));
			abs_gpac_path = pathList.FindAbsoluteValidPath(app.argv[0]);
			if (!abs_gpac_path.IsEmpty()) {
				abs_gpac_path = wxPathOnly(abs_gpac_path);
			} else {
				/*ask user*/
				wxDirDialog dlg(NULL, wxT("Locate GPAC config file directory"));
				if ( dlg.ShowModal() != wxID_OK ) return 0;
				abs_gpac_path = dlg.GetPath();
			}
		}
	}
#else
	gpac_cfg = ".gpacrc";
	char *cfg_dir = getenv("HOME");
	if (cfg_dir) {
		abs_gpac_path = wxString(cfg_dir, wxConvUTF8);
	} else {
		/*ask user*/
		wxDirDialog dlg(NULL, wxT("Locate GPAC config file directory"));
		if ( dlg.ShowModal() != wxID_OK ) return 0;
		abs_gpac_path = dlg.GetPath();
	}

#endif

#endif

	/*load config*/
	m_user.config = gf_cfg_new(abs_gpac_path.mb_str(wxConvUTF8), gpac_cfg);

	if (!m_user.config) {
		unsigned char config_file[GF_MAX_PATH];
		strcpy((char *) config_file, (const char *) abs_gpac_path.mb_str(wxConvUTF8));
		if (config_file[strlen((char *) config_file)-1] != GF_PATH_SEPARATOR) {
		  char szSep[2];
		  szSep[0] = GF_PATH_SEPARATOR;
		  szSep[1] = 0;
		  strcat((char *) config_file, (const char *)szSep);
		}
		strcat((char *) config_file, gpac_cfg);
		FILE *ft = fopen((const char *) config_file, "wt");
		if (!ft) {
			wxMessageDialog(NULL, wxT("Cannot create blank config file"), wxT("Init error"), wxOK).ShowModal();
			return 0;
		}
		fclose(ft);
		m_user.config = gf_cfg_new(abs_gpac_path.mb_str(wxConvUTF8), gpac_cfg);
		if (!m_user.config) {
			wxMessageDialog(NULL, wxT("Cannot open GPAC configuration file"), wxT("Init error"), wxOK);
			return 0;
		}
	}
	strcpy(szAppPath, abs_gpac_path.mb_str(wxConvUTF8));
	if (szAppPath[strlen(szAppPath)] != GF_PATH_SEPARATOR)
		sprintf(szAppPath, "%s%c", szAppPath, GF_PATH_SEPARATOR);

	::wxLogMessage(wxT("GPAC configuration file opened - looking for modules"));
	const char *str = gf_cfg_get_key(m_user.config, "General", "ModulesDirectory");
	Bool first_launch = 0;
	if (!str) {
	  first_launch = 1;
#ifdef GPAC_MODULES_PATH
		str = GPAC_MODULES_PATH;
#else
		str = abs_gpac_path.mb_str(wxConvUTF8);
#endif
	}
	m_user.modules = gf_modules_new((const unsigned char *) str, m_user.config);

	/*initial launch*/
	if (first_launch || !gf_modules_get_count(m_user.modules)) {
		const char *sOpt;
		wxDirDialog dlg(NULL, wxT("Locate GPAC modules directory"));
		if  (!gf_modules_get_count(m_user.modules)) {
		  gf_modules_del(m_user.modules);
		  m_user.modules = NULL;
			if ( dlg.ShowModal() != wxID_OK ) return false;
			str = dlg.GetPath().mb_str(wxConvUTF8);
	
			m_user.modules = gf_modules_new((const unsigned char *) str, m_user.config);
			if (!m_user.modules || !gf_modules_get_count(m_user.modules) ) {
				wxMessageDialog(NULL, wxT("Cannot find any modules for GPAC"), wxT("Init error"), wxOK);
				gf_cfg_del(m_user.config);
				return 0;
			}
		}

		u32 i;
		for (i=0; i<gf_modules_get_count(m_user.modules); i++) {
			GF_InputService *ifce = (GF_InputService *) gf_modules_load_interface(m_user.modules, i, GF_NET_CLIENT_INTERFACE);
			if (!ifce) continue;
			if (ifce) {
				ifce->CanHandleURL(ifce, "test.test");
				gf_modules_close_interface((GF_BaseInterface *) ifce);
			}
		}

		gf_cfg_set_key(m_user.config, "General", "ModulesDirectory", (const char *) str);

		/*setup UDP traffic autodetect*/
		gf_cfg_set_key(m_user.config, "Network", "AutoReconfigUDP", "yes");
		gf_cfg_set_key(m_user.config, "Network", "UDPNotAvailable", "no");
		gf_cfg_set_key(m_user.config, "Network", "UDPTimeout", "10000");
		gf_cfg_set_key(m_user.config, "Network", "BufferLength", "3000");

		/*check audio config on windows, force config*/
		sOpt = gf_cfg_get_key(m_user.config, "Audio", "ForceConfig");
		if (!sOpt) {
			gf_cfg_set_key(m_user.config, "Audio", "ForceConfig", "yes");
			gf_cfg_set_key(m_user.config, "Audio", "NumBuffers", "8");
			gf_cfg_set_key(m_user.config, "Audio", "BuffersPerSecond", "16");
		}

#ifdef WIN32
		sOpt = gf_cfg_get_key(m_user.config, "Rendering", "Raster2D");
		if (!sOpt) gf_cfg_set_key(m_user.config, "Rendering", "Raster2D", "gdip_rend");
		sOpt = gf_cfg_get_key(m_user.config, "General", "CacheDirectory");
		if (!sOpt) {
			unsigned char str_path[MAX_PATH];
			sprintf((char *) str_path, "%scache", abs_gpac_path.mb_str(wxConvUTF8));
			gf_cfg_set_key(m_user.config, "General", "CacheDirectory", (const char *) str_path);
		}
		/*by default use GDIplus, much faster than freetype on font loading*/
		gf_cfg_set_key(m_user.config, "FontEngine", "DriverName", "gdip_rend");
		gf_cfg_set_key(m_user.config, "Video", "DriverName", "DirectX Video Output");
#else
		wxDirDialog dlg3(NULL, wxT("Please specify a cache directory for GPAC"));
		dlg3.SetPath(wxT("/tmp"));
		if ( dlg3.ShowModal() == wxID_OK ) 
			gf_cfg_set_key(m_user.config, "General", "CacheDirectory", (const char *) dlg3.GetPath().mb_str(wxConvUTF8) );

		wxDirDialog dlg2(NULL, wxT("Please locate a TrueType font repository on your system for text support"));
		dlg2.SetPath(wxT("/usr/share/fonts/truetype"));
		if ( dlg2.ShowModal() == wxID_OK ) 
			gf_cfg_set_key(m_user.config, "FontEngine", "FontDirectory", (const char *) dlg2.GetPath().mb_str(wxConvUTF8) );

		gf_cfg_set_key(m_user.config, "Video", "DriverName", "SDL Video Output");
#endif
	}	
	if (! gf_modules_get_count(m_user.modules) ) {
		wxMessageDialog(NULL, wxT("No modules available - system cannot work"), wxT("Fatal Error"), wxOK).ShowModal();
		gf_modules_del(m_user.modules);
		gf_cfg_del(m_user.config);
		return 0;
	}

	::wxLogMessage(wxT("%d modules found:"), gf_modules_get_count(m_user.modules));
	for (u32 i=0; i<gf_modules_get_count(m_user.modules); i++) {
		::wxLogMessage(wxT("\t") + wxString(gf_modules_get_file_name(m_user.modules, i), wxConvUTF8) );
	}

	::wxLogMessage(wxT("Starting MPEG-4 Terminal"));
	/*now load terminal*/
	m_user.opaque = this;
	m_user.EventProc = GPAC_EventProc;

	CheckVideoOut();

	m_term = gf_term_new(&m_user);
	if (!m_term) {
		wxMessageDialog(NULL, wxT("Fatal Error"), wxT("Cannot load MPEG-4 Terminal"), wxOK).ShowModal();
		return 0;
	} else {
		::wxLogMessage(wxT("MPEG-4 Terminal started - using ") + wxString(gf_cfg_get_key(m_user.config, "Rendering", "RendererName"), wxConvUTF8) );
	}
	return 1;
}


wxOsmo4Frame::wxOsmo4Frame() :
		wxFrame((wxFrame *) NULL, -1, wxT("Osmo4 - GPAC"), wxPoint(-1, -1), wxSize(320, FRAME_H), //wxDEFAULT_FRAME_STYLE & ~(wxMAXIMIZE_BOX | wxRESIZE_BORDER)
wxDEFAULT_FRAME_STYLE
		)

{
	int ws[3];
	m_Address = NULL;
	m_pView = NULL;
	m_term = NULL;
	SetIcon(wxIcon(osmo4));
	m_bExternalView = 0;

	m_num_chapters = 0;
	m_chapters_start = NULL;


	myDropfiles *droptarget = new myDropfiles();
	droptarget->m_pMain = this;
	SetDropTarget(droptarget);
	m_pLogs = new GPACLogs(this);
	m_bGrabbed = 0;

	/*new menu bar*/
	wxMenuBar *b = new wxMenuBar();
	/*file*/
	wxMenu *menu = new wxMenu();
	menu->Append(FILE_OPEN, wxT("&Open File\tCtrl+O"), wxT("Open local presentation"));
	menu->Append(FILE_OPEN_URL, wxT("&Open URL\tCtrl+U"), wxT("Open remote presentation"));
	menu->AppendSeparator();
	menu->Append(FILE_PROPERTIES, wxT("&Properties\tCtrl+I"), wxT("Show presentation properties"));
	wxMenu *smenu = new wxMenu();
	smenu->Append(ID_MCACHE_ENABLE, wxT("&Enable"), wxT("Turns Recorder On/Off"));
	smenu->Append(ID_MCACHE_STOP, wxT("&Stop"), wxT("Stops recording and saves"));
	smenu->Append(ID_MCACHE_ABORT, wxT("&Abort"), wxT("Stops recording and discards"));
	menu->Append(0, wxT("&Streaming Cache"), smenu);
	menu->Enable(FILE_PROPERTIES, 0);
	menu->AppendSeparator();
	menu->Append(TERM_RELOAD, wxT("Reload &Player"), wxT("Reload MPEG-4 Terminal"));
	menu->AppendSeparator();
	menu->Append(FILE_QUIT, wxT("E&xit"), wxT("Quit the application"));
	b->Append(menu, wxT("&File"));
	/*view*/
	menu = new wxMenu();
	vp_list = new wxMenu();
	menu->Append(0, wxT("&Viewpoint"), vp_list);
	smenu = new wxMenu();
	smenu->Append(ID_HEADLIGHT, wxT("Headlight"), wxT("Turns headlight on/off"), wxITEM_CHECK);
	smenu->AppendSeparator();
	smenu->Append(ID_NAVIGATE_NONE, wxT("None"), wxT("Disables Navigation"), wxITEM_CHECK);
	smenu->Append(ID_NAVIGATE_WALK, wxT("Walk"), wxT("Walk Navigation"), wxITEM_CHECK);
	smenu->Append(ID_NAVIGATE_FLY, wxT("Fly"), wxT("Fly Navigation"), wxITEM_CHECK);
	smenu->Append(ID_NAVIGATE_EXAMINE, wxT("Examine"), wxT("Examine Navigation"), wxITEM_CHECK);
	smenu->Append(ID_NAVIGATE_PAN, wxT("Pan"), wxT("Pan Navigation"), wxITEM_CHECK);
	smenu->Append(ID_NAVIGATE_SLIDE, wxT("Slide"), wxT("Slide Navigation"), wxITEM_CHECK);
	smenu->Append(ID_NAVIGATE_ORBIT, wxT("Orbit"), wxT("Orbit Navigation"), wxITEM_CHECK);
	smenu->Append(ID_NAVIGATE_GAME, wxT("Game"), wxT("Game Navigation"), wxITEM_CHECK);
	smenu->AppendSeparator();
	wxMenu *ssmenu = new wxMenu();
	ssmenu->Append(ID_COLLIDE_NONE, wxT("None"), wxT("No Collision detection"), wxITEM_CHECK);
	ssmenu->Append(ID_COLLIDE_REG, wxT("Regular"), wxT("Regular Collision detection"), wxITEM_CHECK);
	ssmenu->Append(ID_COLLIDE_DISP, wxT("Displacement"), wxT("Collision detecion with camera displacement"), wxITEM_CHECK);
	smenu->Append(0, wxT("&Collision"), ssmenu);
	smenu->Append(ID_GRAVITY, wxT("Gravity"), wxT("Turns gravity on/off"), wxITEM_CHECK);
	smenu->AppendSeparator();
	smenu->Append(ID_NAVIGATE_RESET, wxT("Reset"), wxT("Reset Navigation"));

	menu->Append(0, wxT("&Navigation"), smenu);
	menu->AppendSeparator();
	menu->Append(VIEW_FULLSCREEN, wxT("&Fullscreen"), wxT("Toggles Fullscreen"), wxITEM_CHECK);
	menu->Append(VIEW_ORIGINAL, wxT("&Original Size"), wxT("Restore original size"));
	smenu = new wxMenu();
	smenu->Append(VIEW_AR_KEEP, wxT("Keep Original\tCtrl+1"), wxT("Keep original aspect ratio"), wxITEM_CHECK);
	smenu->Append(VIEW_AR_FILL, wxT("Fill Screen\tCtrl+2"), wxT("Stretch presentation to fill screen"), wxITEM_CHECK);
	smenu->Append(VIEW_AR_43, wxT("Ratio 4/3\tCtrl+3"), wxT("Force aspect ratio to 4/3"), wxITEM_CHECK);
	smenu->Append(VIEW_AR_169, wxT("Ratio 16/9\tCtrl+4"), wxT("Force aspect ratio to 16/9"), wxITEM_CHECK);
	menu->Append(0, wxT("&Aspect Ratio"), smenu);
	smenu->Check(VIEW_AR_KEEP, 1);
	menu->AppendSeparator();
	menu->Append(VIEW_OPTIONS, wxT("&Options"), wxT("View Options"));
	menu->AppendSeparator();
	menu->Append(VIEW_LOGS, wxT("&Logs"), wxT("View GPAC logs"));
	b->Append(menu, wxT("&View"));

	/*play*/
	menu = new wxMenu();
	sel_menu = new wxMenu();
	sel_menu->Append(0, wxT("&Audio"), new wxMenu());
	sel_menu->Append(0, wxT("&Video"), new wxMenu());
	sel_menu->Append(0, wxT("&Subtitles"), new wxMenu());
	sel_menu->AppendSeparator();
	sel_menu->Append(ID_ADD_SUB, wxT("&Add Subtitle"), wxT("Adds subtitle"));
	menu->Append(ID_STREAM_MENU, wxT("&Streams Selection"), sel_menu);
	chap_menu = new wxMenu();
	menu->Append(ID_CHAPTER_MENU, wxT("&Chapters"), chap_menu);

	menu->AppendSeparator();
	menu->Append(VIEW_PLAYLIST, wxT("&Playlist\tCtrl+L"), wxT("Show navigation history as playlist"), wxITEM_CHECK);
	menu->Append(ID_CLEAR_NAV, wxT("&Clear History"), wxT("Clear navigation history"));
	menu->AppendSeparator();
	menu->Append(FILE_PLAY, wxT("&Play/Pause\tCtrl+P"), wxT("Play/Pause/Resume Presentation"));
	menu->Append(FILE_STEP, wxT("&Step-by-Step\tCtrl+S"), wxT("Play/Pause/Resume Presentation"));
	menu->Append(FILE_STOP, wxT("&Stop"), wxT("Stop Presentation"));
	menu->AppendSeparator();
	menu->Append(FILE_RELOAD, wxT("&Reload File\tCtrl+R"), wxT("Reload Presentation"));
	b->Append(menu, wxT("&Play"));

	menu = new wxMenu();
	menu->Append(APP_SHORTCUTS, wxT("&Shortcuts"), wxT("Show keyboard shortcuts"));
	menu->Append(APP_NAV_KEYS, wxT("&Navigation Keys"), wxT("Show navigation keys"));
	menu->AppendSeparator();
	menu->Append(APP_ABOUT, wxT("&About"), wxT("Display information and copyright"));
	b->Append(menu, wxT("&?"));

	SetMenuBar(b);

	m_pStatusbar = CreateStatusBar(1, 0, -1, wxT("statusBar"));
	ws[0] = 60;
	ws[1] = 70;
	ws[2] = -1;
	m_pStatusbar->SetFieldsCount(3, ws);

	SetStatusBarPane(2);
	wxColour foreCol = m_pStatusbar->GetBackgroundColour();


	m_pTimer = new wxTimer();
	m_pTimer->SetOwner(this, ID_CTRL_TIMER);
	m_bGrabbed = 0;

	/*create toolbar*/
	m_pToolBar = CreateToolBar(wxTB_FLAT|wxTB_HORIZONTAL);
	m_pOpenFile = new wxBitmap(tool_open_file);
	m_pPrev = new wxBitmap(tool_prev);
	m_pNext = new wxBitmap(tool_next);
	m_pPlay = new wxBitmap(tool_play);
	m_pPause = new wxBitmap(tool_pause);
	m_pStep = new wxBitmap(tool_step);
	m_pStop = new wxBitmap(tool_stop);
	m_pInfo = new wxBitmap(tool_info);
	m_pConfig = new wxBitmap(tool_config);
	m_pSW2D = new wxBitmap(tool_sw_2d);
	m_pSW3D = new wxBitmap(tool_sw_3d);
	
	m_pToolBar->AddTool(FILE_OPEN, wxT(""), *m_pOpenFile, wxT("Open File"));
	m_pToolBar->AddSeparator();
	m_pPrevBut = new wxMenuButton(m_pToolBar, FILE_PREV, *m_pPrev);
	m_pPrevBut->SetToolTip(wxT("Previous Location"));
	m_pToolBar->AddControl(m_pPrevBut);
	m_pNextBut = new wxMenuButton(m_pToolBar, FILE_NEXT, *m_pNext);
	m_pNextBut->SetToolTip(wxT("Next Location"));
	m_pToolBar->AddControl(m_pNextBut);

	m_pToolBar->AddSeparator();
	m_pToolBar->AddTool(FILE_PLAY, wxT(""), *m_pPlay, wxT("Play/Pause File"));
	m_pToolBar->AddTool(FILE_STEP, wxT(""), *m_pStep, wxT("Step-by-Step Mode"));
	m_pToolBar->AddTool(FILE_STOP, wxT(""), *m_pStop, wxT("Stop File"));
	m_pToolBar->AddSeparator();
	m_pToolBar->AddTool(FILE_PROPERTIES, wxT(""), *m_pInfo, wxT("Show File Information"));
	m_pToolBar->AddSeparator();
	m_pToolBar->AddTool(VIEW_OPTIONS, wxT(""), *m_pConfig, wxT("GPAC Configuration"));
	m_pToolBar->AddTool(SWITCH_RENDER, wxT(""), *m_pSW3D, wxT("Switch 2D/3D Renderers"));

	m_pToolBar->Realize();

	m_Address = new wxMyComboBox(this, ID_ADDRESS, wxT(""), wxPoint(50, 0), wxSize(260, 20));
	wxStaticText *add_text = new wxStaticText(this, 0, wxT("Location"));
	add_text->SetBackgroundColour(foreCol);

	m_pAddBar = new wxBoxSizer(wxHORIZONTAL);
	m_pAddBar->Add(add_text, 0, wxALIGN_BOTTOM|wxADJUST_MINSIZE|wxEXPAND, 0);
	m_pAddBar->Add(m_Address, 2, wxALIGN_CENTER|wxEXPAND);

	
#ifdef __WXGTK__
	m_pProg = new wxScrollBar(this, ID_SLIDER, wxPoint(0, 20), wxSize(320, 20), wxSB_HORIZONTAL | wxNO_BORDER);
	m_pProg->SetScrollbar(0, 1, 1000, 1);
#else
	m_pProg = new wxSlider(this, ID_SLIDER, 0, 0, 1000, wxPoint(0, 22), wxSize(320, 22), wxSL_HORIZONTAL|wxSUNKEN_BORDER);
#endif
	m_pProg->Enable(0);
	m_pProg->Show();
	m_pProg->SetBackgroundColour(foreCol);

	m_pView = new wxWindow(this, -1, wxDefaultPosition, wxDefaultSize, wxNO_BORDER);
	m_pView->SetBackgroundColour(wxColour(wxT("BLACK")));

	wxBoxSizer *bs;
	m_pSizer = new wxBoxSizer(wxVERTICAL);
	/*on WIN32 platforms the toolbar is part of the frame, not of the client area*/
#if 0
	bs = new wxBoxSizer(wxHORIZONTAL);
	bs->Add(m_pToolBar, 1, wxALIGN_CENTER|wxADJUST_MINSIZE|wxEXPAND);
	m_pSizer->Add(bs, 0, wxALL, 0);
#endif

	m_pSizer->Add(m_pAddBar, 0, wxALL|wxADJUST_MINSIZE|wxEXPAND, 0);

	m_pViewSizer = new wxBoxSizer(wxHORIZONTAL);
	m_pViewSizer->Add(m_pView, 1, wxALIGN_CENTER|wxADJUST_MINSIZE|wxEXPAND, 0);
	m_pSizer->Add(m_pViewSizer, 1, wxEXPAND|wxADJUST_MINSIZE|wxEXPAND, 0);

	bs = new wxBoxSizer(wxHORIZONTAL);
	bs->Add(m_pProg, 1, wxALL|wxALIGN_CENTER|wxADJUST_MINSIZE|wxEXPAND, 0);
	m_pSizer->Add(bs, 0, wxALL|wxADJUST_MINSIZE|wxEXPAND, 0);

	m_pPlayList = new wxPlaylist(this);
	m_pPlayList->SetIcon(wxIcon(osmo4));
	m_pPlayList->Hide();
	Raise();
	Show();
	m_connected = 0;
	if (!LoadTerminal()) {
		Close(TRUE);
		return;
	}

	SetSizer(m_pSizer);
	if (!m_bExternalView) {
//		m_pSizer->Show(m_pViewSizer, 1);
	} else {
//		m_pSizer->Show(m_pViewSizer, 0);
		SetWindowStyle(wxDEFAULT_FRAME_STYLE & ~(wxMAXIMIZE_BOX | wxRESIZE_BORDER));
	}
	DoLayout(320, 240);

	UpdateRenderSwitch();

	const char *sOpt = gf_cfg_get_key(m_user.config, "General", "ConsoleOff");
	m_console_off = (sOpt && !stricmp(sOpt, "yes")) ? 1 : 0;
	sOpt = gf_cfg_get_key(m_user.config, "General", "Loop");
	m_loop = (sOpt && !stricmp(sOpt, "yes")) ? 1 : 0;
	sOpt = gf_cfg_get_key(m_user.config, "General", "LookForSubtitles");
	m_lookforsubs = (sOpt && !stricmp(sOpt, "yes")) ? 1 : 0;
	gf_term_set_option(m_term, GF_OPT_AUDIO_VOLUME, 100);
	
	ReloadURLs();
	Raise();
	m_pStatusbar->SetStatusText(wxT("Ready"), 2);
	m_LastStatusTime = 0;

	m_pPrevBut->Refresh();
	m_pNextBut->Refresh();

	wxOsmo4App &app = wxGetApp();
	if (app.argc>1) {
		m_pPlayList->QueueURL(wxString(app.argv[1]));
		m_pPlayList->RefreshList();
		m_pPlayList->PlayNext();
	} else {
		char sPL[GF_MAX_PATH];
		strcpy((char *) sPL, szAppPath);
#ifdef WIN32
		strcat(sPL, "gpac_pl.m3u");
#else
		strcat(sPL, ".gpac_pl.m3u");
#endif
		m_pPlayList->OpenPlaylist(wxString(sPL, wxConvUTF8) );
		const char *sOpt = gf_cfg_get_key(m_user.config, "General", "PLEntry");
		if (sOpt) {
			m_pPlayList->m_cur_entry = atoi(sOpt);
			if (m_pPlayList->m_cur_entry>=(s32)gf_list_count(m_pPlayList->m_entries))
				m_pPlayList->m_cur_entry = -1;
		}
	}
}

wxOsmo4Frame::~wxOsmo4Frame()
{
	vp_list = NULL;
	sel_menu = NULL;
	//m_pToolBar->RemoveTool(FILE_PREV);
	//m_pToolBar->RemoveTool(FILE_NEXT);

	if (m_chapters_start) free(m_chapters_start);

	if (m_user.modules) gf_modules_del(m_user.modules);
	if (m_user.config) gf_cfg_del(m_user.config);

//	if (m_pView) delete m_pView;

	delete m_pPrevBut;
	delete m_pNextBut;
	delete m_pPlayList;
	delete m_pTimer;
	delete m_pOpenFile;
	delete m_pPrev;
	delete m_pNext;
	delete m_pPlay;
	delete m_pPause;
	delete m_pStep;
	delete m_pStop;
	delete m_pInfo;
	delete m_pConfig;
	delete m_pSW2D;
	delete m_pSW3D;
}


BEGIN_EVENT_TABLE(wxOsmo4Frame, wxFrame)
	EVT_CLOSE(wxOsmo4Frame::OnCloseApp)
	EVT_MENU(FILE_OPEN, wxOsmo4Frame::OnFileOpen)
	EVT_MENU(FILE_OPEN_URL, wxOsmo4Frame::OnFileOpenURL)
	EVT_MENU(FILE_RELOAD, wxOsmo4Frame::OnFileReload)
	EVT_MENU(FILE_PROPERTIES, wxOsmo4Frame::OnFileProperties)
	EVT_MENU(TERM_RELOAD, wxOsmo4Frame::OnTermReload)
	EVT_MENU(FILE_QUIT, wxOsmo4Frame::OnFileQuit)
	EVT_MENU(VIEW_FULLSCREEN, wxOsmo4Frame::OnFullScreen)
	EVT_MENU(VIEW_OPTIONS, wxOsmo4Frame::OnOptions)
	EVT_MENU(VIEW_AR_KEEP, wxOsmo4Frame::OnViewARKeep)
	EVT_MENU(VIEW_AR_FILL, wxOsmo4Frame::OnViewARFill)
	EVT_MENU(VIEW_AR_169, wxOsmo4Frame::OnViewAR169)
	EVT_MENU(VIEW_AR_43, wxOsmo4Frame::OnViewAR43)
	EVT_MENU(VIEW_ORIGINAL, wxOsmo4Frame::OnViewOriginal)
	EVT_MENU(VIEW_PLAYLIST, wxOsmo4Frame::OnPlaylist)
	EVT_UPDATE_UI(VIEW_PLAYLIST, wxOsmo4Frame::OnUpdatePlayList)

	EVT_MENU(ID_CLEAR_NAV, wxOsmo4Frame::OnClearNav)
	EVT_UPDATE_UI(ID_STREAM_MENU, wxOsmo4Frame::OnUpdateStreamMenu)
	EVT_UPDATE_UI(ID_CHAPTER_MENU, wxOsmo4Frame::OnUpdateChapterMenu)
	EVT_MENU(ID_ADD_SUB, wxOsmo4Frame::OnAddSub)

	EVT_MENU(ID_MCACHE_ENABLE, wxOsmo4Frame::OnCacheEnable)
	EVT_UPDATE_UI(ID_MCACHE_ENABLE, wxOsmo4Frame::OnUpdateCacheEnable)
	EVT_MENU(ID_MCACHE_STOP, wxOsmo4Frame::OnCacheStop)
	EVT_MENU(ID_MCACHE_ABORT, wxOsmo4Frame::OnCacheAbort)
	EVT_UPDATE_UI(ID_MCACHE_STOP, wxOsmo4Frame::OnUpdateCacheAbort)
	EVT_UPDATE_UI(ID_MCACHE_ABORT, wxOsmo4Frame::OnUpdateCacheAbort)
	

	EVT_MENU(APP_SHORTCUTS, wxOsmo4Frame::OnShortcuts)
	EVT_MENU(APP_NAV_KEYS, wxOsmo4Frame::OnNavInfo)
	EVT_MENU(APP_ABOUT, wxOsmo4Frame::OnAbout)
	EVT_GPACEVENT(wxOsmo4Frame::OnGPACEvent)
	EVT_TIMER(ID_CTRL_TIMER, wxOsmo4Frame::OnTimer)
	EVT_COMMAND_SCROLL(ID_SLIDER, wxOsmo4Frame::OnSlide)
	EVT_MENU(VIEW_LOGS, wxOsmo4Frame::OnLogs)

	EVT_MENUBUTTON_OPEN(FILE_PREV, wxOsmo4Frame::OnFilePrevOpen)
	EVT_MENUBUTTON_OPEN(FILE_NEXT, wxOsmo4Frame::OnFileNextOpen)
    EVT_MENU(FILE_PREV, wxOsmo4Frame::OnNavPrev)
	EVT_UPDATE_UI(FILE_PREV, wxOsmo4Frame::OnUpdateNavPrev)
    EVT_MENU_RANGE(ID_NAV_PREV_0, ID_NAV_PREV_9, wxOsmo4Frame::OnNavPrevMenu)
    EVT_MENU(FILE_NEXT, wxOsmo4Frame::OnNavNext)
	EVT_UPDATE_UI(FILE_NEXT, wxOsmo4Frame::OnUpdateNavNext)
    EVT_MENU_RANGE(ID_NAV_NEXT_0, ID_NAV_NEXT_9, wxOsmo4Frame::OnNavNextMenu)

	EVT_TOOL(FILE_PLAY, wxOsmo4Frame::OnFilePlay)
	EVT_TOOL(FILE_STEP, wxOsmo4Frame::OnFileStep)
    EVT_TOOL(FILE_STOP, wxOsmo4Frame::OnFileStop)
    EVT_TOOL(SWITCH_RENDER, wxOsmo4Frame::OnRenderSwitch)

	EVT_COMBOBOX(ID_ADDRESS, wxOsmo4Frame::OnURLSelect)

    EVT_MENU_RANGE(ID_SELSTREAM_0, ID_SELSTREAM_9, wxOsmo4Frame::OnStreamSel)
	EVT_UPDATE_UI_RANGE(ID_SELSTREAM_0, ID_SELSTREAM_9, wxOsmo4Frame::OnUpdateStreamSel)  

    EVT_MENU_RANGE(ID_SETCHAP_FIRST, ID_SETCHAP_LAST, wxOsmo4Frame::OnChapterSel)
	EVT_UPDATE_UI_RANGE(ID_SETCHAP_FIRST, ID_SETCHAP_LAST, wxOsmo4Frame::OnUpdateChapterSel)  

    EVT_MENU_RANGE(ID_VIEWPOINT_FIRST, ID_VIEWPOINT_LAST, wxOsmo4Frame::OnViewport)
	EVT_UPDATE_UI_RANGE(ID_VIEWPOINT_FIRST, ID_VIEWPOINT_LAST, wxOsmo4Frame::OnUpdateViewport)  

    EVT_MENU_RANGE(ID_NAVIGATE_NONE, ID_NAVIGATE_GAME, wxOsmo4Frame::OnNavigate)
	EVT_UPDATE_UI_RANGE(ID_NAVIGATE_NONE, ID_NAVIGATE_GAME, wxOsmo4Frame::OnUpdateNavigation)  
	EVT_MENU(ID_NAVIGATE_RESET, wxOsmo4Frame::OnNavigateReset)

    EVT_MENU_RANGE(ID_COLLIDE_NONE, ID_COLLIDE_DISP, wxOsmo4Frame::OnCollide)
	EVT_UPDATE_UI_RANGE(ID_COLLIDE_NONE, ID_COLLIDE_DISP, wxOsmo4Frame::OnUpdateCollide)  

	EVT_MENU(ID_HEADLIGHT, wxOsmo4Frame::OnHeadlight)
	EVT_UPDATE_UI(ID_HEADLIGHT, wxOsmo4Frame::OnUpdateHeadlight)  
	EVT_MENU(ID_GRAVITY, wxOsmo4Frame::OnGravity)
	EVT_UPDATE_UI(ID_GRAVITY, wxOsmo4Frame::OnUpdateGravity)  

	EVT_UPDATE_UI(FILE_PROPERTIES, wxOsmo4Frame::OnUpdateNeedsConnect)
	EVT_UPDATE_UI(FILE_RELOAD, wxOsmo4Frame::OnUpdateNeedsConnect)  
	EVT_UPDATE_UI(FILE_PLAY, wxOsmo4Frame::OnUpdatePlay)  
	EVT_UPDATE_UI(FILE_STOP, wxOsmo4Frame::OnUpdateNeedsConnect)  
	EVT_UPDATE_UI(FILE_STEP, wxOsmo4Frame::OnUpdateNeedsConnect)  
	EVT_UPDATE_UI(VIEW_ORIGINAL, wxOsmo4Frame::OnUpdateNeedsConnect)  
	EVT_UPDATE_UI(VIEW_FULLSCREEN, wxOsmo4Frame::OnUpdateFullScreen)  
	EVT_UPDATE_UI(VIEW_AR_KEEP, wxOsmo4Frame::OnUpdateAR)
	EVT_UPDATE_UI(VIEW_AR_FILL, wxOsmo4Frame::OnUpdateAR)
	EVT_UPDATE_UI(VIEW_AR_169, wxOsmo4Frame::OnUpdateAR)
	EVT_UPDATE_UI(VIEW_AR_43, wxOsmo4Frame::OnUpdateAR)

	EVT_SIZE(wxOsmo4Frame::OnSize) 
END_EVENT_TABLE()

void wxOsmo4Frame::DoLayout(u32 v_width, u32 v_height)
{
	wxPoint pos;
	if (!m_Address || !m_pProg) return;

	if (v_width && v_height) {
		m_orig_width = v_width;
		m_orig_height = v_height;
		if (!m_bExternalView) m_pView->SetSize(wxSize(v_width, v_height));
		/*this will call setSize*/
		m_pSizer->Fit(this);
		return;
	}
	Layout();
	if (!m_bExternalView) {
		wxSize s = GetClientSize();
		s.y -= m_pAddBar->GetSize().y;
		s.y -= m_pProg->GetSize().y;
		if (!m_bExternalView) {
			m_pView->SetSize(s.x, s.y);
			if (m_term) gf_term_set_size(m_term, s.x, s.y);
		}
	}
}

void wxOsmo4Frame::OnSize(wxSizeEvent &event)
{
	SetSize(event.m_size.x, event.m_size.y);
	DoLayout();
}

void wxOsmo4Frame::OnCloseApp(wxCloseEvent &WXUNUSED(event))
{
	if (m_term) gf_term_del(m_term);
	m_term = NULL;
	Destroy();
}


wxString wxOsmo4Frame::GetFileFilter()
{
	u32 keyCount, i;
	wxString sFiles, sSupportedFiles, sExts;

	/*force MP4 and 3GP files at beginning to make sure they are selected (Win32 bug with too large filters)*/
	sSupportedFiles = wxT("All Known Files|*.m3u;*.pls;*.mp4;*.3gp;*.3g2");
	sExts = wxT("");
	sFiles = wxT("");
	keyCount = gf_cfg_get_key_count(m_user.config, "MimeTypes");
	for (i=0; i<keyCount; i++) {
		Bool first = 1;
		const char *sMime;
		char *sKey;
		const char *opt;
		char szKeyList[1000], sDesc[1000];
		sMime = gf_cfg_get_key_name(m_user.config, "MimeTypes", i);
		if (!sMime) continue;
		opt = gf_cfg_get_key(m_user.config, "MimeTypes", sMime);
		/*remove module name*/
		strcpy(szKeyList, opt+1);
		sKey = strrchr(szKeyList, '\"');
		if (!sKey) continue;
		sKey[0] = 0;
		/*get description*/
		sKey = strrchr(szKeyList, '\"');
		if (!sKey) continue;
		strcpy(sDesc, sKey+1);
		sKey[0] = 0;
		sKey = strrchr(szKeyList, '\"');
		if (!sKey) continue;
		sKey[0] = 0;

		/*if same description for # mime types skip (means an old mime syntax)*/
		if (sFiles.Find(wxString(sDesc, wxConvUTF8) )>=0) continue;
		/*if same extensions for # mime types skip (don't polluate the file list)*/
		if (sExts.Find(wxString(szKeyList, wxConvUTF8) )>=0) continue;
	
		sExts += wxString(szKeyList, wxConvUTF8);
		sExts += wxT(" ");
		sFiles += wxString(sDesc, wxConvUTF8);
		sFiles += wxT("|");

		wxString sOpt = wxString(szKeyList, wxConvUTF8);
		while (1) {
			wxString ext = sOpt.BeforeFirst(' ');
			if (ext.Find('.')<0) {
				if (first) first = 0;
				else sFiles += wxT(";");
				sFiles += wxT("*.");
				sFiles += ext;
				wxString sext = ext;
				sext += wxT(";");
				if (sSupportedFiles.Find(sext)<0) {
					sSupportedFiles  += wxT(";*.");
					sSupportedFiles += ext;
				}
			}
			if (sOpt==ext) break;
			wxString rem = ext + wxT(" ");
			sOpt.Replace(rem, wxT(""), TRUE);
		}
		sFiles += wxT("|");
	}
	sSupportedFiles += wxT("|");
	sSupportedFiles += sFiles;
	sSupportedFiles += wxT("M3U Playlists|*.m3u|ShoutCast Playlists|*.pls|All Files|*.*||");
	return sSupportedFiles;
}

void wxOsmo4Frame::OnFileOpen(wxCommandEvent & WXUNUSED(event))
{
	wxFileDialog dlg(this, wxT("Select file(s)"), wxT(""), wxT(""), GetFileFilter(), wxOPEN | wxMULTIPLE | wxCHANGE_DIR | wxHIDE_READONLY);

	if (dlg.ShowModal() != wxID_OK) return;

	wxArrayString stra;
	dlg.GetPaths(stra);
	if (stra.GetCount() == 1) {
		m_pPlayList->Truncate();
	} else {
		m_pPlayList->Clear();
	}
	for (u32 i=0; i<stra.GetCount(); i++) 
		m_pPlayList->QueueURL(stra[i]);

	m_pPlayList->RefreshList();
	m_pPlayList->PlayNext();
}

void wxOsmo4Frame::OnFileOpenURL(wxCommandEvent & WXUNUSED(event))
{
	OpenURLDlg dlg(this, m_user.config);
	if (dlg.ShowModal()==wxID_OK) {
		m_pPlayList->Truncate();
		m_pPlayList->QueueURL(dlg.m_urlVal);
		m_pPlayList->RefreshList();
		m_pPlayList->PlayNext();
	}
}

void wxOsmo4Frame::OnFileProperties(wxCommandEvent & WXUNUSED(event))
{
	wxFileProps dlg(this);
	dlg.SetIcon(wxIcon(osmo4));
	dlg.ShowModal();
}

void wxOsmo4Frame::OnFileReload(wxCommandEvent & WXUNUSED(event))
{
	gf_term_disconnect(m_term); 
	m_connected = 0;
	DoConnect();
}

void wxOsmo4Frame::OnFileQuit(wxCommandEvent & WXUNUSED(event))
{
	Close(FALSE);
}

void wxOsmo4Frame::OnViewOriginal(wxCommandEvent & WXUNUSED(event))
{
	if (m_pView) {
		DoLayout(m_orig_width, m_orig_height);
	} else {
		gf_term_set_option(m_term, GF_OPT_ORIGINAL_VIEW, 1);
	}
}

void wxOsmo4Frame::OnOptions(wxCommandEvent & WXUNUSED(event))
{
	wxGPACControl dlg(this);
	dlg.SetIcon(wxIcon(osmo4));
	dlg.ShowModal();
}

void wxOsmo4Frame::DoConnect()
{
	//if (m_connected) { gf_term_disconnect(m_term); m_connected = 0; }

	wxString url = m_pPlayList->GetURL();
	m_Address->SetValue(url);
	m_pView->SetFocus();

	wxString txt = wxT("Osmo4 - ");
	txt += m_pPlayList->GetDisplayName();
	SetTitle(txt);
	gf_term_connect(m_term, url.mb_str(wxConvUTF8));
}

void wxOsmo4Frame::OnLogs(wxCommandEvent & WXUNUSED(event))
{
	m_pLogs->Show();
}

void wxOsmo4Frame::OnUpdateNeedsConnect(wxUpdateUIEvent &event)
{
	event.Enable(m_connected ? 1 : 0);
}

void wxOsmo4Frame::OnUpdatePlay(wxUpdateUIEvent &event)
{
	event.Enable( (m_connected || m_pPlayList->HasValidEntries()) ? 1 : 0);
}

void wxOsmo4Frame::OnUpdateFullScreen(wxUpdateUIEvent &event)
{
	if (m_connected) {
		event.Enable(1);
		event.Check(gf_term_get_option(m_term, GF_OPT_FULLSCREEN) ? 1 : 0);
	} else {
		event.Enable(0);
	}
}

void wxOsmo4Frame::OnFullScreen(wxCommandEvent & WXUNUSED(event))
{
	Bool isFS = gf_term_get_option(m_term, GF_OPT_FULLSCREEN) ? 1 : 0;
	gf_term_set_option(m_term, GF_OPT_FULLSCREEN, isFS ? 0 : 1);
}

void wxOsmo4Frame::OnViewARKeep(wxCommandEvent & WXUNUSED(event))
{
	gf_term_set_option(m_term, GF_OPT_ASPECT_RATIO, GF_ASPECT_RATIO_KEEP);
}
void wxOsmo4Frame::OnViewARFill(wxCommandEvent & WXUNUSED(event))
{
	gf_term_set_option(m_term, GF_OPT_ASPECT_RATIO, GF_ASPECT_RATIO_FILL_SCREEN);
}
void wxOsmo4Frame::OnViewAR169(wxCommandEvent & WXUNUSED(event))
{
	gf_term_set_option(m_term, GF_OPT_ASPECT_RATIO, GF_ASPECT_RATIO_16_9);
}
void wxOsmo4Frame::OnViewAR43(wxCommandEvent & WXUNUSED(event))
{
	gf_term_set_option(m_term, GF_OPT_ASPECT_RATIO, GF_ASPECT_RATIO_4_3);
}

void wxOsmo4Frame::OnUpdateAR(wxUpdateUIEvent &event)
{
	if (!m_connected) {
		event.Enable(0);
		return;
	}
	event.Enable(1);
	u32 val = gf_term_get_option(m_term, GF_OPT_ASPECT_RATIO);
	if ((event.GetId() == VIEW_AR_FILL) && (val==GF_ASPECT_RATIO_FILL_SCREEN))
		event.Check(1);
	else if ((event.GetId() == VIEW_AR_KEEP) && (val==GF_ASPECT_RATIO_KEEP)) 
		event.Check(1);
	else if ((event.GetId() == VIEW_AR_169) && (val==GF_ASPECT_RATIO_16_9)) 
		event.Check(1);
	else if ((event.GetId() == VIEW_AR_43) && (val==GF_ASPECT_RATIO_4_3)) 
		event.Check(1);
	else event.Check(0);
}

void wxOsmo4Frame::OnShortcuts(wxCommandEvent & WXUNUSED(event))
{
	wxMessageDialog dlg(this,
		wxT("Shortcuts with focus on main frame:\n")
		wxT("Open File: Ctrl + O\n")
		wxT("Show File Information: Ctrl + I\n")
		wxT("Reload File: Ctrl + R\n")
		wxT("Pause/Resume File: Ctrl + P\n")
		wxT("Step by Step: Ctrl + S\n")
		wxT("Fullscreen On/Off: Alt + Return\n")
		wxT("View Playlist: Ctrl + L\n")
		wxT("Aspect Ratio Normal: Ctrl + 1\n")
		wxT("Aspect Ratio Fill: Ctrl + 2\n")
		wxT("Aspect Ratio 4/3: Ctrl + 3\n")
		wxT("Aspect Ratio 16/9: Ctrl + 4\n")
		wxT("\n")
		wxT("Shortcuts with focus on video frame:\n")
		wxT("Seek +5% into presentation: Alt + right arrow\n")
		wxT("Seek -5% into presentation: Alt + left arrow\n")		
		wxT("Seek +1min into presentation: Alt + up arrow\n")
		wxT("Seek -1min into presentation: Alt + down arrow\n")
		wxT("Next Playlist Entry: Ctrl + right arrow\n")
		wxT("Prev Playlist Entry: Ctrl + left arrow\n")
		
		, wxT("Shortcuts Available on Osmo4")
		, wxOK);

	dlg.ShowModal();
}

void wxOsmo4Frame::OnNavInfo(wxCommandEvent & WXUNUSED(event))
{
	wxMessageDialog dlg(this,
		wxT("* Walk & Fly modes:\n")
		wxT("\tH move: H pan - V move: Z-translate - V move+CTRL or Wheel: V pan - Right Click (Walk only): Jump\n")
		wxT("\tleft/right: H pan - left/right+CTRL: H translate - up/down: Z-translate - up/down+CTRL: V pan\n")
		wxT("* Pan mode:\n")
		wxT("\tH move: H pan - V move: V pan - V move+CTRL or Wheel: Z-translate\n")
		wxT("\tleft/right: H pan - left/right+CTRL: H translate - up/down: V pan - up/down+CTRL: Z-translate\n")
		wxT("* Slide mode:\n")
		wxT("\tH move: H translate - V move: V translate - V move+CTRL or Wheel: Z-translate\n")
		wxT("\tleft/right: H translate - left/right+CTRL: H pan - up/down: V translate - up/down+CTRL: Z-translate\n")
		wxT("* Examine & Orbit mode:\n")
		wxT("\tH move: Y-Axis rotate - H move+CTRL: No move - V move: X-Axis rotate - V move+CTRL or Wheel: Z-translate\n")
		wxT("\tleft/right: Y-Axis rotate - left/right+CTRL: H translate - up/down: X-Axis rotate - up/down+CTRL: Y-translate\n")
		wxT("* VR mode:\n")
		wxT("\tH move: H pan - V move: V pan - V move+CTRL or Wheel: Camera Zoom\n")
		wxT("\tleft/right: H pan - up/down: V pan - up/down+CTRL: Camera Zoom\n")
		wxT("* Game mode (press END to escape):\n")
		wxT("\tH move: H pan - V move: V pan\n")
		wxT("\tleft/right: H translate - up/down: Z-translate\n")
		wxT("\n")
		wxT("* All 3D modes: CTRL+PGUP/PGDOWN will zoom in/out camera (field of view) \n")
		wxT("\n")
		wxT("*Slide Mode in 2D:\n")
		wxT("\tH move: H translate - V move: V translate - V move+CTRL: zoom\n")
		wxT("\tleft/right: H translate - up/down: V translate - up/down+CTRL: zoom\n")
		wxT("*Examine Mode in 2D (3D renderer only):\n")
		wxT("\tH move: Y-Axis rotate - V move: X-Axis rotate\n")
		wxT("\tleft/right: Y-Axis rotate - up/down: X-Axis rotate\n")
		wxT("\n")
		wxT("HOME: reset navigation to last viewpoint (2D or 3D navigation)\n")
		wxT("SHIFT key in all modes: fast movement\n")

		, wxT("3D navigation keys (\'H\'orizontal and \'V\'ertical) used in GPAC")
		, wxOK );
	dlg.ShowModal();
}


/*open file dlg*/
class AboutDlg : public wxDialog {
public:
    AboutDlg(wxWindow *parent);

private:
    wxStaticText *m_info;
    wxButton *m_close;
	void OnClose(wxCommandEvent& event);
    DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(AboutDlg, wxDialog)
    EVT_BUTTON(ID_ABOUT_CLOSE, AboutDlg::OnClose)
END_EVENT_TABLE()

AboutDlg::AboutDlg(wxWindow *parent)
             : wxDialog(parent, -1, wxString(wxT("GPAC/Osmo4 V ")wxT(GPAC_VERSION)))
{
	SetSize(220, 320);
	Centre();
	wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

	m_info = new wxStaticText(this, -1, wxT("http://gpac.sourceforge.net"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE);
	sizer->Add(m_info, 1, wxEXPAND|wxADJUST_MINSIZE, 0);
    m_close = new wxButton(this, ID_ABOUT_CLOSE, wxT("Close"), wxDefaultPosition, wxSize(120, 20));
	sizer->Add(m_close, 0, wxEXPAND, 0);

	SetIcon(wxIcon(osmo4));
	m_info->SetLabel(
		wxT("Osmo4 Player\n")
		wxT("GPAC Multimedia Framework\n")
		wxT("\n")
		wxT("This program is free software and may\n")
		wxT("be distributed according to the terms\n")
		wxT("of the GNU Lesser General Public License\n")
		wxT("\n")
		wxT("Copyright (c) Jean Le Feuvre 2000-2005\n")
		wxT("(c) ENST 2005-200X\n")
		wxT("All Rights Reserved\n")
		wxT("http://gpac.sourceforge.net\n")
		wxT("\n")
		wxT(" ** With Many Thanks To ** \n\n")
		wxT("Mozilla SpiderMonkey (JavaScript)\n")
		wxT("The FreeType Project\n")
		wxT("The PNG Group, The I.J.G.\n")
		wxT("FFMPEG, FAAD, XVID, MAD\n")
		);

	SetSizer(sizer);
	sizer->Fit(this);
}
void AboutDlg::OnClose(wxCommandEvent& WXUNUSED(event))
{
	Close(FALSE);
}

void wxOsmo4Frame::OnAbout(wxCommandEvent & WXUNUSED(event))
{
	AboutDlg dlg(this);
	dlg.ShowModal();
}

Bool is_supported_file(GF_Config *cfg, const char *fileName, Bool disable_no_ext)
{
	char szExt[20], *ext, mimes[1000];
	u32 keyCount, i;

	ext = strrchr(fileName, '/');
	if (ext) {
		ext = strchr(fileName, '.');
		/*fixme - a proper browser should check mime & co here*/
		if (!ext) return strstr(fileName, "http://") ? 0 : 1;
	}

	ext = strrchr(fileName, '.');
	/*this may be anything so let's try*/
	if (!ext) return !disable_no_ext;

	strcpy(szExt, ext+1);
	ext =strrchr(szExt, '#');
	if (ext) ext[0] = 0;
	strlwr(szExt);

	keyCount = gf_cfg_get_key_count(cfg, "MimeTypes");
	for (i=0; i<keyCount; i++) {
		char *sKey;
		const char *opt;
		sKey = (char *) gf_cfg_get_key_name(cfg, "MimeTypes", i);
		if (!sKey) continue;
		opt = gf_cfg_get_key(cfg, "MimeTypes", sKey);
		strcpy(mimes, opt+1);
		sKey = strchr(mimes, '"');
		if (!sKey) continue;
		sKey[0] = 0;
		sKey = mimes;
		while (sKey) {
			if (!strnicmp(sKey, szExt, strlen(szExt))) return 1;
			sKey = strchr(sKey, ' ');
			if (sKey) sKey+=1;
		}
	}
	return 0;
}

void wxOsmo4Frame::OnGPACEvent(wxGPACEvent &event)
{
	wxString cmd;
	wxCommandEvent evt;

	switch (event.gpac_evt.type) {
	case GF_EVT_NAVIGATE:
		if (is_supported_file(m_user.config, event.to_url.mb_str(wxConvUTF8), 0)) {
			char *str = gf_url_concatenate(m_pPlayList->GetURL().mb_str(wxConvUTF8), event.to_url.mb_str(wxConvUTF8));
			if (str) {
				m_pPlayList->Truncate();
				m_pPlayList->QueueURL(wxString(str, wxConvUTF8));
				m_pPlayList->RefreshList();
				free(str);
				m_pPlayList->PlayNext();
			}
			return;
		} 
		cmd = get_pref_browser(m_user.config);
		cmd += wxT(" ");
		cmd += event.to_url;
		wxExecute(cmd);
		break;
	case GF_EVT_QUIT:
		Close(TRUE);
		break;
	case GF_EVT_CONNECT:
		BuildStreamList(0);
		ConnectAcknowledged(event.gpac_evt.connect.is_connected);
		break;
	case GF_EVT_KEYDOWN:
		if (!(event.gpac_evt.key.key_states & GF_KM_CTRL)) return;
		switch (event.gpac_evt.key.virtual_code) {
		case 'R':
		case 'r':
			gf_term_set_option(m_term, GF_OPT_FORCE_REDRAW, 1);
			break;
		case 'P':
		case 'p':
			OnFilePlay(evt);
			break;
		case 's':
		case 'S':
			OnFileStep(evt);
			break;
		}
		break;
	case GF_EVT_SCENESIZE:
		DoLayout(event.gpac_evt.size.width, event.gpac_evt.size.height);
		break;
	case GF_EVT_VIEWPOINTS:
		BuildViewList();
		break;
	case GF_EVT_STREAMLIST:
		BuildStreamList(0);
		break;
	}
}


static wxString format_time(u32 duration, u32 timescale)
{
	u32 h, m, s;
	Float time = duration;
	time /= timescale;
	time *= 1000;
	h = (u32) (time / 1000 / 3600);
	m = (u32) (time / 1000 / 60 - h*60);
	s = (u32) (time / 1000 - h*3600 - m*60);
	return wxString::Format(wxT("%02d:%02d:%02d"), h, m, s);
}

void wxOsmo4Frame::SetStatus(wxString str)
{
	m_pStatusbar->SetStatusText(str, 2); 
	m_LastStatusTime = gf_sys_clock();
}

void wxOsmo4Frame::OnTimer(wxTimerEvent& WXUNUSED(event))
{
	wxString str;
	u32 now;
	if (m_LastStatusTime) {
		now = gf_sys_clock();
		if (now > 1000+m_LastStatusTime) {
			m_LastStatusTime = 0;
			m_pStatusbar->SetStatusText(wxT("Ready"), 2); 
		}
	}

	now = gf_term_get_time_in_ms(m_term);
	if (!now) return;

	if (!m_duration) {
		str = format_time(now, 1000);
		m_pStatusbar->SetStatusText(str);
		str = wxString::Format(wxT("FPS %.2f"), gf_term_get_framerate(m_term, 0));
		m_pStatusbar->SetStatusText(str, 1);
		return;
	}
	if (!m_bGrabbed) {
		if ((now >= m_duration + 500) && gf_term_get_option(m_term, GF_OPT_IS_FINISHED)) {
			m_pPlayList->PlayNext();
/*
			if (m_loop) {
				gf_term_play_from_time(m_term, 0);
			} else if (m_stop_at_end && !m_paused) {
				Stop();
			}
*/
		} else {
			Double val = now * 1000;
			val /= m_duration;
			m_pProg->SetValue((val<=1000) ? (u32) val : 1000);

			if (0) {
				str = format_time(m_duration-now, 1000);
			} else {
				str = format_time(now, 1000);
			}
			m_pStatusbar->SetStatusText(str);
			str = wxString::Format(wxT("FPS %.2f"), gf_term_get_framerate(m_term, 0));
			m_pStatusbar->SetStatusText(str, 1);
		}
	}
}

void wxOsmo4Frame::ConnectAcknowledged(Bool bOk) 
{
	if (bOk) {
		m_pTimer->Start(100, 0);
		m_connected = 1;
		m_bToReset = 0;
		UpdatePlay();
		BuildChapterList(0);
	} else {
		BuildChapterList(1);
		if (!m_connected) {
			UpdatePlay();
			m_pTimer->Stop();
			//m_pProg->Enable(0);
		}
	}
}

void wxOsmo4Frame::OnFilePlay(wxCommandEvent & WXUNUSED(event))
{
	wxCommandEvent evt;
	if (m_connected) {
		if (gf_term_get_option(m_term, GF_OPT_PLAY_STATE)==GF_STATE_PAUSED) {
			gf_term_set_option(m_term, GF_OPT_PLAY_STATE, GF_STATE_PLAYING);
			if (m_bToReset) {
				m_pTimer->Start(100, 0);
				gf_term_play_from_time(m_term, 0);
			}
			m_bToReset = 0;
			UpdatePlay();
		} else {
			gf_term_set_option(m_term, GF_OPT_PLAY_STATE, GF_STATE_PAUSED);
			UpdatePlay();
		}
	} else {
		m_pPlayList->Play();
	}
}

void wxOsmo4Frame::OnFileStep(wxCommandEvent & WXUNUSED(event))
{
	wxCommandEvent evt;
	gf_term_set_option(m_term, GF_OPT_PLAY_STATE, GF_STATE_STEP_PAUSE);
	UpdatePlay();
}

void wxOsmo4Frame::OnFileStop(wxCommandEvent &WXUNUSED(event))
{
	Stop();
}

void wxOsmo4Frame::Stop()
{
	if (gf_term_get_option(m_term, GF_OPT_PLAY_STATE)==GF_STATE_PLAYING) {
		gf_term_set_option(m_term, GF_OPT_PLAY_STATE, GF_STATE_PAUSED);
	}
	m_bToReset = 1;
	m_pTimer->Stop();
	m_pProg->SetValue(0);
	UpdatePlay();
}

void wxOsmo4Frame::OnSlide(wxScrollEvent &event)
{
	if (!m_duration) return;
	s32 type = event.GetEventType();

	if (type == wxEVT_SCROLL_THUMBTRACK) {
	  m_bGrabbed = 1;
	  Double now = (Double) event.GetPosition();
	  now /= 1000;
	  now *= m_duration;
	  wxString str = format_time((u32) (now), 1000);
	  m_pStatusbar->SetStatusText(str);
	} 
	else if (m_bGrabbed){
	    m_bGrabbed = 0;
	    Double res = (Double) m_pProg->GetValue();
	    res /= 1000;
	    res *= m_duration;
	    if (gf_term_get_option(m_term, GF_OPT_PLAY_STATE)==GF_STATE_PAUSED) {
			gf_term_set_option(m_term, GF_OPT_PLAY_STATE, GF_STATE_PLAYING);
			m_bToReset = 0;
			m_pTimer->Start(100, 0);
	    }
	    gf_term_play_from_time(m_term, (u32) res);
	}
}


void wxOsmo4Frame::ReloadTerminal()
{
	Bool reconnect = m_connected;
	::wxLogMessage(wxT("Reloading MPEG-4 Terminal"));

	u32 reconnect_time = 0;
	if (m_duration) reconnect_time = gf_term_get_time_in_ms(m_term);

	gf_term_del(m_term);
	/*SET IT TO NULL to make sure we don't process callbacks during setup!!!*/
	m_term = NULL;

	CheckVideoOut();
	m_term = gf_term_new(&m_user);
	if (!m_term) {
		wxMessageDialog(this, wxT("Fatal Error !!"), wxT("Couldn't change renderer"), wxOK);
		Destroy();
		return;
	}
	if (reconnect) {
		if (reconnect_time) {
			gf_term_connect_from_time(m_term, m_pPlayList->GetURL().mb_str(wxConvUTF8), reconnect_time);
		} else {
			gf_term_connect(m_term, m_pPlayList->GetURL().mb_str(wxConvUTF8));
		}
	}
	::wxLogMessage(wxT("MPEG-4 Terminal reloaded"));

	UpdateRenderSwitch();
}


void wxOsmo4Frame::BuildViewList()
{
	if (!vp_list || !m_connected) return;

	while (vp_list->GetMenuItemCount()) {
		wxMenuItem* it = vp_list->FindItemByPosition(0);
		vp_list->Delete(it);
	}

	s32 id = ID_VIEWPOINT_FIRST;
	nb_viewpoints = 0;
	while (1) {
		const char *szName;
		Bool bound;
		GF_Err e = gf_term_get_viewpoint(m_term, nb_viewpoints+1, &szName, &bound);
		if (e) break;
		if (szName) {
			vp_list->AppendCheckItem(id+nb_viewpoints, wxString(szName, wxConvUTF8) );
		} else {
			vp_list->AppendCheckItem(id+nb_viewpoints, wxString::Format(wxT("Viewpoint #%d"), nb_viewpoints+1) );
		}
		nb_viewpoints++;
	}
}

void wxOsmo4Frame::OnViewport(wxCommandEvent & event)
{
	u32 ID = event.GetId() - ID_VIEWPOINT_FIRST;
	gf_term_set_viewpoint(m_term, ID+1, NULL);
}

void wxOsmo4Frame::OnUpdateViewport(wxUpdateUIEvent & event)
{
	u32 ID = event.GetId() - ID_VIEWPOINT_FIRST;
	const char *szName;
	Bool bound;
	gf_term_get_viewpoint(m_term, ID+1, &szName, &bound);
	event.Enable(1);
	if (bound) event.Check(1);
}

void wxOsmo4Frame::OnNavigate(wxCommandEvent & event)
{
	switch (event.GetId()) {
	case ID_NAVIGATE_NONE: gf_term_set_option(m_term, GF_OPT_NAVIGATION, GF_NAVIGATE_NONE); break;
	case ID_NAVIGATE_WALK: gf_term_set_option(m_term, GF_OPT_NAVIGATION, GF_NAVIGATE_WALK); break;
	case ID_NAVIGATE_FLY: gf_term_set_option(m_term, GF_OPT_NAVIGATION, GF_NAVIGATE_FLY); break;
	case ID_NAVIGATE_EXAMINE: gf_term_set_option(m_term, GF_OPT_NAVIGATION, GF_NAVIGATE_EXAMINE); break;
	case ID_NAVIGATE_PAN: gf_term_set_option(m_term, GF_OPT_NAVIGATION, GF_NAVIGATE_PAN); break;
	case ID_NAVIGATE_SLIDE: gf_term_set_option(m_term, GF_OPT_NAVIGATION, GF_NAVIGATE_SLIDE); break;
	case ID_NAVIGATE_ORBIT: gf_term_set_option(m_term, GF_OPT_NAVIGATION, GF_NAVIGATE_ORBIT); break;
	case ID_NAVIGATE_GAME: gf_term_set_option(m_term, GF_OPT_NAVIGATION, GF_NAVIGATE_GAME); break;
	}
}
void wxOsmo4Frame::OnNavigateReset(wxCommandEvent & WXUNUSED(event))
{
	gf_term_set_option(m_term, GF_OPT_NAVIGATION_TYPE, 0);
}
void wxOsmo4Frame::OnUpdateNavigation(wxUpdateUIEvent & event)
{
	u32 ID = event.GetId();
	event.Enable(0);
	if (!m_connected) return;
	u32 type = gf_term_get_option(m_term, GF_OPT_NAVIGATION_TYPE);
	bool enable = type ? 1 : 0;

	u32 mode = gf_term_get_option(m_term, GF_OPT_NAVIGATION);
	/*common 2D/3D modes*/
	if (ID==ID_NAVIGATE_NONE) { event.Enable(enable); event.Check(mode ? 0 : 1); }
	else if (ID==ID_NAVIGATE_EXAMINE) { event.Enable(enable); event.Check((mode==GF_NAVIGATE_EXAMINE) ? 1 : 0); }
	else if (ID==ID_NAVIGATE_SLIDE) { event.Enable(enable); event.Check((mode==GF_NAVIGATE_SLIDE) ? 1 : 0); }

	if (type==GF_NAVIGATE_TYPE_2D) return;
	event.Enable(enable); 	
	if (ID==ID_NAVIGATE_WALK) event.Check((mode==GF_NAVIGATE_WALK) ? 1 : 0);
	else if (ID==ID_NAVIGATE_FLY) event.Check((mode==GF_NAVIGATE_FLY) ? 1 : 0);
	else if (ID==ID_NAVIGATE_PAN) event.Check((mode==GF_NAVIGATE_PAN) ? 1 : 0);
	else if (ID==ID_NAVIGATE_ORBIT) event.Check((mode==GF_NAVIGATE_ORBIT) ? 1 : 0);
	else if (ID==ID_NAVIGATE_GAME) event.Check((mode==GF_NAVIGATE_GAME) ? 1 : 0);
}

void wxOsmo4Frame::OnRenderSwitch(wxCommandEvent &WXUNUSED(event))
{
	const char *opt = gf_cfg_get_key(m_user.config, "Rendering", "RendererName");
	if (!stricmp(opt, "GPAC 2D Renderer"))
		gf_cfg_set_key(m_user.config, "Rendering", "RendererName", "GPAC 3D Renderer");
	else
		gf_cfg_set_key(m_user.config, "Rendering", "RendererName", "GPAC 2D Renderer");

	ReloadTerminal();
}

void wxOsmo4Frame::UpdateRenderSwitch()
{
	const char *opt = gf_cfg_get_key(m_user.config, "Rendering", "RendererName");
	m_pToolBar->RemoveTool(SWITCH_RENDER);
	if (!stricmp(opt, "GPAC 3D Renderer")) 
		m_pToolBar->InsertTool(12, SWITCH_RENDER, *m_pSW3D, wxNullBitmap, FALSE, NULL, wxT("Switch to 2D Renderer"));
	else
		m_pToolBar->InsertTool(12, SWITCH_RENDER, *m_pSW2D, wxNullBitmap, FALSE, NULL, wxT("Switch to 3D Renderer"));

#ifdef WIN32
	/*there's a display bug with the menubtn, remove and reinsert*/
	m_pToolBar->RemoveTool(FILE_PREV);
	m_pToolBar->RemoveTool(FILE_NEXT);
	m_pToolBar->InsertControl(2, m_pPrevBut);
	m_pToolBar->InsertControl(3, m_pNextBut);
#endif
	m_pToolBar->Realize();
}

void wxOsmo4Frame::OnTermReload(wxCommandEvent &WXUNUSED(event))
{
	ReloadTerminal();
}

void wxOsmo4Frame::UpdatePlay()
{
	m_pToolBar->RemoveTool(FILE_PLAY);
	if (m_connected) {
		if (gf_term_get_option(m_term, GF_OPT_PLAY_STATE)==GF_STATE_PAUSED)
			m_pToolBar->InsertTool(5, FILE_PLAY, *m_pPlay, wxNullBitmap, FALSE, NULL, wxT("Pause File"));
		else
			m_pToolBar->InsertTool(5, FILE_PLAY, *m_pPause, wxNullBitmap, FALSE, NULL, wxT("Play File"));
	} else {
		m_pToolBar->InsertTool(5, FILE_PLAY, *m_pPlay, wxNullBitmap, FALSE, NULL, wxT("Pause File"));
	}

#ifdef WIN32
	/*there's a display bug with the menubtn, remove and reinsert*/
	m_pToolBar->RemoveTool(FILE_PREV);
	m_pToolBar->RemoveTool(FILE_NEXT);
	m_pToolBar->InsertControl(2, m_pPrevBut);
	m_pToolBar->InsertControl(3, m_pNextBut);
#endif
	m_pToolBar->Realize();
}

void wxOsmo4Frame::OnCollide(wxCommandEvent & event)
{
	u32 ID = event.GetId();
	if (ID==ID_COLLIDE_NONE) gf_term_set_option(m_term, GF_OPT_COLLISION, GF_COLLISION_NONE);
	else if (ID==ID_COLLIDE_REG) gf_term_set_option(m_term, GF_OPT_COLLISION, GF_COLLISION_NORMAL);
	else if (ID==ID_COLLIDE_DISP) gf_term_set_option(m_term, GF_OPT_COLLISION, GF_COLLISION_DISPLACEMENT);
}
void wxOsmo4Frame::OnUpdateCollide(wxUpdateUIEvent & event)
{
	u32 ID = event.GetId();
	event.Enable(0);
	if (!m_connected) return;
	event.Enable(1);
	u32 mode = gf_term_get_option(m_term, GF_OPT_COLLISION);
	if (ID==ID_COLLIDE_NONE) { event.Check((mode==GF_COLLISION_NONE) ? 1 : 0); }
	else if (ID==ID_COLLIDE_REG) { event.Check((mode==GF_COLLISION_NORMAL) ? 1 : 0); }
	else if (ID==ID_COLLIDE_DISP) { event.Check((mode==GF_COLLISION_DISPLACEMENT) ? 1 : 0); }
}

void wxOsmo4Frame::OnHeadlight(wxCommandEvent &WXUNUSED(event))
{
	Bool val = !gf_term_get_option(m_term, GF_OPT_HEADLIGHT);
	gf_term_set_option(m_term, GF_OPT_HEADLIGHT, val);
}
void wxOsmo4Frame::OnUpdateHeadlight(wxUpdateUIEvent & event)
{
	event.Enable(0);
	if (!m_connected) return;
	u32 type = gf_term_get_option(m_term, GF_OPT_NAVIGATION_TYPE);
	if (type!=GF_NAVIGATE_TYPE_3D) return;

	event.Enable(1);
	event.Check(gf_term_get_option(m_term, GF_OPT_HEADLIGHT) ? 1 : 0);
}
void wxOsmo4Frame::OnGravity(wxCommandEvent & WXUNUSED(event))
{
	Bool val = gf_term_get_option(m_term, GF_OPT_GRAVITY) ? 0 : 1;
	gf_term_set_option(m_term, GF_OPT_GRAVITY, val);
}
void wxOsmo4Frame::OnUpdateGravity(wxUpdateUIEvent & event)
{
	event.Enable(0);
	if (!m_connected) return;
	u32 type = gf_term_get_option(m_term, GF_OPT_NAVIGATION_TYPE);
	if (type!=GF_NAVIGATE_TYPE_3D) return;
	type = gf_term_get_option(m_term, GF_OPT_NAVIGATION);
	if (type != GF_NAVIGATE_WALK) return;
	event.Enable(1);
	event.Check(gf_term_get_option(m_term, GF_OPT_GRAVITY) ? 1 : 0);
}


BEGIN_EVENT_TABLE(wxMyComboBox, wxComboBox)
	EVT_KEY_UP(wxMyComboBox::OnKeyUp)
END_EVENT_TABLE()

void wxMyComboBox::OnKeyUp(wxKeyEvent &event)
{
	if (event.GetKeyCode()==WXK_RETURN) {
		event.Skip();
		wxCommandEvent evt;
		evt.SetEventType(wxEVT_COMMAND_COMBOBOX_SELECTED);
		evt.SetEventObject(this);
		evt.SetId(GetId());
		GetParent()->AddPendingEvent(evt);
	}
}


void wxOsmo4Frame::ReloadURLs()
{
	const char *sOpt;
	char filename[1024];
	u32 i=0;

	m_Address->Clear();
	while (1) {
		sprintf(filename, "last_file_%d", i);
		sOpt = gf_cfg_get_key(m_user.config, "General", filename);
		if (!sOpt) break;
		m_Address->Append(wxString(sOpt, wxConvUTF8) );
		i++;
	}
}

void wxOsmo4Frame::SelectionReady() 
{
	wxString urlVal = m_Address->GetValue();
	if (urlVal.Find(wxT("://"))>0) {
		const char *sOpt;
		char filename[1024];
		u32 i=0;

		while (1) {
			sprintf(filename, "last_file_%d", i);
			sOpt = gf_cfg_get_key(m_user.config, "General", filename);
			if (!sOpt) break;
			if (!strcmp(sOpt, urlVal.mb_str(wxConvUTF8))) {
				m_pPlayList->Truncate();
				m_pPlayList->QueueURL(urlVal);
				m_pPlayList->RefreshList();
				m_pPlayList->PlayNext();
				return;
			}
			i++;
		}
		/*add it*/
		if (i<10) {
			gf_cfg_set_key(m_user.config, "General", filename, urlVal.mb_str(wxConvUTF8));
		} else {
			gf_cfg_set_key(m_user.config, "General", "last_file_10", urlVal.mb_str(wxConvUTF8));
		}
		ReloadURLs();
	}
	m_pPlayList->Truncate();
	m_pPlayList->QueueURL(urlVal);
	m_pPlayList->RefreshList();
	m_pPlayList->PlayNext();
}

void wxOsmo4Frame::OnURLSelect(wxCommandEvent &WXUNUSED(event))
{
	SelectionReady();
}

void wxOsmo4Frame::OnPlaylist(wxCommandEvent &WXUNUSED(event))
{
	assert(m_pPlayList);
	m_pPlayList->Show(m_pPlayList->IsShown() ? 0 : 1);
}

void wxOsmo4Frame::OnUpdatePlayList(wxUpdateUIEvent & event)
{
	event.Enable(1);
	event.Check(m_pPlayList->IsShown() ? 1 : 0);
}

void wxOsmo4Frame::OnFilePrevOpen(wxNotifyEvent & event)
{
	u32 count = gf_list_count(m_pPlayList->m_entries);
	u32 start = m_pPlayList->m_cur_entry - 1;
	wxMenu *popup = new wxMenu();

	for (u32 i=0; i<10; i++) {
		if (start - i < 0) break;
		if (start - i >= count) break;
		PLEntry *ple = (PLEntry *) gf_list_get(m_pPlayList->m_entries, start - i);
		popup->Append(ID_NAV_PREV_0 + i, wxString(ple->m_disp_name, wxConvUTF8) );
	}
	m_pPrevBut->AssignMenu(popup);
}

void wxOsmo4Frame::OnFileNextOpen(wxNotifyEvent & event)
{
	u32 count = gf_list_count(m_pPlayList->m_entries);
	wxMenu *popup = new wxMenu();
	u32 start = m_pPlayList->m_cur_entry + 1;
	for (u32 i=0; i<10; i++) {
		if (start + i >= count) break;
		PLEntry *ple = (PLEntry *) gf_list_get(m_pPlayList->m_entries, start + i);
		popup->Append(ID_NAV_NEXT_0 + i, wxString(ple->m_disp_name, wxConvUTF8) );
	}
	m_pNextBut->AssignMenu(popup);
}

void wxOsmo4Frame::OnNavPrev(wxCommandEvent &WXUNUSED(event))
{
	if (m_pPlayList->m_cur_entry<=0) return;
	m_pPlayList->PlayPrev();
}
void wxOsmo4Frame::OnUpdateNavPrev(wxUpdateUIEvent & event)
{
	if (m_pPlayList->m_cur_entry<=0) event.Enable(0);
	else event.Enable(TRUE);
}
void wxOsmo4Frame::OnNavPrevMenu(wxCommandEvent &event)
{
	u32 ID = event.GetId() - ID_NAV_PREV_0;
	s32 prev = m_pPlayList->m_cur_entry - ID;
	if (prev>=0) {
		m_pPlayList->m_cur_entry = prev;
		m_pPlayList->PlayPrev();
	}
}
void wxOsmo4Frame::OnNavNext(wxCommandEvent &WXUNUSED(event))
{
	/*don't play if last could trigger playlist loop*/
	if ((m_pPlayList->m_cur_entry<0) || (gf_list_count(m_pPlayList->m_entries) == 1 + (u32) m_pPlayList->m_cur_entry)) return;
	m_pPlayList->PlayNext();
}
void wxOsmo4Frame::OnUpdateNavNext(wxUpdateUIEvent & event)
{
	if (m_pPlayList->m_cur_entry<0) event.Enable(0);
	else if ((u32) m_pPlayList->m_cur_entry + 1 == gf_list_count(m_pPlayList->m_entries) ) event.Enable(0);
	else event.Enable(1);
}

void wxOsmo4Frame::OnNavNextMenu(wxCommandEvent &event)
{
	u32 ID = event.GetId() - ID_NAV_NEXT_0;
	s32 next = m_pPlayList->m_cur_entry + ID;
	if (next < (s32) gf_list_count(m_pPlayList->m_entries) ) {
		m_pPlayList->m_cur_entry = next;
		m_pPlayList->PlayNext();
	}
}

void wxOsmo4Frame::OnClearNav(wxCommandEvent &WXUNUSED(event))
{
	m_pPlayList->ClearButPlaying();
}


void wxOsmo4Frame::BuildStreamList(Bool reset_only)
{
	u32 nb_subs;
	wxMenu *pMenu;

	pMenu = sel_menu->FindItemByPosition(0)->GetSubMenu();
	while (pMenu->GetMenuItemCount()) {
		wxMenuItem* it = pMenu->FindItemByPosition(0);
		pMenu->Delete(it);
	}
	pMenu = sel_menu->FindItemByPosition(1)->GetSubMenu();
	while (pMenu->GetMenuItemCount()) {
		wxMenuItem* it = pMenu->FindItemByPosition(0);
		pMenu->Delete(it);
	}
	pMenu = sel_menu->FindItemByPosition(2)->GetSubMenu();
	while (pMenu->GetMenuItemCount()) {
		wxMenuItem* it = pMenu->FindItemByPosition(0);
		pMenu->Delete(it);
	}

	if (reset_only) {
		m_bFirstStreamListBuild = 1;
		return;
	}

	if (!gf_term_get_option(m_term, GF_OPT_CAN_SELECT_STREAMS)) return;

	nb_subs = 0;
	GF_ObjectManager *root_od = gf_term_get_root_object(m_term);
	if (!root_od) return;
	u32 count = gf_term_get_object_count(m_term, root_od);

	for (u32 i=0; i<count; i++) {
		char szLabel[1024];
		ODInfo info;
		GF_ObjectManager *odm = gf_term_get_object(m_term, root_od, i);
		if (!odm) return;

		GF_ObjectManager *remote = odm;
		while (1) {
			remote = gf_term_get_remote_object(m_term, odm);
			if (remote) odm = remote;
			else break;
		}

		if (gf_term_get_object_info(m_term, odm, &info) != GF_OK) break;

		if (info.owns_service) {
			char *szName = strrchr(info.service_url, '\\');
			if (!szName) szName = strrchr(info.service_url, '/');
			if (!szName) szName = (char *) info.service_url;
			else szName += 1;
			strcpy(szLabel, szName);
			szName = strrchr(szLabel, '.');
			if (szName) szName[0] = 0;
		}
		switch (info.od_type) {
		case GF_STREAM_AUDIO:
			pMenu = sel_menu->FindItemByPosition(0)->GetSubMenu();
			if (!info.owns_service) sprintf(szLabel, "Audio #%d", pMenu->GetMenuItemCount() + 1);
			pMenu->AppendCheckItem(ID_SELSTREAM_0 +i, wxString(szLabel, wxConvUTF8));
			break;
		case GF_STREAM_VISUAL:
			pMenu = sel_menu->FindItemByPosition(1)->GetSubMenu();
			if (!info.owns_service) sprintf(szLabel, "Video #%d", pMenu->GetMenuItemCount() + 1);
			pMenu->AppendCheckItem(ID_SELSTREAM_0 +i, wxString(szLabel, wxConvUTF8));
			break;
		case GF_STREAM_TEXT:
			nb_subs ++;
			pMenu = sel_menu->FindItemByPosition(2)->GetSubMenu();
			if (!info.owns_service) sprintf(szLabel, "Subtitle #%d", pMenu->GetMenuItemCount() + 1);
			pMenu->AppendCheckItem(ID_SELSTREAM_0 +i, wxString(szLabel, wxConvUTF8));
			break;
		}
	}
	if (m_bFirstStreamListBuild) {
		m_bFirstStreamListBuild = 0;
		if (!nb_subs && m_lookforsubs) LookForSubtitles();
	}
}

void wxOsmo4Frame::OnStreamSel(wxCommandEvent & event)
{
	GF_ObjectManager *root_od = gf_term_get_root_object(m_term);
	if (!root_od) return;
	u32 ID = event.GetId() - ID_SELSTREAM_0;
	GF_ObjectManager *odm = gf_term_get_object(m_term, root_od, ID);
	gf_term_select_object(m_term, odm);
}

void wxOsmo4Frame::OnUpdateStreamSel(wxUpdateUIEvent & event)
{
	GF_ObjectManager *root_od = gf_term_get_root_object(m_term);
	if (!root_od) return;
	u32 ID = event.GetId() - ID_SELSTREAM_0;

	GF_ObjectManager *odm = gf_term_get_object(m_term, root_od, ID);
	if (!odm) return;
	GF_ObjectManager *remote = odm;
	while (1) {
		remote = gf_term_get_remote_object(m_term, odm);
		if (remote) odm = remote;
		else break;
	}

	ODInfo info;
	gf_term_get_object_info(m_term, odm, &info);
	event.Enable(1);
	event.Check(info.status ? 1 : 0);
}

void wxOsmo4Frame::OnUpdateStreamMenu(wxUpdateUIEvent & event)
{
	if (!m_connected || !gf_term_get_option(m_term, GF_OPT_CAN_SELECT_STREAMS)) {
		event.Enable(0);
	} else {
		event.Enable(1);
	}
}

void wxOsmo4Frame::OnAddSub(wxCommandEvent &WXUNUSED(event))
{
	wxFileDialog dlg(this, wxT("Add Subtitle"), wxT(""), wxT(""), wxT("All Subtitles|*.srt;*.ttxt|SRT Subtitles|*.srt|3GPP TimedText|*.ttxt|"), wxOPEN | wxCHANGE_DIR | wxHIDE_READONLY);

	if (dlg.ShowModal() == wxID_OK) {
		AddSubtitle(dlg.GetPath().mb_str(wxConvUTF8), 1);
	}

}

void wxOsmo4Frame::AddSubtitle(const char *fileName, Bool auto_play)
{
	gf_term_add_object(m_term, fileName, auto_play);
}

static Bool subs_enum_dir_item(void *cbck, char *item_name, char *item_path)
{
	char *ext;
	wxOsmo4Frame *_this = (wxOsmo4Frame*)cbck;

	ext = strrchr(item_name, '.');
	if (!ext) return 0;
	ext += 1;
	if (!stricmp(ext, "srt") || !stricmp(ext, "ttxt")) 
		_this->AddSubtitle(item_path, 0);

	return 0;
}

void wxOsmo4Frame::LookForSubtitles()
{
	char dir[GF_MAX_PATH];
	const char *url = m_pPlayList->GetURL().mb_str(wxConvUTF8);
	strcpy(dir, url);
	char *sep = strrchr(dir, '\\');
	if (!sep) strcpy(dir, ::wxGetCwd().mb_str(wxConvUTF8));
	else sep[0] = 0;

	gf_enum_directory(dir, 0, subs_enum_dir_item, this);
}

void wxOsmo4Frame::OnCacheEnable(wxCommandEvent &WXUNUSED(event))
{
	u32 state = gf_term_get_option(m_term, GF_OPT_MEDIA_CACHE);
	if (state==GF_MEDIA_CACHE_DISABLED) {
		gf_term_set_option(m_term, GF_OPT_MEDIA_CACHE, GF_MEDIA_CACHE_ENABLED);
	} else if (state==GF_MEDIA_CACHE_DISABLED) {
		gf_term_set_option(m_term, GF_OPT_MEDIA_CACHE, GF_MEDIA_CACHE_DISABLED);
	}
}

void wxOsmo4Frame::OnCacheStop(wxCommandEvent &WXUNUSED(event))
{
	gf_term_set_option(m_term, GF_OPT_MEDIA_CACHE, GF_MEDIA_CACHE_DISABLED);
}

void wxOsmo4Frame::OnCacheAbort(wxCommandEvent &WXUNUSED(event))
{
	gf_term_set_option(m_term, GF_OPT_MEDIA_CACHE, GF_MEDIA_CACHE_DISCARD);
}

void wxOsmo4Frame::OnUpdateCacheEnable(wxUpdateUIEvent & event)
{
	u32 state = gf_term_get_option(m_term, GF_OPT_MEDIA_CACHE);
	switch (state) {
	case GF_MEDIA_CACHE_ENABLED:
		event.Enable(1);
		event.SetText(wxT("Enabled")); 
		break;
	case GF_MEDIA_CACHE_RUNNING: 
		event.SetText(wxT("Running")); 
		event.Enable(0);
		break;
	case GF_MEDIA_CACHE_DISABLED: 
		event.SetText(wxT("Disabled")); 
		break;
	}
}

void wxOsmo4Frame::OnUpdateCacheAbort(wxUpdateUIEvent & event)
{
	u32 state = gf_term_get_option(m_term, GF_OPT_MEDIA_CACHE);
	event.Enable( (state==GF_MEDIA_CACHE_RUNNING) ? 1 : 0);
}



void wxOsmo4Frame::BuildChapterList(Bool reset_only)
{
	ODInfo odi;

	while (chap_menu->GetMenuItemCount()) {
		wxMenuItem* it = chap_menu->FindItemByPosition(0);
		chap_menu->Delete(it);
	}
	if (m_chapters_start) free(m_chapters_start);
	m_chapters_start = NULL;
	m_num_chapters = 0;
	if (reset_only) return;

	GF_ObjectManager *root_od = gf_term_get_root_object(m_term);
	if (!root_od) return;
	if (gf_term_get_object_info(m_term, root_od, &odi) != GF_OK) return;

	u32 count = gf_list_count(odi.od->OCIDescriptors);
	m_num_chapters = 0;
	for (u32 i=0; i<count; i++) {
		char szLabel[1024];
		GF_Segment *seg = (GF_Segment *) gf_list_get(odi.od->OCIDescriptors, i);
		if (seg->tag != GF_ODF_SEGMENT_TAG) continue;

		if (seg->SegmentName && strlen((const char *)seg->SegmentName)) {
			strcpy(szLabel, (const char *) seg->SegmentName);
		} else {
			sprintf(szLabel, "Chapter %02d", m_num_chapters+1);
		}
		chap_menu->AppendCheckItem(ID_SETCHAP_FIRST + m_num_chapters, wxString(szLabel, wxConvUTF8));

		m_chapters_start = (Double *) realloc(m_chapters_start, sizeof(Double)*(m_num_chapters+1));
		m_chapters_start[m_num_chapters] = seg->startTime;
		m_num_chapters++;
	}
}

void wxOsmo4Frame::OnChapterSel(wxCommandEvent & event)
{
	GF_ObjectManager *root_od = gf_term_get_root_object(m_term);
	if (!root_od) return;
	u32 ID = event.GetId() - ID_SETCHAP_FIRST;
	gf_term_play_from_time(m_term, (u32) (1000*m_chapters_start[ID]));
}

void wxOsmo4Frame::OnUpdateChapterSel(wxUpdateUIEvent & event)
{
	Double now;
	Bool is_current;
	u32 ID = event.GetId() - ID_SETCHAP_FIRST;

	now = gf_term_get_time_in_ms(m_term);
	now /= 1000;

	is_current = 0;
	if (m_chapters_start[ID]<=now) {
		if (ID+1<m_num_chapters) {
			if (m_chapters_start[ID+1]>now) is_current = 1;
		} else {
			is_current = 1;
		}
	}
	event.Enable(1);
	event.Check(is_current ? 1 : 0);
}

void wxOsmo4Frame::OnUpdateChapterMenu(wxUpdateUIEvent & event)
{
	if (!m_connected || !m_num_chapters) {
		event.Enable(0);
	} else {
		event.Enable(1);
	}
}
