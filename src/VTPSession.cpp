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

#include "VTPSession.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <string>
#include <vector>

#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <xbmc/libXBMC_addon.h>

extern ADDON::CHelper_libXBMC_addon* XBMC;

CVTPSession::CVTPSession()
  : m_socket(INVALID_SOCKET)
{}

CVTPSession::~CVTPSession()
{
  Close();
}

bool CVTPSession::OpenStreamSocket(SOCKET& sock, struct sockaddr_in& address2)
{
  struct sockaddr_in address(address2);

  sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  if(sock == INVALID_SOCKET)
  {
    XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::OpenStreamSocket - invalid socket");
    return false;
  }

  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port        = 0;

  if(bind(sock, (struct sockaddr*) &address, sizeof(address)) == SOCKET_ERROR)
  {
    XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::OpenStreamSocket - bind failed");
    return false;
  }

  socklen_t len = sizeof(address);
  if(getsockname(sock, (struct sockaddr*) &address, &len) == SOCKET_ERROR)
  {
    XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::OpenStreamSocket - bind failed");
    return false;
  }

  if(listen(sock, 1) == SOCKET_ERROR)
  {
    XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::OpenStreamSocket - listen failed");
    return false;
  }

  address2.sin_port = address.sin_port;

  XBMC->Log(ADDON::LOG_DEBUG, "CVTPSession::OpenStreamSocket - listening on %s:%d", inet_ntoa(address.sin_addr), address.sin_port);
  return true;
}

bool CVTPSession::AcceptStreamSocket(SOCKET& sock2)
{
  SOCKET sock;
  sock = accept(sock2, NULL, NULL);
  if(sock == INVALID_SOCKET)
  {
    XBMC->Log(ADDON::LOG_ERROR, "CVTPStream::Accept - failed to accept incomming connection");
    return false;
  }

  closesocket(sock2);
  sock2 = sock;
  return true;
}

void CVTPSession::Close()
{
  if(m_socket != INVALID_SOCKET)
    closesocket(m_socket);
}

bool CVTPSession::Open(const std::string &host, int port)
{
  char     namebuf[NI_MAXHOST], portbuf[NI_MAXSERV];
  struct   addrinfo hints = {};
  struct   addrinfo *result, *addr;
  char     service[33];
  int      res;

  hints.ai_family   = AF_INET; //the streaming code doesn't support ipv6 yet
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  sprintf(service, "%d", port);

  res = getaddrinfo(host.c_str(), service, &hints, &result);
  if(res) {
    switch(res) {
    case EAI_NONAME:
      XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::Open - The specified host is unknown\n");
      break;

    case EAI_FAIL:
      XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::Open - A nonrecoverable failure in name resolution occurred\n");
      break;

    case EAI_MEMORY:
      XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::Open - A memory allocation failure occurred\n");
      break;

    case EAI_AGAIN:
      XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::Open - A temporary error occurred on an authoritative name server\n");
      break;

    default:
      XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::Open - Unknown error %d\n", res);
      break;
    }
    return false;
  }

  for(addr = result; addr; addr = addr->ai_next)
  {
    if(getnameinfo(addr->ai_addr, addr->ai_addrlen, namebuf, sizeof(namebuf), portbuf, sizeof(portbuf),NI_NUMERICHOST))
    {
      strcpy(namebuf, "[unknown]");
      strcpy(portbuf, "[unknown]");
    }
    XBMC->Log(ADDON::LOG_DEBUG, "CVTPSession::Open - connecting to: %s:%s ...", namebuf, portbuf);

    m_socket = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if(m_socket == INVALID_SOCKET)
      continue;

    if(connect(m_socket, addr->ai_addr, addr->ai_addrlen) != SOCKET_ERROR)
      break;

    closesocket(m_socket);
    m_socket = INVALID_SOCKET;
  }

  freeaddrinfo(result);
  if(m_socket == INVALID_SOCKET)
  {
    XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::Open - failed to connect to hostname %s", host.c_str());
    return false;
  }

  // VTP Server will send a greeting
  std::string line;
  int    code;
  ReadResponse(code, line);

  XBMC->Log(ADDON::LOG_DEBUG, "CVTPSession::Open - server greeting: %s", line.c_str());

  return true;
}

