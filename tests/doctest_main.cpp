/**
 * @file doctest_main.cpp
 * @brief Entry point for the doctest unit-test binary.
 *
 * The single TU that compiles doctest's implementation and provides @c main().
 * Keep it free of test cases: a change to a suite would otherwise recompile the
 * whole framework implementation.
 *
 * @see @ref cpptests for the suite overview and how to run it.
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

int main(int argc, char** argv) {
    // The suite builds schedulers it never kicks, and an armed watchdog would
    // re-exec the test binary out from under doctest. The sim's own default is
    // ON: a running unit wants the real thing.
    setenv("BOTANIK_WATCHDOG", "0", /*overwrite=*/0);

    // The parameter store is a FILE, and its default is $HOME/.botanik_config -
    // the very one the developer's own botanik_sim writes. Cases that bring a
    // bank up would then start from whatever was last saved there, so the suite
    // gets a store of its own. Per-PID because ctest runs the per-case entries
    // concurrently; removed at both ends because a run that died still holds a
    // file, and a PID comes round again.
    char store[64] = { 0 };
    const char* given = getenv("BOTANIK_CONFIG_STORE");
    if (given == nullptr || given[0] == 0) {
        snprintf(store, sizeof(store), "/tmp/botanik_test_store.%ld", (long)getpid());
        setenv("BOTANIK_CONFIG_STORE", store, /*overwrite=*/1);
        remove(store);
    }

    const int rc = doctest::Context(argc, argv).run();
    if (store[0] != 0) remove(store);
    return rc;
}
