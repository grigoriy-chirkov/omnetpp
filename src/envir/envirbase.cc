//==========================================================================
//  ENVIRBASE.CC - part of
//                     OMNeT++/OMNEST
//             Discrete System Simulation in C++
//
//  Author: Andras Varga
//
//==========================================================================

/*--------------------------------------------------------------*
  Copyright (C) 1992-2008 Andras Varga
  Copyright (C) 2006-2008 OpenSim Ltd.

  This file is distributed WITHOUT ANY WARRANTY. See the file
  `license' for details on this and other legal matters.
*--------------------------------------------------------------*/

#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <fstream>
#include <set>
#include <algorithm>

#include "args.h"
#include "envirbase.h"
#include "appreg.h"
#include "ccoroutine.h"
#include "csimulation.h"
#include "cscheduler.h"
#include "cpar.h"
#include "cproperties.h"
#include "cproperty.h"
#include "random.h"
#include "crng.h"
#include "cmodule.h"
#include "cmessage.h"
#include "ccomponenttype.h"
#include "cxmlelement.h"
#include "cxmldoccache.h"
#include "chistogram.h"
#include "stringtokenizer.h"
#include "fnamelisttokenizer.h"
#include "chasher.h"
#include "cconfigoption.h"
#include "cnedmathfunction.h"
#include "cnedfunction.h"
#include "../nedxml/nedparser.h" // NEDParser::getBuiltInDeclarations()
#include "regmacros.h"
#include "stringutil.h"
#include "enumstr.h"
#include "simtime.h"
#include "cresultrecorder.h"
#include "resultfilters.h"
#include "statisticparser.h"
#include "httpserver.h"
#include "mongoosehttpserver.h" //XXX not needed if httpserver-class option is implemented

#ifdef WITH_PARSIM
#include "cparsimcomm.h"
#include "parsim/cparsimpartition.h"
#include "parsim/cparsimsynchr.h"
#include "parsim/creceivedexception.h"
#endif

#include "opp_ctype.h"
#include "stringtokenizer.h"
#include "fileglobber.h"
#include "unitconversion.h"
#include "commonutil.h"
#include "../common/ver.h"

#include "timeutil.h"
#include "platmisc.h"
#include "fileutil.h"  // splitFileName

#ifdef USE_PORTABLE_COROUTINES /* coroutine stacks reside in main stack area */

# define TOTAL_STACK_SIZE   (2*1024*1024)
# define MAIN_STACK_SIZE       (128*1024)  // Tkenv needs more than 64K

#else /* nonportable coroutines, stacks are allocated on heap */

# define TOTAL_STACK_SIZE        0  // dummy value
# define MAIN_STACK_SIZE         0  // dummy value

#endif

#if defined _WIN32
#define DEFAULT_DEBUGGER_COMMAND "start gdb --pid=%u"
#elif defined __APPLE__
#define DEFAULT_DEBUGGER_COMMAND "xterm -e 'gdb --pid=%u' &"  // is it possible to have XCode launched and attached to us?
#else /* Linux, *BSD and other: assume Nemiver is available and installed */
#define DEFAULT_DEBUGGER_COMMAND "nemiver --attach=%u &"
#endif

USING_NAMESPACE

using std::ostream;

