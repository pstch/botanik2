#include "vendor/doctest.h"

#include "libbotanik.h"
#include "sites/gh_mlb41/site.h"

using namespace Botanik;

namespace {

    struct Row { const char* name; unsigned bytes; };

    /** @brief What each long-lived object costs on THIS build, largest first.
     *  The numbers are pinned in libbotanik_sizes.h; this only shows them. */
    const Row g_sizes[] = {
        { "GreenhouseMLB41 (site firmware)", (unsigned)sizeof(GreenhouseMLB41)      },
        { "BotanikFirmware (base)",          (unsigned)sizeof(BotanikFirmware)      },
        { "Pages::LogsPage",                 (unsigned)sizeof(Pages::LogsPage)      },
        { "Pages::ShellPage",                (unsigned)sizeof(Pages::ShellPage)     },
        { "Vars::Bank",                      (unsigned)sizeof(Vars::Bank)           },
        { "Terminal::Console",               (unsigned)sizeof(Terminal::Console)    },
        { "Terminal::AnsiTerminal",          (unsigned)sizeof(Terminal::AnsiTerminal) },
        { "Utils::BufferedSerial",           (unsigned)sizeof(Utils::BufferedSerial)  },
        { "Tasks::Registry",                 (unsigned)sizeof(Tasks::Registry)      },
        { "Health::HealthMonitor",           (unsigned)sizeof(Health::HealthMonitor)  },
        { "Alarms::AlarmController",         (unsigned)sizeof(Alarms::AlarmController) },
        { "Modbus::TcpSlave",                (unsigned)sizeof(Modbus::TcpSlave)     },
        { "Modbus::RtuClient",               (unsigned)sizeof(Modbus::RtuClient)    },
        { "Vars::ConfigBinder",              (unsigned)sizeof(Vars::ConfigBinder)   },
        { "Scheduler::LoopScheduler",        (unsigned)sizeof(Scheduler::LoopScheduler) },
        { "Logs::Logger",                    (unsigned)sizeof(Logs::Logger)         },
        { "Terminal::TextEditor",            (unsigned)sizeof(Terminal::TextEditor) },
    };

}

TEST_CASE("sizes: the footprint table, and the site firmware within its budget") {
    unsigned total = 0;
    for (const Row& r : g_sizes) {
        // MESSAGE rather than CHECK: the pinning is libbotanik_sizes.h's job and
        // it has already failed the COMPILE if a number moved. This is the table
        // `botanik_tests -s` prints.
        MESSAGE(doctest::String(r.name) << ": " << r.bytes << " B");
        total += r.bytes;
    }
    MESSAGE("(the site firmware contains most of the rest; they do not sum)");

    // The site firmware is the object that actually lands in .bss, and it is
    // what scripts/footprint.py reports as the largest static object on the
    // OPTA. A ceiling here catches the growth on the HOST build too, where the
    // gate runs on every push and nobody is looking at a linker map.
    CHECK(sizeof(GreenhouseMLB41) <= 256u * 1024u);
    CHECK(sizeof(GreenhouseMLB41) >= sizeof(BotanikFirmware));
    CHECK(total > 0);
}