bool CVTPSession::ReadResponse(int &code, std::string &line)
{
  std::vector<std::string> lines;
  if(ReadResponse(code, lines))
  {
    line = lines[lines.size()-1];
    return true;
  }
  return false;
}

bool CVTPSession::ReadResponse(int &code, std::vector<std::string> &lines)
{
  fd_set         set_r, set_e;
  struct timeval tv;
  int            result;
  char           buffer[256];
  char           cont = 0;
  std::string         line;
  size_t         pos1 = 0, pos2 = 0, pos3 = 0;

  while(true)
  {
    while( (pos1 = line.find("\r\n", pos3)) != std::string::npos)
    {
      if(sscanf(line.c_str(), "%d%c", &code, &cont) != 2)
      {
        XBMC->Log(ADDON::LOG_DEBUG, "CVTPSession::ReadResponse - unknown line format: %s", line.c_str());
        line.erase(0, pos1 + 2);
        continue;
      }

      pos2 = line.find(cont);

      lines.push_back(line.substr(pos2+1, pos1-pos2-1));

      line.erase(0, pos1 + 2);
      pos3 = 0;
    }

    // we only need to recheck 1 byte
    if(line.size() > 0)
      pos3 = line.size() - 1;
    else
      pos3 = 0;

    if(cont == ' ')
      break;

    // fill with new data
    tv.tv_sec  = 10;
    tv.tv_usec = 0;

    FD_ZERO(&set_r);
    FD_ZERO(&set_e);
    FD_SET(m_socket, &set_r);
    FD_SET(m_socket, &set_e);
    result = select(FD_SETSIZE, &set_r, NULL, &set_e, &tv);
    if(result < 0)
    {
      XBMC->Log(ADDON::LOG_DEBUG, "CVTPSession::ReadResponse - select failed");
      return false;
    }

    if(result == 0)
    {
      XBMC->Log(ADDON::LOG_DEBUG, "CVTPSession::ReadResponse - timeout waiting for response, retrying...");
      continue;
    }

    result = recv(m_socket, buffer, sizeof(buffer)-1, 0);
    if(result < 0)
    {
      XBMC->Log(ADDON::LOG_DEBUG, "CVTPSession::ReadResponse - recv failed");
      return false;
    }
    buffer[result] = 0;

    line.append(buffer);
  }

#ifdef DEBUG
  XBMC->Log(ADDON::LOG_DEBUG, "CVTPSession::ReadResponse - Response code %d", code);
  for(unsigned i=0; i<lines.size(); i++)
    XBMC->Log(ADDON::LOG_DEBUG, "CVTPSession::ReadResponse - Line %d: %s", i, lines[i].c_str());
#endif

  return true;
}

bool CVTPSession::SendCommand(const std::string &command)
{
  std::string buffer;

  buffer  = command;
  buffer += "\r\n";
#ifdef DEBUG
  XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::SendCommand - sending '%s'", command.c_str());
#endif
  if(send(m_socket, buffer.c_str(), buffer.length(), 0) != (int)buffer.length())
  {
    XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::SendCommand - failed to send data");
    return false;
  }
  return true;
}

bool CVTPSession::SendCommand(const std::string &command, int &code, std::string line)
{
  std::vector<std::string> lines;
  if(SendCommand(command, code, lines))
  {
    line = lines[lines.size()-1];
    return true;
  }
  return false;
}

bool CVTPSession::SendCommand(const std::string &command, int &code, std::vector<std::string> &lines)
{
  if(!SendCommand(command))
    return false;

  if(!ReadResponse(code, lines))
    return false;

  if(code < 200 || code > 299)
  {
    XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::GetChannels - Failed with code: %d (%s)", code, lines[lines.size()-1].c_str());
    return false;
  }

  return true;
}

