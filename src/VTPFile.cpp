/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "xbmc/libXBMC_addon.h"
#include "xbmc/threads/mutex.h"
#include <map>
#include <sstream>

#include "VTPSession.h"

ADDON::CHelper_libXBMC_addon *XBMC           = NULL;

extern "C" {

#include "xbmc/xbmc_vfs_dll.h"
#include "xbmc/IFileTypes.h"

//-- Create -------------------------------------------------------------------
// Called on load. Addon should fully initalize or return error status
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!XBMC)
    XBMC = new ADDON::CHelper_libXBMC_addon;

  if (!XBMC->RegisterMe(hdl))
  {
    delete XBMC, XBMC=NULL;
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  return ADDON_STATUS_OK;
}

//-- Stop ---------------------------------------------------------------------
// This dll must cease all runtime activities
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Stop()
{
}

//-- Destroy ------------------------------------------------------------------
// Do everything before unload of this add-on
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Destroy()
{
  XBMC=NULL;
}

//-- HasSettings --------------------------------------------------------------
// Returns true if this add-on use settings
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
bool ADDON_HasSettings()
{
  return false;
}

//-- GetStatus ---------------------------------------------------------------
// Returns the current Status of this visualisation
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_GetStatus()
{
  return ADDON_STATUS_OK;
}

//-- GetSettings --------------------------------------------------------------
// Return the settings for XBMC to display
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
unsigned int ADDON_GetSettings(ADDON_StructSetting ***sSet)
{
  return 0;
}

//-- FreeSettings --------------------------------------------------------------
// Free the settings struct passed from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------

void ADDON_FreeSettings()
{
}

//-- SetSetting ---------------------------------------------------------------
// Set a specific Setting value (called from XBMC)
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_SetSetting(const char *strSetting, const void* value)
{
  return ADDON_STATUS_OK;
}

//-- Announce -----------------------------------------------------------------
// Receive announcements from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Announce(const char *flag, const char *sender, const char *message, const void *data)
{
}

struct VTPContext
{
  CVTPSession  session;
  SOCKET       socket;
  int          channel;

  VTPContext()
  {
    socket = INVALID_SOCKET;
    channel = 0;
  }
};

void* Open(VFSURL* url)
{
  std::string host(url->hostname);
  if (host.empty())
    host = "localhost";
  if (url->port == 0)
    url->port = 2004;

  if (strstr(url->filename, "channels/") == url->filename)
  {
    std::string channel = url->filename+9;
    if (channel.find(".ts") == std::string::npos)
    {
      XBMC->Log(ADDON::LOG_ERROR, "%s - invalid channel url %s", __FUNCTION__, channel.c_str());
      return NULL;
    }

    VTPContext* result = new VTPContext;
    result->session.Open(host, url->port);
    result->channel = atoi(channel.c_str());
    result->socket = result->session.GetStreamLive(result->channel);
    return result;
  }
  else
  {
    XBMC->Log(ADDON::LOG_ERROR, "%s - invalid path specified %s", __FUNCTION__, url->filename);
    return NULL;
  }
}

bool Close(void* context)
{
  VTPContext* ctx = (VTPContext*)context;
  ctx->session.AbortStreamLive();
  closesocket(ctx->socket);
  ctx->session.Close();
  delete ctx;
}

int64_t GetLength(void* context)
{
  return -1;
}

int64_t GetPosition(void* context)
{
  return -1;
}

int64_t Seek(void* context, int64_t iFilePosition, int iWhence)
{
  return -1;
}

bool Exists(VFSURL* url)
{
  return false;
}

int Stat(VFSURL* url, struct __stat64* buffer)
{
  return -1;
}

int IoControl(void* context, XFILE::EIoControl request, void* param)
{
  if(request == XFILE::IOCTRL_SEEK_POSSIBLE)
    return 0;

  return -1;
}

void ClearOutIdle()
{
}

void DisconnectAll()
{
}

bool DirectoryExists(VFSURL* url)
{
  return false;
}

