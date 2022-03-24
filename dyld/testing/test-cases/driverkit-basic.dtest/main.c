// BUILD:  $DKCC dext.c -o $BUILD_DIR/dext.exe -Wl,-client_name,DriverKit -lSystem
// BUILD:  $CC main.c -o $BUILD_DIR/driverkit-basic.exe -DRUN_DIR="$RUN_DIR"
// BUILD:  $DEXT_SPAWN_ENABLE $BUILD_DIR/driverkit-basic.exe 

// RUN:  $SUDO ./driverkit-basic.exe

#include <sandbox.h>
#include <spawn.h>
#include <spawn_private.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/spawn_internal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test_support.h"

static void spawn_dext(const char* dextPath, char* const env[])
{
    posix_spawnattr_t attrs;
    if ( posix_spawnattr_init(&attrs) != 0 )
        FAIL("posix_spawnattr_init failed");
    if ( posix_spawnattr_setprocesstype_np(&attrs, POSIX_SPAWN_PROC_TYPE_DRIVER) != 0 )
        FAIL("posix_spawnattr_setprocesstype_np failed");
    
    struct sandbox_spawnattrs sbattrs;
    sandbox_spawnattrs_init(&sbattrs);
    if ( sandbox_spawnattrs_setprofilename(&sbattrs, "com.apple.dext") != 0)
        FAIL("sandbox_spawnattrs_setprofilename failed");
    if ( posix_spawnattr_setmacpolicyinfo_np(&attrs, "Sandbox", &sbattrs, sizeof(sbattrs)) != 0)
        FAIL("posix_spawnattr_setmacpolicyinfo_np failed");

    pid_t pid;
    const char* args[] = {dextPath, NULL};
    int err = posix_spawn(&pid, args[0], NULL, &attrs,  (char *const *)args, env);
    if ( err != 0 )
        FAIL("posix_spawn failed: %s %s", strerror(err), dextPath);

    int status;
    if ( waitpid(pid, &status, 0) == -1 )
        FAIL("waitpid failed");

    if ( WIFSIGNALED(status) ) {
        if ( !WTERMSIG(status) )
            FAIL("WTERMSIG failed");
        FAIL("dext received signal %d", status);
    }

    if ( !WIFEXITED(status) ) {
        FAIL("dext did not exit");
    }

    err = WEXITSTATUS(status);
    if ( err != 24 )
        FAIL("dext exit with code %d", err);

    if ( posix_spawnattr_destroy(&attrs) == -1 )
        FAIL("posix_spawnattr_destroy failed");
}

int main(int argc, const char* argv[], char *env[])
{
    spawn_dext(RUN_DIR "/dext.exe", env);
    PASS("Success");
    return 0;
}

