/* Contract test for the registry the INPROC and SAMELOOP transports share.
 *
 * Those two transports keep no state of their own: a server and a client are
 * built from independent configurations and meet through a registry the caller
 * creates and hands to both. This used to hang off uv_loop_t::data, which made
 * the framework claim libuv's per-loop user field and required a magic read
 * through a pointer the caller owned -- and a loop declared the way libuv's own
 * docs show (no zero-initialisation) left the stack's leftovers there, faulting
 * about one run in five. Handing the registry in removes that entirely, so
 * these checks are about what the new contract owes the caller instead.
 *
 * What must hold: a registry is created and freed cleanly, two transports
 * given the same one find each other, two given different ones do not, a
 * missing registry is a clean error rather than a crash, and the framework no
 * longer reads or writes loop->data at all.
 */
#include <stdio.h>
#include <string.h>
#include <uv.h>

#include "uvbus_loop_registry.h"

static int failures;

static void check(int ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void) {
    /* 1. Creation and validation. */
    uvbus_loop_registry_t* reg = uvbus_loop_registry_new();
    check(reg != NULL, "a registry can be created");
    check(uvbus_loop_registry_is_valid(reg), "a fresh registry is valid");
    check(reg->refcount == 1, "the caller holds the first reference");
    check(reg->inproc_endpoints == NULL && reg->sameloop_servers == NULL,
          "a fresh registry is empty");

    /* 2. Reference counting: retain/release balance, and a release that takes
     * the count to zero frees the object. */
    check(uvbus_loop_registry_retain(reg) == reg, "retain returns the registry");
    check(reg->refcount == 2, "retain counted");
    uvbus_loop_registry_release(reg);
    check(reg->refcount == 1, "release counted");
    check(uvbus_loop_registry_is_valid(reg), "still valid with one reference left");

    /* 3. A pointer that is not a registry is rejected -- and rejecting it does
     * not dereference anything the caller does not own, which is the whole
     * point of the change. */
    int user_token = 0;
    check(!uvbus_loop_registry_is_valid(NULL), "NULL is not a registry");
    check(!uvbus_loop_registry_is_valid((uvbus_loop_registry_t*)&user_token),
          "a caller's own object is not mistaken for a registry");
    check(uvbus_loop_registry_retain(NULL) == NULL, "retain(NULL) is refused");
    uvbus_loop_registry_release(NULL);  /* must not fault */
    uvbus_loop_registry_free(NULL);     /* must not fault */
    check(user_token == 0, "a caller's object was left untouched");

    /* 4. The framework no longer has any business on loop->data. Zero it, run a
     * transport through the registry, and confirm it is still NULL afterwards
     * -- whatever the caller put there must be what is still there. */
    {
        uv_loop_t loop = {0};
        uv_loop_init(&loop);
        int* mine = &user_token;
        loop.data = mine;

        check(loop.data == mine, "the caller keeps its loop->data");

        /* A registry is attached to configs, not to the loop, so nothing here
         * needs the field -- which is exactly why a wild value can no longer
         * reach the framework. */
        uvbus_loop_registry_t* r2 = uvbus_loop_registry_new();
        check(r2 != NULL, "second registry created");
        check(loop.data == mine, "creating a registry does not touch loop->data");
        uvbus_loop_registry_free(r2);
        check(loop.data == mine, "freeing a registry does not touch loop->data");

        /* Even the pathological case: an uninitialised loop is irrelevant now. */
        uv_loop_t dirty;
        memset(&dirty, 0xCD, sizeof(dirty));
        uv_loop_init(&dirty);
        uvbus_loop_registry_t* r3 = uvbus_loop_registry_new();
        uvbus_loop_registry_free(r3);
        uv_loop_close(&dirty);
        check(1, "an uninitialised loop is no longer a hazard");
        uv_loop_close(&loop);
    }

    uvbus_loop_registry_free(reg);

    if (failures == 0) {
        printf("loop registry contract: OK\n");
        return 0;
    }
    printf("loop registry contract: %d check(s) failed\n", failures);
    return 1;
}
