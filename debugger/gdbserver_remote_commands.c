#include "gdbserver_remote_commands.h"
#include "gdbserver.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include "fuse.h"
#include "libspectrum.h"
#include "peripherals/spectranet.h"
#include "snapshot.h"
#include "utils.h"

static const char *
skip_spaces( const char *text )
{
    while( text && *text && isspace( (unsigned char)*text ) ) text++;
    return text;
}

static uint8_t remote_command_help( const char *args GCC_UNUSED )
{
    const struct remote_command_entry_t *entry;

    gdbserver_send_remote_console_output("Supported commands:\n");

    for (entry = remote_commands; entry->name; entry++) {
        gdbserver_send_remote_console_output(entry->name);
        gdbserver_send_remote_console_output("\n");
    }

    return 0;
}

static uint8_t remote_command_reset( const char *args GCC_UNUSED )
{
    return gdbserver_reset_via_remote_command();
}

static int
write_sna_snapshot( const char *filename )
{
    libspectrum_snap *snap;
    libspectrum_byte *buffer = NULL;
    size_t length = 0;
    int flags = 0;
    int error;

    snap = libspectrum_snap_alloc();
    if( !snap ) return 1;

    error = snapshot_copy_to( snap );
    if( error ) { libspectrum_snap_free( snap ); return error; }

    error = libspectrum_snap_write( &buffer, &length, &flags, snap,
                                    LIBSPECTRUM_ID_SNAPSHOT_SNA,
                                    fuse_creator, 0 );
    libspectrum_snap_free( snap );
    if( error ) return error;

    error = utils_write_file( filename, buffer, length );
    libspectrum_free( buffer );

    return error;
}

static uint8_t remote_command_dump( const char *args )
{
    char type[32];
    const char *filename;
    size_t type_len;

    args = skip_spaces( args );
    if( !args || !*args ) {
        gdbserver_send_remote_console_output(
            "Usage: dump <sna|spectranet-ram> <file>\n" );
        return 1;
    }

    filename = args;
    while( *filename && !isspace( (unsigned char)*filename ) ) filename++;

    type_len = filename - args;
    if( type_len == 0 || type_len >= sizeof( type ) ) return 1;

    memcpy( type, args, type_len );
    type[type_len] = '\0';

    filename = skip_spaces( filename );
    if( !filename || !*filename ) {
        gdbserver_send_remote_console_output(
            "Usage: dump <sna|spectranet-ram> <file>\n" );
        return 1;
    }

    if( !strcmp( type, "sna" ) ) {
        return write_sna_snapshot( filename ) ? 1 : 0;
    }

    if( !strcmp( type, "spectranet-ram" ) ) {
        return spectranet_dump_ram( filename ) ? 1 : 0;
    }

    gdbserver_send_remote_console_output(
        "Unsupported dump type. Supported: sna, spectranet-ram\n" );
    return 1;
}

const struct remote_command_entry_t remote_commands[] = {
    { "help", remote_command_help },
    { "reset", remote_command_reset },
    { "dump", remote_command_dump },
    { NULL, NULL }
};