void* GetDirectory(VFSURL* url, VFSDirEntry** items,
                   int* num_items, VFSCallbacks* callbacks)
{
  std::string host(url->hostname);
  if(host.empty())
    host = "localhost";

  std::string base(url->url);
  if (base[base.size()-1] == '/')
    base.erase(base.end()-1);

  // add port after, it changes the structure
  // and breaks CUtil::GetMatchingSource
  if(url->port == 0)
    url->port = 2004;

  CVTPSession session;

  if(!session.Open(host, url->port))
    return NULL;

  if(strlen(url->filename) == 0)
  {
    std::vector<VFSDirEntry>* itms = new std::vector<VFSDirEntry>(1);
    *items = &(*itms)[0];
    items[0]->path = strdup((base+"/channels/").c_str());
    items[0]->folder = true;
    items[0]->label = XBMC->GetLocalizedString(3000);
    items[0]->title = NULL;
    items[0]->properties = new VFSProperty;
    items[0]->properties->name = strdup("propmisusepreformatted");
    items[0]->properties->val = strdup("true");
    items[0]->num_props = 1;
    *num_items = 1;
    return itms;
  }
  else if(strcmp(url->filename, "channels/") == 0)
  {
    std::vector<CVTPSession::Channel> channels;
    if(!session.GetChannels(channels))
      return NULL;

    std::vector<VFSDirEntry>* itms = new std::vector<VFSDirEntry>(channels.size());

    std::vector<CVTPSession::Channel>::iterator it;
    std::vector<VFSDirEntry>::iterator it2 = itms->begin();
    for(it = channels.begin(); it != channels.end(); it++, ++it2)
    {
      char temp[128];
      sprintf(temp, "%s/%d.ts", base.c_str(), it->index);
      it2->path = strdup(temp);
      it2->folder = false;
      it2->title = strdup(it->name.c_str());
      sprintf(temp, "%d - %s", it->index, it->name.c_str());
      it2->label = strdup(temp);
    }
    *items = &(*itms)[0];
    *num_items = itms->size();
    return itms;
  }
  else
    return NULL;
}

void FreeDirectory(void* items)
{
  std::vector<VFSDirEntry>& ctx = *(std::vector<VFSDirEntry>*)items;
  for (size_t i=0;i<ctx.size();++i)
  {
    free(ctx[i].label);
    for (size_t j=0;j<ctx[i].num_props;++j)
    {
      free(ctx[i].properties[j].name);
      free(ctx[i].properties[j].val);
    }
    delete ctx[i].properties;
    free(ctx[i].path);
    free(ctx[i].title);
  }
  delete &ctx;
}

bool CreateDirectory(VFSURL* url)
{
  return false;
}

bool RemoveDirectory(VFSURL* url)
{
  return false;
}

int Truncate(void* context, int64_t size)
{
  return -1;
}

ssize_t Write(void* context, const void* lpBuf, size_t uiBufSize)
{
  return -1;
}

bool Delete(VFSURL* url)
{
  return false;
}

bool Rename(VFSURL* url, VFSURL* url2)
{
  return false;
}

void* OpenForWrite(VFSURL* url, bool bOverWrite)
{ 
  return NULL;
}

void* ContainsFiles(VFSURL* url, VFSDirEntry** items, int* num_items, char* rootpath)
{
  return NULL;
}

int GetStartTime(void* ctx)
{
  return 0;
}

int GetTotalTime(void* ctx)
{
  return 0;
}

bool NextChannel(void* context, bool preview)
{
  VTPContext* ctx = (VTPContext*)context;

  int channel = ctx->channel;
  while(++channel < 1000)
  {
    if(!ctx->session.CanStreamLive(channel))
      continue;

    if(ctx->socket != INVALID_SOCKET)
    {
      shutdown(ctx->socket, SHUT_RDWR);
      ctx->session.AbortStreamLive();
      closesocket(ctx->socket);
    }

    ctx->channel = channel;
    ctx->socket  = ctx->session.GetStreamLive(ctx->channel);
    if(ctx->socket != INVALID_SOCKET)
      return true;
  }
  return false;
}

bool PrevChannel(void* context, bool preview)
{
  VTPContext* ctx = (VTPContext*)context;

  int channel = ctx->channel;
  while(--channel > 0)
  {
    if(!ctx->session.CanStreamLive(channel))
      continue;

    if(ctx->socket != INVALID_SOCKET)
    {
      shutdown(ctx->socket, SHUT_RDWR);
      ctx->session.AbortStreamLive();
      closesocket(ctx->socket);
    }

    ctx->channel = channel;
    ctx->socket  = ctx->session.GetStreamLive(ctx->channel);
    if(ctx->socket != INVALID_SOCKET)
      return true;
  }
  return false;
}

bool SelectChannel(void* context, unsigned int channel)
{
  VTPContext* ctx = (VTPContext*)context;
  if(!ctx->session.CanStreamLive(channel))
    return false;

  ctx->session.AbortStreamLive();

  if(ctx->socket != INVALID_SOCKET)
  {
    shutdown(ctx->socket, SHUT_RDWR);
    ctx->session.AbortStreamLive();
    closesocket(ctx->socket);
  }

  ctx->channel = channel;
  ctx->socket  = ctx->session.GetStreamLive(ctx->channel);
  if(ctx->socket != INVALID_SOCKET)
    return true;
  else
    return false;
}

bool UpdateItem(void* context)
{
  return false;
}

int GetChunkSize(void* context)
{
  return 0;
}

}
