/* winsparkle.c: WinSparkle auto-update integration for Win32
   Copyright (c) 2026 Philip Kendall

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

   Author contact information:

   E-mail: philip-fuse@shadowmagic.org.uk

*/

#include "config.h"

#include "winsparkle.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

#include <winsparkle.h>

#include "fuse.h"
#include "menu.h"
#include "win32internals.h"

#ifndef WINSPARKLE_APPCAST_URL
#define WINSPARKLE_APPCAST_URL "https://speccytools.org/updates/windows/appcast.xml"
#endif

#ifndef WINSPARKLE_GITHUB_RELEASES_REPO
#define WINSPARKLE_GITHUB_RELEASES_REPO "speccytools/fusex"
#endif

static int g_winsparkle_initialized;
static CRITICAL_SECTION g_winsparkle_log_lock;
static int g_winsparkle_log_lock_inited;
static char g_winsparkle_error_log_path[PATH_MAX];

static void
win32_winsparkle_set_error_log_path( void )
{
  char buffer[PATH_MAX];
  char *slash;
  DWORD len;

  if( g_winsparkle_error_log_path[0] )
    return;

  len = GetModuleFileName( NULL, buffer, PATH_MAX );
  if( !len || len >= PATH_MAX )
    return;

  slash = strrchr( buffer, '\\' );
  if( !slash )
    slash = strrchr( buffer, '/' );
  if( slash )
    *slash = '\0';

  if( strlen( buffer ) + strlen( "\\error.log" ) + 1 > PATH_MAX )
    return;

  snprintf( g_winsparkle_error_log_path, PATH_MAX, "%s\\error.log", buffer );
}

static void
win32_winsparkle_log_event( const char *event )
{
  FILE *f;
  SYSTEMTIME st;

  if( !g_winsparkle_log_lock_inited )
    return;

  win32_winsparkle_set_error_log_path();
  if( !g_winsparkle_error_log_path[0] )
    return;

  EnterCriticalSection( &g_winsparkle_log_lock );

  f = fopen( g_winsparkle_error_log_path, "a" );
  if( f ) {
    GetLocalTime( &st );
    fprintf( f,
             "%04u-%02u-%02u %02u:%02u:%02u WinSparkle: %s\n",
             (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
             (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
             event );
    fprintf( f, "  app version: %s\n", VERSION );
    fprintf( f, "  appcast: %s\n", WINSPARKLE_APPCAST_URL );
    fprintf( f,
             "  expected installer: "
             "https://github.com/%s/releases/download/%s/"
             "fusex-%s-win32-setup.exe\n",
             WINSPARKLE_GITHUB_RELEASES_REPO, VERSION, VERSION );
    fputc( '\n', f );
    fclose( f );
  }

  LeaveCriticalSection( &g_winsparkle_log_lock );
}

static void __cdecl
win32_winsparkle_error_callback( void )
{
  win32_winsparkle_log_event(
    "update check failed (bad appcast, missing installer, or network error)" );
}

static int __cdecl
win32_winsparkle_can_shutdown_callback( void )
{
  return 1;
}

static void __cdecl
win32_winsparkle_shutdown_request_callback( void )
{
  if( fuse_hWnd && IsWindow( fuse_hWnd ) )
    PostMessage( fuse_hWnd, WM_USER_WINSPARKLE_QUIT, 0, 0 );
}

void
win32_winsparkle_init( void )
{
  if( g_winsparkle_initialized )
    return;

  if( !g_winsparkle_log_lock_inited ) {
    InitializeCriticalSection( &g_winsparkle_log_lock );
    g_winsparkle_log_lock_inited = 1;
  }

  win32_winsparkle_set_error_log_path();
  win_sparkle_set_appcast_url( WINSPARKLE_APPCAST_URL );
  win_sparkle_set_error_callback( win32_winsparkle_error_callback );
  win_sparkle_set_can_shutdown_callback( win32_winsparkle_can_shutdown_callback );
  win_sparkle_set_shutdown_request_callback(
    win32_winsparkle_shutdown_request_callback );
  win_sparkle_init();

  g_winsparkle_initialized = 1;
}

void
win32_winsparkle_cleanup( void )
{
  if( !g_winsparkle_initialized )
    return;

  win_sparkle_cleanup();
  g_winsparkle_initialized = 0;

  if( g_winsparkle_log_lock_inited ) {
    DeleteCriticalSection( &g_winsparkle_log_lock );
    g_winsparkle_log_lock_inited = 0;
  }
}

MENU_CALLBACK( menu_help_check_for_updates )
{
  (void)action;

  if( !g_winsparkle_initialized )
    win32_winsparkle_init();

  win_sparkle_check_update_with_ui();
}