bool CVTPSession::GetChannels(std::vector<Channel> &channels)
{
  std::vector<std::string> lines;
  int            code;

  if(!SendCommand("LSTC", code, lines))
    return false;

  for(std::vector<std::string>::iterator it = lines.begin(); it != lines.end(); it++)
  {
    std::string& data(*it);
    size_t space, colon;
    struct Channel channel;

    space = data.find(" ");
    if(space == std::string::npos)
    {
      XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::GetChannels - failed to parse line %s", it->c_str());
      continue;
    }

    colon = data.find(":", space+1);
    if(colon == std::string::npos)
    {
      XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::GetChannels - failed to parse line %s", it->c_str());
      continue;
    }

    channel.index = atoi(data.substr(0, space).c_str());
    channel.name  = data.substr(space+1, colon-space-1);

    colon = channel.name.find(";");
    if(colon != std::string::npos)
    {
      channel.network = channel.name.substr(colon+1);
      channel.name.erase(colon);
    }

    channels.push_back(channel);

#ifdef DEBUG
    XBMC->Log(ADDON::LOG_DEBUG, "CVTPSession::GetChannels - Channel:%d, Name: '%s', Network: '%s'", channel.index, channel.name.c_str(), channel.network.c_str());
#endif
  }
  return true;
}

SOCKET CVTPSession::GetStreamLive(int channel)
{
  sockaddr_in address;
  SOCKET      sock;
  socklen_t   len = sizeof(address);
  char        buffer[1024];
  std::string      result;
  int         code;

  if(!SendCommand("CAPS TS", code, result))
  {
    XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::GetStreamLive - server is unable to provide mpeg-ts");
    return INVALID_SOCKET;
  }

  sprintf(buffer, "PROV %d %d", 100, channel);
  if(!SendCommand(buffer, code, result))
  {
    XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::GetStreamLive - server is unable to provide channel");
    return INVALID_SOCKET;
  }

  sprintf(buffer, "TUNE %d", channel);
  if(!SendCommand(buffer, code, result))
  {
    XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::GetStreamLive - server is unable to tune to said channel");
    return INVALID_SOCKET;
  }

  if(getsockname(m_socket, (struct sockaddr*) &address, &len) == SOCKET_ERROR)
  {
    XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::GetStreamLive - getsockname failed");
    return INVALID_SOCKET;
  }

  char namebuf[NI_MAXHOST], portbuf[NI_MAXSERV];
  if(getnameinfo((struct sockaddr*)&address, len, namebuf, sizeof(namebuf), portbuf, sizeof(portbuf), NI_NUMERICHOST))
  {
    strcpy(namebuf, "[unknown]");
    strcpy(portbuf, "[unknown]");
  }

  XBMC->Log(ADDON::LOG_DEBUG, "CVTPSession::GetStreamLive - local address %s:%s", namebuf, portbuf );

  if(!OpenStreamSocket(sock, address))
  {
    closesocket(sock);
    return INVALID_SOCKET;
  }

  int port = ntohs(address.sin_port);
  int addr = ntohl(address.sin_addr.s_addr);

  sprintf(buffer, "PORT 0 %d,%d,%d,%d,%d,%d"
                , (addr & 0xFF000000)>>24
                , (addr & 0x00FF0000)>>16
                , (addr & 0x0000FF00)>>8
                , (addr & 0x000000FF)>>0
                , (port & 0xFF00)>>8
                , (port & 0x00FF)>>0);

  if(!SendCommand(buffer, code, result))
  {
    closesocket(sock);
    return 0;
  }

  if(!AcceptStreamSocket(sock))
  {
    closesocket(sock);
    return INVALID_SOCKET;
  }
  return sock;
}

void CVTPSession::AbortStreamLive()
{
  if(m_socket == INVALID_SOCKET)
    return;

  std::string line;
  int    code;
  if(!SendCommand("ABRT 0", code, line))
    XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::AbortStreamLive - failed");
}

bool CVTPSession::CanStreamLive(int channel)
{
  if(m_socket == INVALID_SOCKET)
    return false;

  char   buffer[1024];
  std::string line;
  int    code;

  sprintf(buffer, "PROV %d %d", -1, channel);
  if(!SendCommand(buffer, code, line))
  {
    XBMC->Log(ADDON::LOG_ERROR, "CVTPSession::CanStreamLive - server is unable to provide channel %d", channel);
    return false;
  }
  return true;
}