#define CREATE_BY_CLASSNAME(var,classname,baseclass,description) \
     baseclass *var ## _tmp = (baseclass *) createOne(classname); \
     var = dynamic_cast<baseclass *>(var ## _tmp); \
     if (!var) \
         throw cRuntimeError("Class \"%s\" is not subclassed from " #baseclass, (const char *)classname);

Register_GlobalConfigOptionU(CFGID_TOTAL_STACK, "total-stack", "B", NULL, "Specifies the maximum memory for activity() simple module stacks. You need to increase this value if you get a ``Cannot allocate coroutine stack'' error.");
Register_GlobalConfigOption(CFGID_PARALLEL_SIMULATION, "parallel-simulation", CFG_BOOL, "false", "Enables parallel distributed simulation.");
Register_GlobalConfigOption(CFGID_SCHEDULER_CLASS, "scheduler-class", CFG_STRING, "cSequentialScheduler", "Part of the Envir plugin mechanism: selects the scheduler class. This plugin interface allows for implementing real-time, hardware-in-the-loop, distributed and distributed parallel simulation. The class has to implement the cScheduler interface.");
Register_GlobalConfigOption(CFGID_PARSIM_COMMUNICATIONS_CLASS, "parsim-communications-class", CFG_STRING, "cFileCommunications", "If parallel-simulation=true, it selects the class that implements communication between partitions. The class must implement the cParsimCommunications interface.");
Register_GlobalConfigOption(CFGID_PARSIM_SYNCHRONIZATION_CLASS, "parsim-synchronization-class", CFG_STRING, "cNullMessageProtocol", "If parallel-simulation=true, it selects the parallel simulation algorithm. The class must implement the cParsimSynchronizer interface.");
Register_GlobalConfigOption(CFGID_OUTPUTVECTORMANAGER_CLASS, "outputvectormanager-class", CFG_STRING, "cIndexedFileOutputVectorManager", "Part of the Envir plugin mechanism: selects the output vector manager class to be used to record data from output vectors. The class has to implement the cIOutputVectorManager interface.");
Register_GlobalConfigOption(CFGID_OUTPUTSCALARMANAGER_CLASS, "outputscalarmanager-class", CFG_STRING, "cFileOutputScalarManager", "Part of the Envir plugin mechanism: selects the output scalar manager class to be used to record data passed to recordScalar(). The class has to implement the cIOutputScalarManager interface.");
Register_GlobalConfigOption(CFGID_SNAPSHOTMANAGER_CLASS, "snapshotmanager-class", CFG_STRING, "cFileSnapshotManager", "Part of the Envir plugin mechanism: selects the class to handle streams to which snapshot() writes its output. The class has to implement the cISnapshotManager interface.");
Register_GlobalConfigOption(CFGID_FNAME_APPEND_HOST, "fname-append-host", CFG_BOOL, NULL, "Turning it on will cause the host name and process Id to be appended to the names of output files (e.g. omnetpp.vec, omnetpp.sca). This is especially useful with distributed simulation. The default value is true if parallel simulation is enabled, false otherwise.");
Register_GlobalConfigOption(CFGID_DEBUG_ON_ERRORS, "debug-on-errors", CFG_BOOL, "false", "When set to true, runtime errors will cause the simulation program to break into the C++ debugger (if the simulation is running under one, or just-in-time debugging is activated). Once in the debugger, you can view the stack trace or examine variables.");
Register_GlobalConfigOption(CFGID_PRINT_UNDISPOSED, "print-undisposed", CFG_BOOL, "true", "Whether to report objects left (that is, not deallocated by simple module destructors) after network cleanup.");
Register_GlobalConfigOption(CFGID_SIMTIME_SCALE, "simtime-scale", CFG_INT, "-12", "Sets the scale exponent, and thus the resolution of time for the 64-bit fixed-point simulation time representation. Accepted values are -18..0; for example, -6 selects microsecond resolution. -12 means picosecond resolution, with a maximum simtime of ~110 days.");
Register_GlobalConfigOption(CFGID_NED_PATH, "ned-path", CFG_PATH, "", "A semicolon-separated list of directories. The directories will be regarded as roots of the NED package hierarchy, and all NED files will be loaded from their subdirectory trees. This option is normally left empty, as the OMNeT++ IDE sets the NED path automatically, and for simulations started outside the IDE it is more convenient to specify it via a command-line option or the NEDPATH environment variable.");
Register_GlobalConfigOption(CFGID_DEBUGGER_ATTACH_ON_STARTUP, "debugger-attach-on-startup", CFG_BOOL, "false", "When set to true, the simulation program will launch an external debugger attached to it, allowing you to set breakpoints before proceeding. The debugger command is configurable.");
Register_GlobalConfigOption(CFGID_DEBUGGER_ATTACH_ON_ERROR, "debugger-attach-on-error", CFG_BOOL, "false", "When set to true, runtime errors and crashes will trigger an external debugger to be launched, allowing you to do just-in-time debugging on the simulation process.");
Register_GlobalConfigOption(CFGID_DEBUGGER_ATTACH_COMMAND, "debugger-attach-command", CFG_STRING, DEFAULT_DEBUGGER_COMMAND, "Command line to launch the debugger. It must contain exactly one percent sign, as '%u', which will be replaced by the PID of this process. The command must not block (i.e. it should end in '&' on Unix-like systems).");
Register_GlobalConfigOptionU(CFGID_DEBUGGER_ATTACH_WAIT_TIME, "debugger-attach-wait-time", "s", "20s", "An interval to wait after launching the external debugger, to give the debugger time to start up and attach to the simulation process.");

Register_PerRunConfigOption(CFGID_NETWORK, "network", CFG_STRING, NULL, "The name of the network to be simulated.  The package name can be omitted if the ini file is in the same directory as the NED file that contains the network.");
Register_PerRunConfigOption(CFGID_WARNINGS, "warnings", CFG_BOOL, "true", "Enables warnings.");
Register_PerRunConfigOptionU(CFGID_SIM_TIME_LIMIT, "sim-time-limit", "s", NULL, "Stops the simulation when simulation time reaches the given limit. The default is no limit.");
Register_PerRunConfigOptionU(CFGID_CPU_TIME_LIMIT, "cpu-time-limit", "s", NULL, "Stops the simulation when CPU usage has reached the given limit. The default is no limit.");
Register_PerRunConfigOptionU(CFGID_WARMUP_PERIOD, "warmup-period", "s", NULL, "Length of the initial warm-up period. When set, results belonging to the first x seconds of the simulation will not be recorded into output vectors, and will not be counted into output scalars (see option **.result-recording-modes). This option is useful for steady-state simulations. The default is 0s (no warmup period). Note that models that compute and record scalar results manually (via recordScalar()) will not automatically obey this setting.");
Register_PerRunConfigOption(CFGID_FINGERPRINT, "fingerprint", CFG_STRING, NULL, "The expected fingerprint of the simulation. When provided, a fingerprint will be calculated from the simulation event times and other quantities during simulation, and checked against the given one. Fingerprints are suitable for crude regression tests. As fingerprints occasionally differ across platforms, more than one fingerprint values can be specified here, separated by spaces, and a match with any of them will be accepted. To obtain the initial fingerprint, enter a dummy value such as \"0000\"), and run the simulation.");
Register_PerRunConfigOption(CFGID_NUM_RNGS, "num-rngs", CFG_INT, "1", "The number of random number generators.");
Register_PerRunConfigOption(CFGID_RNG_CLASS, "rng-class", CFG_STRING, "cMersenneTwister", "The random number generator class to be used. It can be `cMersenneTwister', `cLCG32', `cAkaroaRNG', or you can use your own RNG class (it must be subclassed from cRNG).");
Register_PerRunConfigOption(CFGID_SEED_SET, "seed-set", CFG_INT, "${runnumber}", "Selects the kth set of automatic random number seeds for the simulation. Meaningful values include ${repetition} which is the repeat loop counter (see repeat= key), and ${runnumber}.");
Register_PerRunConfigOption(CFGID_RESULT_DIR, "result-dir", CFG_STRING, "results", "Value for the ${resultdir} variable, which is used as the default directory for result files (output vector file, output scalar file, eventlog file, etc.)");
Register_PerRunConfigOption(CFGID_RECORD_EVENTLOG, "record-eventlog", CFG_BOOL, "false", "Enables recording an eventlog file, which can be later visualized on a sequence chart. See eventlog-file= option too.");
Register_PerRunConfigOption(CFGID_DEBUG_STATISTICS_RECORDING, "debug-statistics-recording", CFG_BOOL, "false", "Turns on the printing of debugging information related to statistics recording (@statistic properties)");
Register_PerRunConfigOption(CFGID_LOG_FORMAT, "log-format", CFG_STRING, "[%l]\t", "Specifies the format string that determines the prefix of each recorded log line. Log lines can be produced by using any of the predefined C++ macros such as FATAL, ERROR, WARN, INFO, DEBUG or TRACE. The format string may contain constant texts interleaved with format directives. A format directive is a '%' character followed by a single format character. See the manual for the list of available format characters.")
Register_PerRunConfigOption(CFGID_GLOBAL_LOGLEVEL, "log-level", CFG_STRING, "DEBUG", "Globally specifies the level of detail recorded by log statements, output below the specified level is omitted. Available values are (case insensitive): fatal, error, warn, info, debug or trace. Note that the level of detail is also controlled by the specified per component log levels and the GLOBAL_COMPILETIME_LOGLEVEL macro that is used to completely remove log statements from the executable.")
Register_PerRunConfigOption(CFGID_CHECK_SIGNALS, "check-signals", CFG_BOOL, "false", "Controls whether the simulation kernel will validate signals emitted by modules and channels against signal declarations (@signal properties) in NED files.");  //TODO for 5.0, add: "The default setting depends on the build type: 'true' in DEBUG, and 'false' in RELEASE mode."

Register_PerObjectConfigOption(CFGID_PARTITION_ID, "partition-id", KIND_MODULE, CFG_STRING, NULL, "With parallel simulation: in which partition the module should be instantiated. Specify numeric partition ID, or a comma-separated list of partition IDs for compound modules that span across multiple partitions. Ranges (\"5..9\") and \"*\" (=all) are accepted too.");
Register_PerObjectConfigOption(CFGID_RNG_K, "rng-%", KIND_MODULE, CFG_INT, "", "Maps a module-local RNG to one of the global RNGs. Example: **.gen.rng-1=3 maps the local RNG 1 of modules matching `**.gen' to the global RNG 3. The default is one-to-one mapping.");
Register_PerObjectConfigOption(CFGID_RESULT_RECORDING_MODES, "result-recording-modes", KIND_STATISTIC, CFG_STRING, "default", "Defines how to calculate results from the @statistic property matched by the wildcard. Special values: default, all: they select the modes listed in the record= key of @statistic; all selects all of them, default selects the non-optional ones (i.e. excludes the ones that end in a question mark). Example values: vector, count, last, sum, mean, min, max, timeavg, stats, histogram. More than one values are accepted, separated by commas. Expressions are allowed. Items prefixed with '-' get removed from the list. Example: **.queueLength.result-recording-modes=default,-vector,+timeavg");

// the following options are declared in other files
extern cConfigOption *CFGID_SCALAR_RECORDING;
extern cConfigOption *CFGID_VECTOR_RECORDING;


#define STRINGIZE0(x) #x
#define STRINGIZE(x) STRINGIZE0(x)

static const char *compilerInfo =
    #if defined __GNUC__
    "GCC " __VERSION__
    #elif defined _MSC_VER
    "Microsoft Visual C++ version " STRINGIZE(_MSC_VER)
    #elif defined __INTEL_COMPILER
    "Intel Compiler version " STRINGIZE(__INTEL_COMPILER)
    #else
    "unknown-compiler"
    #endif
    ;

static const char *buildInfoFormat =
    "%d-bit"

    #ifdef NDEBUG
    " RELEASE"
    #else
    " DEBUG"
    #endif

    " simtime_t=%s"
    " large-file-support=%s"
    ;

static const char *buildOptions = ""
    #ifdef USE_DOUBLE_SIMTIME
    " USE_DOUBLE_SIMTIME"
    #endif

    #ifdef WITH_PARSIM
    " WITH_PARSIM"
    #endif

    #ifdef WITH_MPI
    " WITH_MPI"
    #endif

    #ifdef WITH_NETBUILDER
    " WITH_NETBUILDER"
    #endif

    #ifdef WITH_AKAROA
    " WITH_AKAROA"
    #endif

    #ifdef XMLPARSER
    " XMLPARSER=" STRINGIZE(XMLPARSER)
    #endif
    ;


EnvirBase::EnvirBase()
{
    args = NULL;
    cfg = NULL;
    xmlcache = NULL;

    record_eventlog = false;
    eventlogmgr = NULL;
    outvectormgr = NULL;
    outscalarmgr = NULL;
    snapshotmgr = NULL;

    num_rngs = 0;
    rngs = NULL;

#ifdef WITH_PARSIM
    parsimcomm = NULL;
    parsimpartition = NULL;
#endif

    httpServer = NULL;

    exitcode = 0;
}

EnvirBase::~EnvirBase()
{
    while (!listeners.empty()) {
        listeners.back()->listenerRemoved();
        listeners.pop_back();
    }

    delete args;
    delete cfg;
    delete xmlcache;

    delete eventlogmgr;
    delete outvectormgr;
    delete outscalarmgr;
    delete snapshotmgr;

    for (int i = 0; i < num_rngs; i++)
         delete rngs[i];
    delete [] rngs;

#ifdef WITH_PARSIM
    delete parsimcomm;
    delete parsimpartition;
#endif
}

int EnvirBase::run(int argc, char *argv[], cConfiguration *configobject)
{
    args = new ArgList();
    args->parse(argc, argv, "h?f:u:l:c:r:p:n:x:X:agGvw");  //TODO share spec with startup.cc!
    cfg = dynamic_cast<cConfigurationEx *>(configobject);
    if (!cfg)
        throw cRuntimeError("Cannot cast configuration object %s to cConfigurationEx", configobject->getClassName());
    if (cfg->getAsBool(CFGID_DEBUGGER_ATTACH_ON_STARTUP))
        attachDebugger();

    if (simulationRequired())
    {
        if (setup())
            run();   // must not throw, because we want shutdown() always to be called
        shutdown();
    }
    return exitcode;
}

bool EnvirBase::simulationRequired()
{
    // handle -h and -v command-line options
    if (args->optionGiven('h'))
    {
        const char *category = args->optionValue('h',0);
        if (!category)
            printHelp();
        else
            dumpComponentList(category);
        return false;
    }

    if (args->optionGiven('v'))
    {
        struct opp_stat_t statbuf;
        std::cout << "\n";
        std::cout << "Build: " OMNETPP_RELEASE " " OMNETPP_BUILDID << "\n";
        std::cout << "Compiler: " << compilerInfo << "\n";
        std::cout << "Options: " << opp_stringf(buildInfoFormat,
                                         8*sizeof(void*),
                                         opp_typename(typeid(simtime_t)),
                                         sizeof(statbuf.st_size)>=8 ? "yes" : "no");
        std::cout << buildOptions << "\n";
        return false;
    }

    cConfigurationEx *cfg = getConfigEx();

    // -a option: print all config names, and number of runs in them
    if (args->optionGiven('a'))
    {
        std::cout << "\n";
        std::vector<std::string> configNames = cfg->getConfigNames();
        for (int i=0; i<(int)configNames.size(); i++)
            std::cout << "Config " << configNames[i] << ": " << cfg->getNumRunsInConfig(configNames[i].c_str()) << "\n";
        return false;
    }

    // '-x' option: print number of runs in the given config, and exit (overrides configname)
    const char *configToPrint = args->optionValue('x');
    if (configToPrint)
    {
        //
        // IMPORTANT: the simulation launcher will parse the output of this
        // option, so it should be modified with care and the two kept in sync
        // (see OmnetppLaunchUtils.getSimulationRunInfo()).
        //
        // Rules:
        // (1) the number of runs should appear on the rest of the line
        //     after the "Number of runs:" text
        // (2) per-run information lines should span from the "Number of runs:"
        //     line until the next blank line ("\n\n").
        //

        // '-g'/'-G' options: modifies -x: print unrolled config, iteration variables, etc as well
        bool unrollBrief = args->optionGiven('g');
        bool unrollDetailed = args->optionGiven('G');

        std::cout <<"\n";
        std::cout <<"Config: " << configToPrint << "\n";
        std::cout <<"Number of runs: " << cfg->getNumRunsInConfig(configToPrint) << "\n";

        if (unrollBrief || unrollDetailed)
        {
            std::vector<std::string> runs = cfg->unrollConfig(configToPrint, unrollDetailed);
            const char *fmt = unrollDetailed ? "Run %d:\n%s" : "Run %d: %s\n";
            for (int i=0; i<(int)runs.size(); i++)
                std::cout << opp_stringf(fmt, i, runs[i].c_str());
        }
        return false;
    }

    // -X option: print fallback chain of the given config, and exit
    configToPrint = args->optionValue('X');
    if (configToPrint)
    {
        std::cout << "\n";
        std::vector<std::string> configNames = cfg->getConfigChain(configToPrint);
        for (int i=0; i<(int)configNames.size(); i++) {
            std::string configName = configNames[i];
            if (configName != "General")
                std::cout << "Config ";
            std::cout << configName << "\n";
        }
        return false;
    }


    return true;
}

bool EnvirBase::setup()
{
    try
    {
        // ensure correct numeric format in output files
        setPosixLocale();

        // set opt_* variables from ini file(s)
        readOptions();

        if (getConfig()->getAsBool(CFGID_DEBUGGER_ATTACH_ON_ERROR))
        {
            signal(SIGSEGV, crashHandler);
            signal(SIGILL, crashHandler);
#ifndef _WIN32
            signal(SIGBUS, crashHandler);
            signal(SIGUSR1, crashHandler);
#endif
        }

        // initialize coroutine library
        if (TOTAL_STACK_SIZE!=0 && opt_total_stack<=MAIN_STACK_SIZE+4096)
        {
            std::cout << "Total stack size " << opt_total_stack << " increased to " << MAIN_STACK_SIZE << "\n";
            opt_total_stack = MAIN_STACK_SIZE+4096;
        }
        cCoroutine::init(opt_total_stack, MAIN_STACK_SIZE);

        // install XML document cache
        xmlcache = new cXMLDocCache();

        // configure log formatter
        const std::string& logFormat = getConfig()->getAsString(CFGID_LOG_FORMAT);
        logFormatUsesEventName = logFormat.find("%m") != std::string::npos || logFormat.find("%v") != std::string::npos;
        cLogLevel::globalRuntimeLoglevel = cLogLevel::getLevel(getConfig()->getAsString(CFGID_GLOBAL_LOGLEVEL).c_str());
        logFormatter.setFormat(logFormat.c_str());

        // install eventlog manager
        eventlogmgr = new EventlogFileManager();
        addListener(eventlogmgr);

        // install output vector manager
        CREATE_BY_CLASSNAME(outvectormgr, opt_outputvectormanager_class.c_str(), cIOutputVectorManager, "output vector manager");
        addListener(outvectormgr);

        // install output scalar manager
        CREATE_BY_CLASSNAME(outscalarmgr, opt_outputscalarmanager_class.c_str(), cIOutputScalarManager, "output scalar manager");
        addListener(outscalarmgr);

        // install snapshot manager
        CREATE_BY_CLASSNAME(snapshotmgr, opt_snapshotmanager_class.c_str(), cISnapshotManager, "snapshot manager");
        addListener(snapshotmgr);

        // set up for sequential or distributed execution
        if (!opt_parsim)
        {
            // sequential
            cScheduler *scheduler;
            CREATE_BY_CLASSNAME(scheduler, opt_scheduler_class.c_str(), cScheduler, "event scheduler");
            simulation.setScheduler(scheduler);
        }
        else
        {
#ifdef WITH_PARSIM
            // parsim: create components
            CREATE_BY_CLASSNAME(parsimcomm, opt_parsimcomm_class.c_str(), cParsimCommunications, "parallel simulation communications layer");
            parsimpartition = new cParsimPartition();
            cParsimSynchronizer *parsimsynchronizer;
            CREATE_BY_CLASSNAME(parsimsynchronizer, opt_parsimsynch_class.c_str(), cParsimSynchronizer, "parallel simulation synchronization layer");
            addListener(parsimpartition);

            // wire them together (note: 'parsimsynchronizer' is also the scheduler for 'simulation')
            parsimpartition->setContext(&simulation, parsimcomm, parsimsynchronizer);
            parsimsynchronizer->setContext(&simulation, parsimpartition, parsimcomm);
            simulation.setScheduler(parsimsynchronizer);

            // initialize them
            parsimcomm->init();
#else
            throw cRuntimeError("Parallel simulation is turned on in the ini file, but OMNeT++ was compiled without parallel simulation support (WITH_PARSIM=no)");
#endif
        }

        // load NED files from folders on the NED path. Note: NED path is taken
        // from the "-n" command-line option or the NEDPATH variable ("-n" takes
        // precedence), and the "ned-path=" config entry gets appended to it.
        // If the result is still empty, we fall back to "." -- this is needed
        // for single-directory models to work
        const char *nedpath1 = args->optionValue('n',0);
        if (!nedpath1)
            nedpath1 = getenv("NEDPATH");
        std::string nedpath2 = getConfig()->getAsPath(CFGID_NED_PATH);
        std::string nedpath = opp_join(";", nedpath1, nedpath2.c_str());
        if (nedpath.empty())
            nedpath = ".";

        StringTokenizer tokenizer(nedpath.c_str(), PATH_SEPARATOR);
        std::set<std::string> foldersloaded;
        while (tokenizer.hasMoreTokens())
        {
            const char *folder = tokenizer.nextToken();
            if (foldersloaded.find(folder)==foldersloaded.end())
            {
                std::cout << "Loading NED files from " << folder << ": ";
                int count = simulation.loadNedSourceFolder(folder);
                std::cout << " " << count << endl;
                foldersloaded.insert(folder);
            }
        }
        simulation.doneLoadingNedFiles();

        // initialize built-in web server
        const char *portSpec = args->optionValue('p',0);
        if (!portSpec)
            portSpec = "8000+"; // default value
        if (strcmp(portSpec, "-") != 0)
        {
            httpServer = new cMongooseHttpServer();
            httpServer->start(portSpec);
        }
        //FIXME TODO also shutdown

        // notify listeners when global setup is complete
        notifyListeners(LF_ON_STARTUP);
    }
    catch (std::exception& e)
    {
        displayException(e);
        exitcode = 1;
        return false; // don't run the app
    }
    return true;
}

void EnvirBase::printHelp()
{
    std::cout << "\n";
    std::cout << "Command line options:\n";
    std::cout << "  <inifile> or -f <inifile>\n";
    std::cout << "                Use the given ini file instead of omnetpp.ini. More than one\n";
    std::cout << "                ini files can be loaded this way.\n";
    std::cout << "  -u <ui>       Selects the user interface. Standard choices are Cmdenv\n";
    std::cout << "                and Tkenv. To make a user interface available, you need\n";
    std::cout << "                to link the simulation executable with the Cmdenv/Tkenv\n";
    std::cout << "                library, or load it as shared library via the -l option.\n";
    std::cout << "  -n <nedpath>  When present, overrides the NEDPATH environment variable.\n";
    std::cout << "  -l <library>  Load the specified shared library (.so or .dll) on startup.\n";
    std::cout << "                The file name should be given without the .so or .dll suffix\n";
    std::cout << "                (it will be appended automatically.) The loaded module may\n";
    std::cout << "                contain simple modules, plugins, etc. Multiple -l options\n";
    std::cout << "                can be present.\n";
    std::cout << "  -p <port>     Port number for the built-in web server.\n";
    std::cout << "                If the port is not available, the program will exit with an\n";
    std::cout << "                error message unless the number is suffixed with the plus\n";
    std::cout << "                sign. The plus sign causes the program to search for the\n";
    std::cout << "                first available port number above the given one, and not stop\n";
    std::cout << "                with an error even if no available port was found. A plain\n";
    std::cout << "                minus sign will turn off the built-in web server.\n";
    std::cout << "                The default value is \"8000+\".\n";
    std::cout << "  --<configuration-key>=<value>\n";
    std::cout << "                Any configuration option can be specified on the command\n";
    std::cout << "                line, and it takes precedence over settings specified in the\n";
    std::cout << "                ini file(s). Examples:\n";
    std::cout << "                      --debug-on-errors=true\n";
    std::cout << "                      --record-eventlog=true\n";
    std::cout << "                      --sim-time-limit=1000s\n";
    std::cout << "  -v            Print version and build info.\n";
    std::cout << "  -h            Print this help and exit.\n";
    std::cout << "  -h <category> Lists registered components:\n";
    std::cout << "    -h config         Prints the list of available config options\n";
    std::cout << "    -h configdetails  Prints the list of available config options, with\n";
    std::cout << "                      their documentation\n";
    std::cout << "    -h userinterfaces Lists available user interfaces (see -u option)\n";
    std::cout << "    -h classes        Lists registered C++ classes (including module classes)\n";
    std::cout << "    -h classdesc      Lists C++ classes that have associated reflection\n";
    std::cout << "                      information (needed for Tkenv inspectors)\n";
    std::cout << "    -h nedfunctions   Lists registered NED functions\n";
    std::cout << "    -h neddecls       Lists built-in NED component declarations\n";
    std::cout << "    -h units          Lists recognized physical units\n";
    std::cout << "    -h enums          Lists registered enums\n";
    std::cout << "    -h resultfilters  Lists result filters\n";
    std::cout << "    -h resultrecorders Lists result recorders\n";
    std::cout << "    -h all            Union of all the above\n";
    std::cout << "\n";

    // print specific help for each user interface
    cRegistrationList *table = omnetapps.getInstance();
    table->sort();
    for (int i=0; i<table->size(); i++)
    {
        // instantiate the ui, call printUISpecificHelp(), then dispose.
        // note: their ctors are not supposed to do anything but trivial member initializations
        cOmnetAppRegistration *appreg = dynamic_cast<cOmnetAppRegistration *>(table->get(i));
        ASSERT(appreg != NULL);
        cEnvir *app = appreg->createOne();
        if (dynamic_cast<EnvirBase *>(app))
            ((EnvirBase *)app)->printUISpecificHelp();
        delete app;
    }
}

void EnvirBase::dumpComponentList(const char *category)
{
    bool wantAll = !strcmp(category, "all");
    bool processed = false;
    if (wantAll || !strcmp(category, "config") || !strcmp(category, "configdetails"))
    {
        processed = true;
        std::cout << "Supported configuration options:\n";
        bool printDescriptions = strcmp(category, "configdetails")==0;

        cRegistrationList *table = configOptions.getInstance();
        table->sort();
        for (int i=0; i<table->size(); i++)
        {
            cConfigOption *obj = dynamic_cast<cConfigOption *>(table->get(i));
            ASSERT(obj);
            if (!printDescriptions) std::cout << "  ";
            if (obj->isPerObject()) std::cout << "<object-full-path>.";
            std::cout << obj->getName() << "=";
            std::cout << "<" << cConfigOption::getTypeName(obj->getType()) << ">";
            if (obj->getUnit())
                std::cout << ", unit=\"" << obj->getUnit() << "\"";
            if (obj->getDefaultValue())
                std::cout << ", default:" << obj->getDefaultValue() << "";
            std::cout << "; " << (obj->isGlobal() ? "global" : obj->isPerObject() ? "per-object" : "per-run") << " setting";
            std::cout << "\n";
            if (printDescriptions && !opp_isempty(obj->getDescription()))
                std::cout << opp_indentlines(opp_breaklines(obj->getDescription(),75).c_str(), "    ") << "\n";
            if (printDescriptions) std::cout << "\n";
        }
        std::cout << "\n";

        std::cout << "Predefined variables that can be used in config values:\n";
        std::vector<const char *> v = getConfigEx()->getPredefinedVariableNames();
        for (int i=0; i<(int)v.size(); i++)
        {
            if (!printDescriptions) std::cout << "  ";
            std::cout << "${" << v[i] << "}\n";
            const char *desc = getConfigEx()->getVariableDescription(v[i]);
            if (printDescriptions && !opp_isempty(desc))
                std::cout << opp_indentlines(opp_breaklines(desc,75).c_str(), "    ") << "\n";
        }
        std::cout << "\n";
    }
    if (!strcmp(category, "jconfig")) // internal undocumented option, for maintenance purposes
    {
        // generate Java code for ConfigurationRegistry.java in the IDE
        processed = true;
        std::cout << "Supported configuration options (as Java code):\n";
        cRegistrationList *table = configOptions.getInstance();
        table->sort();
        for (int i=0; i<table->size(); i++)
        {
            cConfigOption *key = dynamic_cast<cConfigOption *>(table->get(i));
            ASSERT(key);

            std::string id = "CFGID_";
            for (const char *s = key->getName(); *s; s++)
                id.append(1, opp_isalpha(*s) ? opp_toupper(*s) : *s=='-' ? '_' : *s=='%' ? 'n' : *s);
            const char *method = key->isGlobal() ? "addGlobalOption" :
                                 !key->isPerObject() ? "addPerRunOption" :
                                 "addPerObjectOption";
            #define CASE(X)  case cConfigOption::X: typestring = #X; break;
            const char *typestring;
            switch (key->getType()) {
                CASE(CFG_BOOL)
                CASE(CFG_INT)
                CASE(CFG_DOUBLE)
                CASE(CFG_STRING)
                CASE(CFG_FILENAME)
                CASE(CFG_FILENAMES)
                CASE(CFG_PATH)
                CASE(CFG_CUSTOM)
            }
            #undef CASE

            #define CASE(X)  case cConfigOption::X: kindstring = #X; break;
            const char *kindstring;
            switch (key->getObjectKind()) {
                CASE(KIND_MODULE)
                CASE(KIND_SIMPLE_MODULE)
                CASE(KIND_UNSPECIFIED_TYPE)
                CASE(KIND_PARAMETER)
                CASE(KIND_STATISTIC)
                CASE(KIND_SCALAR)
                CASE(KIND_VECTOR)
                CASE(KIND_NONE)
                CASE(KIND_OTHER)
            }
            #undef CASE

            std::cout << "    public static final ConfigOption " << id << " = ";
            std::cout << method << (key->getUnit() ? "U" : "") << "(\n";
            std::cout << "        \"" << key->getName() << "\", ";
            if (key->isPerObject())
                std::cout << kindstring << ", ";
            if (!key->getUnit())
                std::cout << typestring << ", ";
            else
                std::cout << "\"" << key->getUnit() << "\", ";
            if (!key->getDefaultValue())
                std::cout << "null";
            else
                std::cout << "\"" << opp_replacesubstring(key->getDefaultValue(), "\"", "\\\"", true) << "\"";
            std::cout << ",\n";

            std::string desc = key->getDescription();
            desc = opp_replacesubstring(desc.c_str(), "\n", "\\n\n", true); // keep explicit line breaks
            desc = opp_breaklines(desc.c_str(), 75);  // break long lines
            desc = opp_replacesubstring(desc.c_str(), "\"", "\\\"", true);
            desc = opp_replacesubstring(desc.c_str(), "\n", " \" +\n\"", true);
            desc = opp_replacesubstring(desc.c_str(), "\\n \"", "\\n\"", true); // remove bogus space after explicit line breaks
            desc = "\"" + desc + "\"";

            std::cout << opp_indentlines(desc.c_str(), "        ") << ");\n";
        }
        std::cout << "\n";

        std::vector<const char *> vars = getConfigEx()->getPredefinedVariableNames();
        for (int i=0; i<(int)vars.size(); i++)
        {
            opp_string id = vars[i];
            opp_strupr(id.buffer());
            const char *desc = getConfigEx()->getVariableDescription(vars[i]);
            std::cout << "    public static final String CFGVAR_" << id << " = addConfigVariable(";
            std::cout << "\"" << vars[i] << "\", \"" << opp_replacesubstring(desc, "\"", "\\\"", true) << "\");\n";
        }
        std::cout << "\n";
    }
    if (wantAll || !strcmp(category, "classes"))
    {
        processed = true;
        std::cout << "Registered C++ classes, including modules, channels and messages:\n";
        cRegistrationList *table = classes.getInstance();
        table->sort();
        for (int i=0; i<table->size(); i++)
        {
            cObject *obj = table->get(i);
            std::cout << "  class " << obj->getFullName() << "\n";
        }
        std::cout << "Note: if your class is not listed, it needs to be registered in the\n";
        std::cout << "C++ code using Define_Module(), Define_Channel() or Register_Class().\n";
        std::cout << "\n";
    }
    if (wantAll || !strcmp(category, "classdesc"))
    {
        processed = true;
        std::cout << "Classes that have associated reflection information (needed for Tkenv inspectors):\n";
        cRegistrationList *table = classDescriptors.getInstance();
        table->sort();
        for (int i=0; i<table->size(); i++)
        {
            cObject *obj = table->get(i);
            std::cout << "  class " << obj->getFullName() << "\n";
        }
        std::cout << "\n";
    }
    if (wantAll || !strcmp(category, "nedfunctions"))
    {
        processed = true;
        std::cout << "Functions that can be used in NED expressions and in omnetpp.ini:\n";
        cRegistrationList *table = nedFunctions.getInstance();
        table->sort();
        std::set<std::string> categories;
        for (int i=0; i<table->size(); i++)
        {
            cNEDFunction *nf = dynamic_cast<cNEDFunction *>(table->get(i));
            cNEDMathFunction *mf = dynamic_cast<cNEDMathFunction *>(table->get(i));
            categories.insert(nf ? nf->getCategory() : mf ? mf->getCategory() : "???");
        }
        for (std::set<std::string>::iterator ci=categories.begin(); ci!=categories.end(); ++ci)
        {
            std::string category = (*ci);
            std::cout << "\n Category \"" << category << "\":\n";
            for (int i=0; i<table->size(); i++)
            {
                cObject *obj = table->get(i);
                cNEDFunction *nf = dynamic_cast<cNEDFunction *>(table->get(i));
                cNEDMathFunction *mf = dynamic_cast<cNEDMathFunction *>(table->get(i));
                const char *fcat = nf ? nf->getCategory() : mf ? mf->getCategory() : "???";
                const char *desc = nf ? nf->getDescription() : mf ? mf->getDescription() : "???";
                if (fcat==category)
                {
                    std::cout << "  " << obj->getFullName() << " : " << obj->info() << "\n";
                    if (desc)
                        std::cout << "    " << desc << "\n";
                }
            }
        }
        std::cout << "\n";
    }
    if (wantAll || !strcmp(category, "neddecls"))
    {
        processed = true;
        std::cout << "Built-in NED declarations:\n\n";
        std::cout << "---START---\n";
        std::cout << NEDParser::getBuiltInDeclarations();
        std::cout << "---END---\n";
        std::cout << "\n";
    }
    if (wantAll || !strcmp(category, "units"))
    {
        processed = true;
        std::cout << "Recognized physical units (note: other units can be used as well, only\n";
        std::cout << "no automatic conversion will be available for them):\n";
        std::vector<const char *> units = UnitConversion::getAllUnits();
        for (int i=0; i<(int)units.size(); i++)
        {
            const char *u = units[i];
            const char *bu = UnitConversion::getBaseUnit(u);
            std::cout << "  " << u << "\t" << UnitConversion::getLongName(u);
            if (opp_strcmp(u,bu)!=0)
                std::cout << "\t" << UnitConversion::convertUnit(1,u,bu) << bu;
            std::cout << "\n";
        }
        std::cout << "\n";
    }
    if (wantAll || !strcmp(category, "enums"))
    {
        processed = true;
        std::cout << "Enums defined in .msg files\n";
        cRegistrationList *table = enums.getInstance();
        table->sort();
        for (int i=0; i<table->size(); i++)
        {
            cObject *obj = table->get(i);
            std::cout << "  " << obj->getFullName() << " : " << obj->info() << "\n";
        }
        std::cout << "\n";
    }
    if (wantAll || !strcmp(category, "userinterfaces"))
    {
        processed = true;
        std::cout << "User interfaces loaded:\n";
        cRegistrationList *table = omnetapps.getInstance();
        table->sort();
        for (int i=0; i<table->size(); i++)
        {
            cObject *obj = table->get(i);
            std::cout << "  " << obj->getFullName() << " : " << obj->info() << "\n";
        }
    }

    if (wantAll || !strcmp(category, "resultfilters"))
    {
        processed = true;
        std::cout << "Result filters that can be used in @statistic properties:\n";
        cRegistrationList *table = resultFilters.getInstance();
        table->sort();
        for (int i=0; i<table->size(); i++)
        {
            cObject *obj = table->get(i);
            std::cout << "  " << obj->getFullName() << " : " << obj->info() << "\n";
        }
    }

    if (wantAll || !strcmp(category, "resultrecorders"))
    {
        processed = true;
        std::cout << "Result recorders that can be used in @statistic properties:\n";
        cRegistrationList *table = resultRecorders.getInstance();
        table->sort();
        for (int i=0; i<table->size(); i++)
        {
            cObject *obj = table->get(i);
            std::cout << "  " << obj->getFullName() << " : " << obj->info() << "\n";
        }
    }

    if (!processed)
        throw cRuntimeError("Unrecognized category for '-h' option: %s", category);
}

int EnvirBase::getParsimProcId() const
{
#ifdef WITH_PARSIM
    return parsimcomm ? parsimcomm->getProcId() : 0;
#else
    return 0;
#endif
}

int EnvirBase::getParsimNumPartitions() const
{
#ifdef WITH_PARSIM
    return parsimcomm ? parsimcomm->getNumPartitions() : 0;
#else
    return 0;
#endif
}

void EnvirBase::run()
{
    try
    {
        doRun();
    }
    catch (std::exception& e)
    {
        displayException(e);
    }
}

void EnvirBase::shutdown()
{
    try
    {
        simulation.deleteNetwork();
        ev.notifyListeners(LF_ON_SHUTDOWN);
    }
    catch (std::exception& e)
    {
        displayException(e);
    }
}

void EnvirBase::setupNetwork(cModuleType *network)
{
    simulation.setupNetwork(network);
    eventlogmgr->flush();
}

void EnvirBase::startRun()
{
    resetClock();
    if (opt_simtimelimit > SIMTIME_ZERO)
        simulation.setSimulationTimeLimit(opt_simtimelimit);
    simulation.callInitialize();
    cLogProxy::flushLastLine();
}

void EnvirBase::endRun()  //FIXME eliminate???
{
    notifyListeners(LF_ON_RUN_END);
}

//-------------------------------------------------------------

void EnvirBase::configure(cComponent *component)
{
    addResultRecorders(component);
}

static int search_(std::vector<std::string>& v, const char *s)
{
    for (int i=0; i<(int)v.size(); i++)
        if (strcmp(v[i].c_str(), s)==0)
            return i;
    return -1;
}

inline void addIfNotContains_(std::vector<std::string>& v, const char *s)
{
    if (search_(v,s)==-1)
        v.push_back(s);
}

inline void addIfNotContains_(std::vector<std::string>& v, const std::string& s)
{
    if (search_(v,s.c_str())==-1)
        v.push_back(s);
}

void EnvirBase::addResultRecorders(cComponent *component)
{
    std::vector<const char *> statisticNames = component->getProperties()->getIndicesFor("statistic");
    std::string componentFullPath;
    for (int i = 0; i < (int)statisticNames.size(); i++)
    {
        const char *statisticName = statisticNames[i];
        if (componentFullPath.empty())
            componentFullPath = component->getFullPath();
        std::string statisticFullPath = componentFullPath + "." + statisticName;

        bool scalarsEnabled = ev.getConfig()->getAsBool(statisticFullPath.c_str(), CFGID_SCALAR_RECORDING);
        bool vectorsEnabled = ev.getConfig()->getAsBool(statisticFullPath.c_str(), CFGID_VECTOR_RECORDING);
        if (!scalarsEnabled && !vectorsEnabled)
            continue;

        cProperty *statisticProperty = component->getProperties()->get("statistic", statisticName);
        ASSERT(statisticProperty!=NULL);

        // collect the list of result recorders
        std::string modesOption = ev.getConfig()->getAsString(statisticFullPath.c_str(), CFGID_RESULT_RECORDING_MODES, "");
        std::vector<std::string> modes = extractRecorderList(modesOption.c_str(), statisticProperty);

        // if there are result recorders, add source filters and recorders
        if (!modes.empty())
        {
            // add source
            bool hasSourceKey = statisticProperty->getNumValues("source") > 0;
            const char *sourceSpec = hasSourceKey ? statisticProperty->getValue("source",0) : statisticName;
            SignalSource source = doStatisticSource(component, statisticName, sourceSpec, opt_warmupperiod!=SIMTIME_ZERO);

            // add result recorders
            for (int j = 0; j < (int)modes.size(); j++)
                doResultRecorder(source, modes[j].c_str(), scalarsEnabled, vectorsEnabled, component, statisticName);
        }
    }

    if (opt_debug_statistics_recording)
        dumpResultRecorders(component);
}

std::vector<std::string> EnvirBase::extractRecorderList(const char *modesOption, cProperty *statisticProperty)
{
    // "-" means "none"
    if (!modesOption[0] || (modesOption[0]=='-' && !modesOption[1]))
        return std::vector<std::string>();

    std::vector<std::string> modes; // the result

    // if first configured mode starts with '+' or '-', assume "default" as base
    if (modesOption[0]=='-' || modesOption[0]=='+')
    {
        // collect the mandatory record= items from @statistic (those not ending in '?')
        int n = statisticProperty->getNumValues("record");
        for (int i = 0; i < n; i++) {
            const char *m = statisticProperty->getValue("record", i);
            if (m[strlen(m)-1] != '?')
                addIfNotContains_(modes, m);
        }
    }

    // loop through all modes
    StringTokenizer tokenizer(modesOption, ","); //XXX we should ignore commas within parens
    while (tokenizer.hasMoreTokens())
    {
        const char *mode = tokenizer.nextToken();
        if (!strcmp(mode, "default"))
        {
            // collect the mandatory record= items from @statistic (those not ending in '?')
            int n = statisticProperty->getNumValues("record");
            for (int i = 0; i < n; i++) {
                const char *m = statisticProperty->getValue("record", i);
                if (m[strlen(m)-1] != '?')
                    addIfNotContains_(modes, m);
            }
        }
        else if (!strcmp(mode, "all"))
        {
            // collect all record= items from @statistic (strip trailing '?' if present)
            int n = statisticProperty->getNumValues("record");
            for (int i = 0; i < n; i++) {
                const char *m = statisticProperty->getValue("record", i);
                if (m[strlen(m)-1] != '?')
                    addIfNotContains_(modes, m);
                else
                    addIfNotContains_(modes, std::string(m, strlen(m)-1));
            }
        }
        else if (mode[0] == '-')
        {
            // remove from modes
            int k = search_(modes, mode+1);
            if (k != -1)
                modes.erase(modes.begin()+k);
        }
        else {
            // add to modes
            addIfNotContains_(modes, mode[0]=='+' ? mode+1 : mode);
        }
    }
    return modes;
}

static bool opp_isidentifier(const char *s)
{
    if (!opp_isalphaext(s[0]) && s[0]!='_')
        return false;
    while (*++s)
        if (!opp_isalnumext(*s) && s[0]!='_')
            return false;
    return true;
}

SignalSource EnvirBase::doStatisticSource(cComponent *component, const char *statisticName, const char *sourceSpec, bool needWarmupFilter)
{
    try
    {
        if (opp_isidentifier(sourceSpec))
        {
            // simple case: just a signal name
            simsignal_t signalID = cComponent::registerSignal(sourceSpec);
            if (!needWarmupFilter)
                return SignalSource(component, signalID);
            else
            {
                WarmupPeriodFilter *warmupFilter = new WarmupPeriodFilter();
                component->subscribe(signalID, warmupFilter);
                return SignalSource(warmupFilter);
            }
        }
        else
        {
            StatisticSourceParser parser;
            return parser.parse(component, statisticName, sourceSpec, needWarmupFilter);
        }
    }
    catch (std::exception& e)
    {
        throw cRuntimeError("Error adding statistic '%s' to module %s (NED type: %s): error in source=%s: %s",
            statisticName, component->getFullPath().c_str(), component->getNedTypeName(), sourceSpec, e.what());
    }
}

void EnvirBase::doResultRecorder(const SignalSource& source, const char *recordingMode, bool scalarsEnabled, bool vectorsEnabled, cComponent *component, const char *statisticName)
{
    try
    {
        if (opp_isidentifier(recordingMode))
        {
            // simple case: just a plain recorder
            bool recordsVector = !strcmp(recordingMode, "vector");  // the only vector recorder is "vector"
            if (recordsVector ? !vectorsEnabled : !scalarsEnabled)
                return; // no point in creating if recording is disabled

            cResultRecorder *recorder = cResultRecorderDescriptor::get(recordingMode)->create();
            recorder->init(component, statisticName, recordingMode);
            source.subscribe(recorder);
        }
        else
        {
            // something more complicated: use parser
            StatisticRecorderParser parser;
            parser.parse(source, recordingMode, scalarsEnabled, vectorsEnabled, component, statisticName);
        }
    }
    catch (std::exception& e)
    {
        throw cRuntimeError("Error adding statistic '%s' to module %s (NED type: %s): bad recording mode '%s': %s",
            statisticName, component->getFullPath().c_str(), component->getNedTypeName(), recordingMode, e.what());
    }
}

void EnvirBase::dumpResultRecorders(cComponent *component)
{
    bool componentPathPrinted = false;
    std::vector<simsignal_t> signals = component->getLocalListenedSignals();
    for (unsigned int i = 0; i < signals.size(); i++)
    {
        bool signalNamePrinted = false;
        simsignal_t signalID = signals[i];
        std::vector<cIListener*> listeners = component->getLocalSignalListeners(signalID);
        for (unsigned int j = 0; j < listeners.size(); j++) {
            if (dynamic_cast<cResultListener*>(listeners[j])) {
                if (!componentPathPrinted) {
                    std::cout << component->getFullPath() << " (" << component->getNedTypeName() << "):\n";
                    componentPathPrinted = true;
                }
                if (!signalNamePrinted) {
                    std::cout << "    \"" << cComponent::getSignalName(signalID) << "\" (signalID="  << signalID << "):\n";
                    signalNamePrinted = true;
                }
                dumpResultRecorderChain((cResultListener *)listeners[j], 0);
            }
        }
    }
}

void EnvirBase::dumpResultRecorderChain(cResultListener *listener, int depth)
{
    for (int i = 0; i < depth+2; i++)
        std::cout << "    ";
    std::cout << listener->str();
    if (dynamic_cast<cResultRecorder*>(listener))
        std::cout << " ==> " << ((cResultRecorder*)listener)->getResultName();
    std::cout << "\n";

    if (dynamic_cast<cResultFilter *>(listener))
    {
        std::vector<cResultListener *> delegates = ((cResultFilter*)listener)->getDelegates();
        for (unsigned int i=0; i < delegates.size(); i++)
            dumpResultRecorderChain(delegates[i], depth+1);
    }
}

void EnvirBase::readParameter(cPar *par)
{
    ASSERT(!par->isSet());  // must be unset at this point

    // get it from the ini file
    std::string moduleFullPath = par->getOwner()->getFullPath();
    const char *str = getConfigEx()->getParameterValue(moduleFullPath.c_str(), par->getName(), par->containsValue());

/* XXX hack to use base directory for resolving xml files location has been commented out
 * FIXME a solution needs to be worked out!
    if (str[0]=='x' && !strncmp(str,"xmldoc",6) && !opp_isalnum(str[6]))
    {
        // Make XML file location relative to the ini file in which it occurs.
        // Find substring between first two quotes (that is, the XML filename),
        // and prefix it with the directory.
        const char *begQuote = strchr(str+6,'"');
        if (!begQuote)
            return std::string(str);
        const char *endQuote = strchr(begQuote+1,'"');
        while (endQuote && *(endQuote-1)=='\\' && *(endQuote-2)!='\\')
            endQuote = strchr(endQuote+1,'"');
        if (!endQuote)
            return std::string(str);
        std::string fname(begQuote+1, endQuote-begQuote-1);
        const char *baseDir = getConfig()->getBaseDirectoryFor(NULL, "Parameters", parname);
        fname = tidyFilename(concatDirAndFile(baseDir, fname.c_str()).c_str(),true);
        std::string ret = std::string(str, begQuote-str+1) + fname + endQuote;
        //XXX use "ret" further!!!
    }
*/

    if (opp_strcmp(str, "default")==0)
    {
        ASSERT(par->containsValue());  // cConfiguration should not return "=default" lines for params that have no default value
        par->acceptDefault();
    }
    else if (opp_strcmp(str, "ask")==0)
    {
        askParameter(par, false);
    }
    else if (!opp_isempty(str))
    {
        par->parse(str);
    }
    else
    {
        // str empty: no value in the ini file
        if (par->containsValue())
            par->acceptDefault();
        else
            askParameter(par, true);
    }
}

bool EnvirBase::isModuleLocal(cModule *parentmod, const char *modname, int index)
{
#ifdef WITH_PARSIM
    if (!opt_parsim)
       return true;

    // toplevel module is local everywhere
    if (!parentmod)
       return true;

    // find out if this module is (or has any submodules that are) on this partition
    char parname[MAX_OBJECTFULLPATH];
    if (index<0)
        sprintf(parname,"%s.%s", parentmod->getFullPath().c_str(), modname);
    else
        sprintf(parname,"%s.%s[%d]", parentmod->getFullPath().c_str(), modname, index); //FIXME this is incorrectly chosen for non-vector modules too!
    std::string procIds = getConfig()->getAsString(parname, CFGID_PARTITION_ID, "");
    if (procIds.empty())
    {
        // modules inherit the setting from their parents, except when the parent is the system module (the network) itself
        if (!parentmod->getParentModule())
            throw cRuntimeError("incomplete partitioning: missing value for '%s'",parname);
        // "true" means "inherit", because an ancestor which answered "false" doesn't get recursed into
        return true;
    }
    else if (strcmp(procIds.c_str(), "*") == 0)
    {
        // present on all partitions (provided that ancestors have "*" set as well)
        return true;
    }
    else
    {
        // we expect a partition Id (or partition Ids, separated by commas) where this
        // module needs to be instantiated. So we return true if any of the numbers
        // is the Id of the local partition, otherwise false.
        EnumStringIterator procIdIter(procIds.c_str());
        if (procIdIter.hasError())
            throw cRuntimeError("wrong partitioning: syntax error in value '%s' for '%s' "
                                "(allowed syntax: '', '*', '1', '0,3,5-7')",
                                procIds.c_str(), parname);
        int numPartitions = parsimcomm->getNumPartitions();
        int myProcId = parsimcomm->getProcId();
        for (; procIdIter()!=-1; procIdIter++)
        {
            if (procIdIter() >= numPartitions)
                throw cRuntimeError("wrong partitioning: value %d too large for '%s' (total partitions=%d)",
                                    procIdIter(), parname, numPartitions);
            if (procIdIter() == myProcId)
                return true;
        }
        return false;
    }
#else
    return true;
#endif
}

cXMLElement *EnvirBase::getXMLDocument(const char *filename, const char *path)
{
    cXMLElement *documentnode = xmlcache->getDocument(filename);
    return resolveXMLPath(documentnode, path);
}

cXMLElement *EnvirBase::getParsedXMLString(const char *content, const char *path)
{
    cXMLElement *documentnode = xmlcache->getParsed(content);
    return resolveXMLPath(documentnode, path);
}

cXMLElement *EnvirBase::resolveXMLPath(cXMLElement *documentnode, const char *path)
{
    assert(documentnode);
    if (path)
    {
        ModNameParamResolver resolver(simulation.getContextModule()); // resolves $MODULE_NAME etc in XPath expr.
        return cXMLElement::getDocumentElementByPath(documentnode, path, &resolver);
    }
    else
    {
        // returns the root element (child of the document node)
        return documentnode->getFirstChild();
    }
}

void EnvirBase::forgetXMLDocument(const char *filename)
{
    xmlcache->forgetDocument(filename);
}

void EnvirBase::forgetParsedXMLString(const char *content)
{
    xmlcache->forgetParsed(content);
}

void EnvirBase::flushXMLDocumentCache()
{
    xmlcache->flushDocumentCache();
}

void EnvirBase::flushXMLParsedContentCache()
{
    xmlcache->flushParsedContentCache();
}

cConfiguration *EnvirBase::getConfig()
{
    return cfg;
}

cConfigurationEx *EnvirBase::getConfigEx()
{
    return cfg;
}

//-------------------------------------------------------------

void EnvirBase::bubble(cComponent *component, const char *text)
{
    if (record_eventlog)
        eventlogmgr->bubble(component, text);
}

void EnvirBase::objectDeleted(cObject *object)
{
    // TODO?
}

void EnvirBase::simulationEvent(cEvent *event)
{
    if (logFormatUsesEventName)
        currentEventName = event->getFullName();
    currentEventClassName = event->getClassName();
    currentModuleId = event->isMessage() ? (static_cast<cMessage *>(event))->getArrivalModule()->getId() : -1;
    if (record_eventlog)
        eventlogmgr->simulationEvent(event);
}

void EnvirBase::beginSend(cMessage *msg)
{
    if (record_eventlog)
        eventlogmgr->beginSend(msg);
}

void EnvirBase::messageScheduled(cMessage *msg)
{
    if (record_eventlog)
        eventlogmgr->messageScheduled(msg);
}

void EnvirBase::messageCancelled(cMessage *msg)
{
    if (record_eventlog)
        eventlogmgr->messageCancelled(msg);
}

void EnvirBase::messageSendDirect(cMessage *msg, cGate *toGate, simtime_t propagationDelay, simtime_t transmissionDelay)
{
    if (record_eventlog)
        eventlogmgr->messageSendDirect(msg, toGate, propagationDelay, transmissionDelay);
}

void EnvirBase::messageSendHop(cMessage *msg, cGate *srcGate)
{
    if (record_eventlog)
        eventlogmgr->messageSendHop(msg, srcGate);
}

void EnvirBase::messageSendHop(cMessage *msg, cGate *srcGate, simtime_t propagationDelay, simtime_t transmissionDelay)
{
    if (record_eventlog)
        eventlogmgr->messageSendHop(msg, srcGate, propagationDelay, transmissionDelay);
}

void EnvirBase::endSend(cMessage *msg)
{
    if (record_eventlog)
        eventlogmgr->endSend(msg);
}

void EnvirBase::messageCreated(cMessage *msg)
{
    if (record_eventlog)
        eventlogmgr->messageCreated(msg);
}

void EnvirBase::messageCloned(cMessage *msg, cMessage *clone)
{
    if (record_eventlog)
        eventlogmgr->messageCloned(msg, clone);
}

void EnvirBase::messageDeleted(cMessage *msg)
{
    if (record_eventlog)
        eventlogmgr->messageDeleted(msg);
}

void EnvirBase::componentMethodBegin(cComponent *from, cComponent *to, const char *methodFmt, va_list va, bool silent)
{
    if (record_eventlog)
        eventlogmgr->componentMethodBegin(from, to, methodFmt, va);
}

void EnvirBase::componentMethodEnd()
{
    if (record_eventlog)
        eventlogmgr->componentMethodEnd();
}

void EnvirBase::moduleCreated(cModule *newmodule)
{
    if (record_eventlog)
        eventlogmgr->moduleCreated(newmodule);
}

void EnvirBase::moduleDeleted(cModule *module)
{
    if (record_eventlog)
        eventlogmgr->moduleDeleted(module);
}

void EnvirBase::moduleReparented(cModule *module, cModule *oldparent)
{
    if (record_eventlog)
        eventlogmgr->moduleReparented(module, oldparent);
}

void EnvirBase::gateCreated(cGate *newgate)
{
    if (record_eventlog)
        eventlogmgr->gateCreated(newgate);
}

void EnvirBase::gateDeleted(cGate *gate)
{
    if (record_eventlog)
        eventlogmgr->gateDeleted(gate);
}

void EnvirBase::connectionCreated(cGate *srcgate)
{
    if (record_eventlog)
        eventlogmgr->connectionCreated(srcgate);
}

void EnvirBase::connectionDeleted(cGate *srcgate)
{
    if (record_eventlog)
        eventlogmgr->connectionDeleted(srcgate);
}

void EnvirBase::displayStringChanged(cComponent *component)
{
    if (record_eventlog)
        eventlogmgr->displayStringChanged(component);
}

void EnvirBase::log(cLogEntry *entry)
{
    if (record_eventlog)
    {
        std::string prefix = logFormatter.formatPrefix(entry);
        eventlogmgr->logLine(prefix.c_str(), entry->text, entry->textLength);
    }
}

void EnvirBase::undisposedObject(cObject *obj)
{
    if (opt_print_undisposed)
        ::printf("undisposed object: (%s) %s -- check module destructor\n", obj->getClassName(), obj->getFullPath().c_str());
}

//-------------------------------------------------------------

void EnvirBase::processFileName(opp_string& fname)
{
    std::string text = fname.c_str();

    // insert ".<hostname>.<pid>" if requested before file extension
    // (note: parsimProcId cannot be appended because of initialization order)
    if (opt_fname_append_host)
    {
        std::string extension = "";
        std::string::size_type index = text.rfind('.');
        if (index != std::string::npos) {
          extension = std::string(text, index);
          text.erase(index);
        }

        const char *hostname = opp_gethostname();
        if (!hostname)
            throw cRuntimeError("Cannot append hostname to file name `%s': no HOST, HOSTNAME "
                                "or COMPUTERNAME (Windows) environment variable",
                                fname.c_str());
        int pid = getpid();

        // append
        text += opp_stringf(".%s.%d%s", hostname, pid, extension.c_str());
    }
    fname = text.c_str();
}

void EnvirBase::readOptions()
{
    cConfiguration *cfg = getConfig();

    opt_total_stack = (size_t) cfg->getAsDouble(CFGID_TOTAL_STACK, TOTAL_STACK_SIZE);
    opt_parsim = cfg->getAsBool(CFGID_PARALLEL_SIMULATION);
    if (!opt_parsim)
    {
        opt_scheduler_class = cfg->getAsString(CFGID_SCHEDULER_CLASS);
    }
    else
    {
#ifdef WITH_PARSIM
        opt_parsimcomm_class = cfg->getAsString(CFGID_PARSIM_COMMUNICATIONS_CLASS);
        opt_parsimsynch_class = cfg->getAsString(CFGID_PARSIM_SYNCHRONIZATION_CLASS);
#else
        throw cRuntimeError("Parallel simulation is turned on in the ini file, but OMNeT++ was compiled without parallel simulation support (WITH_PARSIM=no)");
#endif
    }

    opt_outputvectormanager_class = cfg->getAsString(CFGID_OUTPUTVECTORMANAGER_CLASS);
    opt_outputscalarmanager_class = cfg->getAsString(CFGID_OUTPUTSCALARMANAGER_CLASS);
    opt_snapshotmanager_class = cfg->getAsString(CFGID_SNAPSHOTMANAGER_CLASS);

    opt_fname_append_host = cfg->getAsBool(CFGID_FNAME_APPEND_HOST, opt_parsim);

    ev.debug_on_errors = cfg->getAsBool(CFGID_DEBUG_ON_ERRORS);
    ev.attach_debugger_on_errors = cfg->getAsBool(CFGID_DEBUGGER_ATTACH_ON_ERROR);
    opt_print_undisposed = cfg->getAsBool(CFGID_PRINT_UNDISPOSED);

    int scaleexp = (int) cfg->getAsInt(CFGID_SIMTIME_SCALE);
    SimTime::setScaleExp(scaleexp);

    // note: this is read per run as well, but Tkenv needs its value on startup too
    opt_inifile_network_dir = cfg->getConfigEntry(CFGID_NETWORK->getName()).getBaseDirectory();

    // other options are read on per-run basis
}

void EnvirBase::readPerRunOptions()
{
    cConfiguration *cfg = getConfig();

    // get options from ini file
    opt_network_name = cfg->getAsString(CFGID_NETWORK);
    opt_inifile_network_dir = cfg->getConfigEntry(CFGID_NETWORK->getName()).getBaseDirectory();
    opt_warnings = cfg->getAsBool(CFGID_WARNINGS);
    opt_simtimelimit = cfg->getAsDouble(CFGID_SIM_TIME_LIMIT);
    opt_cputimelimit = (long) cfg->getAsDouble(CFGID_CPU_TIME_LIMIT);
    opt_warmupperiod = cfg->getAsDouble(CFGID_WARMUP_PERIOD);
    opt_fingerprint = cfg->getAsString(CFGID_FINGERPRINT);
    opt_num_rngs = cfg->getAsInt(CFGID_NUM_RNGS);
    opt_rng_class = cfg->getAsString(CFGID_RNG_CLASS);
    opt_seedset = cfg->getAsInt(CFGID_SEED_SET);
    opt_debug_statistics_recording = cfg->getAsBool(CFGID_DEBUG_STATISTICS_RECORDING);
    opt_check_signals = cfg->getAsBool(CFGID_CHECK_SIGNALS);

    simulation.setWarmupPeriod(opt_warmupperiod);

    // install hasher object
    if (!opt_fingerprint.empty())
        simulation.setHasher(new cHasher());
    else
        simulation.setHasher(NULL);

    cComponent::setCheckSignals(opt_check_signals);

    // run RNG self-test on RNG class selected for this run
    cRNG *testrng;
    CREATE_BY_CLASSNAME(testrng, opt_rng_class.c_str(), cRNG, "random number generator");
    testrng->selfTest();
    delete testrng;

    // set up RNGs
    int i;
    for (i=0; i<num_rngs; i++)
         delete rngs[i];
    delete [] rngs;

    num_rngs = opt_num_rngs;
    rngs = new cRNG *[num_rngs];
    for (i=0; i<num_rngs; i++)
    {
        cRNG *rng;
        CREATE_BY_CLASSNAME(rng, opt_rng_class.c_str(), cRNG, "random number generator");
        rngs[i] = rng;
        rngs[i]->initialize(opt_seedset, i, num_rngs, getParsimProcId(), getParsimNumPartitions(), getConfig());
    }

    // init nextuniquenumber -- startRun() is too late because simple module ctors have run by then
    nextuniquenumber = 0;
#ifdef WITH_PARSIM
    if (opt_parsim)
        nextuniquenumber = (unsigned)parsimcomm->getProcId() * ((~0UL) / (unsigned)parsimcomm->getNumPartitions());
#endif

    // open eventlog file.
    // Note: in startRun() it would be too late, because modules are created earlier
    eventlogmgr->configure();
    record_eventlog = cfg->getAsBool(CFGID_RECORD_EVENTLOG);
}

void EnvirBase::setEventlogRecording(bool enabled)
{
    // NOTE: eventlogmgr must be non-NULL when record_eventlog is true
    if (enabled && !record_eventlog) {   //FIXME not good!!!!
        eventlogmgr->open();
        eventlogmgr->recordSimulation();
    }
    record_eventlog = enabled;
    eventlogmgr->flush();
}

bool EnvirBase::hasEventlogRecordingIntervals() const
{
    return eventlogmgr && eventlogmgr->hasRecordingIntervals();
}

void EnvirBase::clearEventlogRecordingIntervals()
{
    if (eventlogmgr)
        eventlogmgr->clearRecordingIntervals();
}

//-------------------------------------------------------------

int EnvirBase::getNumRNGs() const
{
    return num_rngs;
}

cRNG *EnvirBase::getRNG(int k)
{
    if (k<0 || k>=num_rngs)
        throw cRuntimeError("RNG index %d is out of range (num-rngs=%d, check the configuration)", k, num_rngs);
    return rngs[k];
}

void EnvirBase::getRNGMappingFor(cComponent *component)
{
    cConfigurationEx *cfg = getConfigEx();
    std::string componentFullPath = component->getFullPath();
    std::vector<const char *> suffixes = cfg->getMatchingPerObjectConfigKeySuffixes(componentFullPath.c_str(), "rng-*"); // CFGID_RNG_K
    if (suffixes.empty())
        return;

    // extract into tmpmap[]
    int mapsize=0;
    int tmpmap[100];
    for (int i=0; i<(int)suffixes.size(); i++)
    {
        const char *suffix = suffixes[i];  // contains "rng-1", "rng-4" or whichever has been found in the config for this module/channel
        const char *value = cfg->getPerObjectConfigValue(componentFullPath.c_str(), suffix);
        ASSERT(value!=NULL);
        char *s1, *s2;
        int modRng = strtol(suffix+strlen("rng-"), &s1, 10);
        int physRng = strtol(value, &s2, 10);
        if (*s1!='\0' || *s2!='\0')
            throw cRuntimeError("Configuration error: %s=%s for module/channel %s: "
                                "numeric RNG indices expected",
                                suffix, value, component->getFullPath().c_str());

        if (physRng>getNumRNGs())
            throw cRuntimeError("Configuration error: rng-%d=%d of module/channel %s: "
                                "RNG index out of range (num-rngs=%d)",
                                modRng, physRng, component->getFullPath().c_str(), getNumRNGs());
        if (modRng>=mapsize)
        {
            if (modRng>=100)
                throw cRuntimeError("Configuration error: rng-%d=... of module/channel %s: "
                                    "local RNG index out of supported range 0..99",
                                    modRng, component->getFullPath().c_str());
            while (mapsize<=modRng)
            {
                tmpmap[mapsize] = mapsize;
                mapsize++;
            }
        }
        tmpmap[modRng] = physRng;
    }

    // install map into the module
    if (mapsize>0)
    {
        int *map = new int[mapsize];
        memcpy(map, tmpmap, mapsize*sizeof(int));
        component->setRNGMap(mapsize, map);
    }
}

//-------------------------------------------------------------

void *EnvirBase::registerOutputVector(const char *modulename, const char *vectorname)
{
    assert(outvectormgr);
    return outvectormgr->registerVector(modulename, vectorname);
}

void EnvirBase::deregisterOutputVector(void *vechandle)
{
    assert(outvectormgr);
    outvectormgr->deregisterVector(vechandle);
}

void EnvirBase::setVectorAttribute(void *vechandle, const char *name, const char *value)
{
    assert(outvectormgr);
    outvectormgr->setVectorAttribute(vechandle, name, value);
}

bool EnvirBase::recordInOutputVector(void *vechandle, simtime_t t, double value)
{
    assert(outvectormgr);
    return outvectormgr->record(vechandle, t, value);
}

//-------------------------------------------------------------

void EnvirBase::recordScalar(cComponent *component, const char *name, double value, opp_string_map *attributes)
{
    assert(outscalarmgr);
    outscalarmgr->recordScalar(component, name, value, attributes);
}

void EnvirBase::recordStatistic(cComponent *component, const char *name, cStatistic *statistic, opp_string_map *attributes)
{
    assert(outscalarmgr);
    outscalarmgr->recordStatistic(component, name, statistic, attributes);
}

//-------------------------------------------------------------

std::ostream *EnvirBase::getStreamForSnapshot()
{
    return snapshotmgr->getStreamForSnapshot();
}

void EnvirBase::releaseStreamForSnapshot(std::ostream *os)
{
    snapshotmgr->releaseStreamForSnapshot(os);
}

//-------------------------------------------------------------

unsigned long EnvirBase::getUniqueNumber()
{
    // TBD check for overflow
    return nextuniquenumber++;
}

std::string EnvirBase::makeDebuggerCommand()
{
    std::string cmd = getConfig()->getAsString(CFGID_DEBUGGER_ATTACH_COMMAND);
    if (cmd == "")
    {
        ::printf("Cannot start debugger: no debugger configured\n");
        return "";
    }
    size_t pos = cmd.find('%');
    if (pos == std::string::npos || cmd.rfind('%') != pos || cmd[pos+1] != 'u')
    {
        ::printf("Cannot start debugger: debugger attach command must contain '%%u' "
                 "and no additional percent sign.\n");
        return "";
    }
    pid_t pid = getpid();
    return opp_stringf(cmd.c_str(), (unsigned int)pid);
}

void EnvirBase::attachDebugger()
{
    // launch debugger
    std::string cmd = makeDebuggerCommand();
    if (cmd == "")
        return; // no suitable debugger command

    ::printf("Starting debugger: %s\n", cmd.c_str());
    fflush(stdout);
    system(cmd.c_str());

    // hold for a while to allow debugger to start up and attach to us
    int secondsToWait = (int)ceil(getConfig()->getAsDouble(CFGID_DEBUGGER_ATTACH_WAIT_TIME));
    time_t startTime = time(NULL);
    while (time(NULL)-startTime < secondsToWait)
        for (int i=0; i<1000; i++); // DEBUGGER ATTACHED -- YOUR CODE IS A FEW FRAMES UP ON THE CALL STACK
}

void EnvirBase::crashHandler(int)
{
    ev.attachDebugger();
}

void EnvirBase::addHttpRequestHandler(cHttpRequestHandler *p)
{
    if (httpServer)
        httpServer->addHttpRequestHandler(p);
}

void EnvirBase::removeHttpRequestHandler(cHttpRequestHandler *p)
{
    if (httpServer)
        httpServer->removeHttpRequestHandler(p);
}

void EnvirBase::displayException(std::exception& ex)
{
    cException *e = dynamic_cast<cException *>(&ex);
    if (!e)
        ev.printfmsg("Error: %s.", ex.what());
    else
        ev.printfmsg("%s", e->getFormattedMessage().c_str());
}

bool EnvirBase::idle()
{
    return false;
}

int EnvirBase::getArgCount() const
{
    return args->getArgCount();
}

char **EnvirBase::getArgVector() const
{
    return args->getArgVector();
}

void EnvirBase::addListener(cISimulationLifetimeListener *listener)
{
    std::vector<cISimulationLifetimeListener*>::iterator it = std::find(listeners.begin(), listeners.end(), listener);
    if (it == listeners.end()) {
        listeners.push_back(listener);
        listener->listenerAdded();
    }
}

void EnvirBase::removeListener(cISimulationLifetimeListener *listener)
{
    std::vector<cISimulationLifetimeListener*>::iterator it = std::find(listeners.begin(), listeners.end(), listener);
    if (it != listeners.end()) {
        listeners.erase(it);
        listener->listenerRemoved();
    }
}

void EnvirBase::notifyListeners(SimulationLifetimeEventType eventType, cObject *details)
{
    // make a copy of the listener list, to avoid problems from listeners
    // getting added/removed during notification
    std::vector<cISimulationLifetimeListener*> copy = listeners;
    for (int i = 0; i < (int)copy.size(); i++) {
        try {
            copy[i]->lifetimeEvent(eventType, details);
        }
        catch (std::exception& e) {  //XXX perhaps we shouldn't hide the exception!!!! just re-throw? then all notifyListeners() calls MUST be surrounded with try-catch!!!!
            const char *eventName = cISimulationLifetimeListener::getSimulationLifetimeEventName(eventType);
            printfmsg("Error in %s listener: %s", eventName, e.what());
        }
    }
}


//-------------------------------------------------------------

void EnvirBase::resetClock()
{
    timeval now;
    gettimeofday(&now, NULL);
    laststarted = simendtime = simbegtime = now;
    elapsedtime.tv_sec = elapsedtime.tv_usec = 0;
}

void EnvirBase::startClock()
{
    gettimeofday(&laststarted, NULL);
}

void EnvirBase::stopClock()
{
    gettimeofday(&simendtime, NULL);
    elapsedtime = elapsedtime + simendtime - laststarted;
    simulatedtime = simulation.getSimTime();
}

timeval EnvirBase::totalElapsed()
{
    timeval now;
    gettimeofday(&now, NULL);
    return now - laststarted + elapsedtime;
}

void EnvirBase::checkTimeLimits()
{
#ifdef USE_OMNETPP4x_FINGERPRINTS
    if (opt_simtimelimit!=SIMTIME_ZERO && simulation.getSimTime()>=opt_simtimelimit)
         throw cTerminationException(eSIMTIME);
#endif
    if (opt_cputimelimit==0) // no limit
         return;
    if (disable_tracing && (simulation.getEventNumber()&0xFF)!=0) // optimize: in Express mode, don't call gettimeofday() on every event
         return;
    timeval now;
    gettimeofday(&now, NULL);
    long elapsedsecs = now.tv_sec - laststarted.tv_sec + elapsedtime.tv_sec;
    if (elapsedsecs>=opt_cputimelimit)
         throw cTerminationException(eREALTIME);
}

void EnvirBase::stoppedWithTerminationException(cTerminationException& e)
{
    // if we're running in parallel and this exception is NOT one we received
    // from other partitions, then notify other partitions
#ifdef WITH_PARSIM
    if (opt_parsim && !dynamic_cast<cReceivedTerminationException *>(&e))
        parsimpartition->broadcastTerminationException(e);
#endif
    if (record_eventlog)
        eventlogmgr->endRun(e.isError(), e.getErrorCode(), e.getFormattedMessage().c_str());
}

void EnvirBase::stoppedWithException(std::exception& e)
{
    // if we're running in parallel and this exception is NOT one we received
    // from other partitions, then notify other partitions
#ifdef WITH_PARSIM
    if (opt_parsim && !dynamic_cast<cReceivedException *>(&e))
        parsimpartition->broadcastException(e);
#endif
    if (record_eventlog)
        // TODO: get error code from the exception?
        eventlogmgr->endRun(true, eCUSTOM, e.what());
}

void EnvirBase::checkFingerprint()
{
    if (opt_fingerprint.empty() || !simulation.getHasher())
        return;

    int k = 0;
    StringTokenizer tokenizer(opt_fingerprint.c_str());
    while (tokenizer.hasMoreTokens())
    {
        const char *fingerprint = tokenizer.nextToken();
        if (simulation.getHasher()->equals(fingerprint))
        {
            printfmsg("Fingerprint successfully verified: %s", fingerprint);
            return;
        }
        k++;
    }

    printfmsg("Fingerprint mismatch! calculated: %s, expected: %s%s",
              simulation.getHasher()->str().c_str(),
              (k>=2 ? "one of: " : ""), opt_fingerprint.c_str());
}

cModuleType *EnvirBase::resolveNetwork(const char *networkname)
{
    cModuleType *network = NULL;
    std::string inifilePackage = simulation.getNedPackageForFolder(opt_inifile_network_dir.c_str());

    bool hasInifilePackage = !inifilePackage.empty() && strcmp(inifilePackage.c_str(),"-")!=0;
    if (hasInifilePackage)
        network = cModuleType::find((inifilePackage+"."+networkname).c_str());
    if (!network)
        network = cModuleType::find(networkname);
    if (!network) {
        if (hasInifilePackage)
            throw cRuntimeError("Network `%s' or `%s' not found, check .ini and .ned files",
                                networkname, (inifilePackage+"."+networkname).c_str());
        else
            throw cRuntimeError("Network `%s' not found, check .ini and .ned files", networkname);
    }
    if (!network->isNetwork())
        throw cRuntimeError("Module type `%s' is not a network", network->getFullName());
    return network;
}
