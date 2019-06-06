/*
 Copyright (C) 2017-2019 Fredrik Öhrström

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include"cmdline.h"
#include"config.h"
#include"meters.h"
#include"printer.h"
#include"serial.h"
#include"util.h"
#include"version.h"
#include"wmbus.h"

#include<string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>

using namespace std;

void oneshotCheck(Configuration *cmdline, SerialCommunicationManager *manager, Telegram *t, Meter *meter, vector<unique_ptr<Meter>> &meters);
void startUsingCommandline(Configuration *cmdline);
void startUsingConfigFiles(string root, bool is_daemon);
void startDaemon(string pid_file); // Will use config files.

int main(int argc, char **argv)
{
    auto cmdline = parseCommandLine(argc, argv);

    if (cmdline->version) {
        printf("wmbusmeters: " VERSION "\n");
        printf(COMMIT "\n");
        exit(0);
    }
    if (cmdline->license) {
        const char * license = R"LICENSE(
Copyright (C) 2017-2019 Fredrik Öhrström

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

You can download the source here: https://github.com/weetmuts/wmbusmeters
But you can also request the source from the person/company that
provided you with this binary. Read the full license for all details.

)LICENSE";
        puts(license);
        exit(0);
    }
    if (cmdline->need_help) {
        printf("wmbusmeters version: " VERSION "\n");
        const char *msg = R"MANUAL(
Usage: wmbusmeters {options} <device> ( [meter_name] [meter_type]{:<modes>} [meter_id] [meter_key] )*

As <options> you can use:

    --addconversion=<unit>+ add conversion to these units to json and meter env variables (GJ)
    --debug for a lot of information
    --exitafter=<time> exit program after time, eg 20h, 10m 5s
    --format=<hr/json/fields> for human readable, json or semicolon separated fields
    --listento=<mode> tell the wmbus dongle to listen to this single link mode where mode can be
                      c1,t1,s1,s1m,n1a,n1b,n1c,n1d,n1e,n1f
    --listento=c1,t1,s1 tell the wmbus dongle to listen to these link modes
                      different dongles support different combinations of modes
    --c1 --t1 --s1 --s1m ... another way to set the link mode for the dongle
    --logfile=<file> use this file instead of stdout
    --logtelegrams log the contents of the telegrams for easy replay
    --meterfiles=<dir> store meter readings in dir
    --meterfilesaction=(overwrite|append) overwrite or append to the meter readings file
    --oneshot wait for an update from each meter, then quit
    --separator=<c> change field separator to c
    --shell=<cmdline> invokes cmdline with env variables containing the latest reading
    --shellenvs list the env variables available for the meter
    --useconfig=<dir> load config files from dir/etc
    --verbose for more information

As a <device> you can use: auto
which will look for the links /dev/im87a,/dev/amb8475 and /dev/rtlsdr (the
links are automatically generated by udev if you have run the install scripts.)
and start wmbusmeters with the proper tty device or rtlwmbus background process.

As a <device> you can also use: the exact /dev/ttyUSB0 to your dongle if you do not want
to install the udev rule.

As a <device> you can also use: rtlwmbus
to spawn the background process: \"rtl_sdr -f 868.95M -s 1.6e6 - 2>/dev/null | rtl_wmbus\"
You can also use: rtlwmbus:868.9M to use this fq instead. Fq tuning can sometimes
be necessary. Or you can specify the entire background process command line: \"rtlwmbus:<commandline>\"

As meter quadruples you specify:
<meter_name> a mnemonic for this particular meter
<meter_type> one of the supported meters
(can be suffixed with :<modes> to specify which modes you expect the meter to use when transmitting)
<meter_id> an 8 digit mbus id, usually printed on the meter
<meter_key> an encryption key unique for the meter
    if the meter uses no encryption, then supply ""

Supported water meters:
Kamstrup Multical 21 (multical21)
Kamstrup flowIQ 3100 (flowiq3100)
Sontex Supercom 587 (supercom587)
Sensus iPERL (iperl)
Techem MK Radio 3 (mkradio3)

Supported heat cost allocators:
Qundis Q caloric (qcaloric)
Heat Cost Allocator Innotas EurisII  (eurisii)

Supported heat meters:
Techem Vario 4 (vario451)

Work in progress:
Water meter Apator at-wmbus-16-2 (apator162)
Heat meter Kamstrup Multical 302 (multical302)
Electricity meter Kamstrup Omnipower (omnipower) and Tauron Amiplus (amiplus)

)MANUAL";
        puts(msg);
    }
    else
    if (cmdline->daemon) {
        startDaemon(cmdline->pid_file);
        exit(0);
    }
    else
    if (cmdline->useconfig) {
        startUsingConfigFiles(cmdline->config_root, false);
        exit(0);
    }
    else {
        // We want the data visible in the log file asap!
        setbuf(stdout, NULL);
        startUsingCommandline(cmdline.get());
    }
}

void startUsingCommandline(Configuration *config)
{
    if (config->use_logfile) {
        verbose("(wmbusmeters) using log file %s\n", config->logfile.c_str());
        bool ok = enableLogfile(config->logfile, config->daemon);
        if (!ok) {
            if (config->daemon) {
                warning("Could not open log file, will use syslog instead.\n");
            } else {
                error("Could not open log file.\n");
            }
        }
    }

    warningSilenced(config->silence);
    verboseEnabled(config->verbose);
    logTelegramsEnabled(config->logtelegrams);
    debugEnabled(config->debug);

    debug("(wmbusmeters) version: " VERSION "\n");

    if (config->exitafter != 0) {
        verbose("(config) wmbusmeters will exit after %d seconds\n", config->exitafter);
    }

    if (config->meterfiles) {
        verbose("(config) store meter files in: \"%s\"\n", config->meterfiles_dir.c_str());
    }
    verbose("(config) using device: %s\n", config->device.c_str());
    if (config->device_extra.length() > 0) {
        verbose("(config) with: %s\n", config->device_extra.c_str());
    }
    verbose("(config) number of meters: %d\n", config->meters.size());

    auto manager = createSerialCommunicationManager(config->exitafter);

    onExit(call(manager.get(),stop));

    unique_ptr<WMBus> wmbus;

    auto type_and_device = detectMBusDevice(config->device, manager.get());

    switch (type_and_device.first) {
    case DEVICE_IM871A:
        verbose("(im871a) detected on %s\n", type_and_device.second.c_str());
        wmbus = openIM871A(type_and_device.second, manager.get());
        break;
    case DEVICE_AMB8465:
        verbose("(amb8465) detected on %s\n", type_and_device.second.c_str());
        wmbus = openAMB8465(type_and_device.second, manager.get());
        break;
    case DEVICE_SIMULATOR:
        verbose("(simulator) found %s\n", type_and_device.second.c_str());
        wmbus = openSimulator(type_and_device.second, manager.get());
        break;
    case DEVICE_RTLWMBUS:
    {
        string command = config->device_extra;
        string freq = "868.95M";
        string prefix = "";
        if (isFrequency(command)) {
            freq = command;
            command = "";
        }
        if (config->daemon) {
            prefix = "/usr/bin/";
        }
        if (command == "") {
            command = prefix+"rtl_sdr -f "+freq+" -s 1.6e6 - 2>/dev/null | "+prefix+"rtl_wmbus";
        }
        verbose("(rtlwmbus) using command: %s\n", command.c_str());

        wmbus = openRTLWMBUS(command, manager.get(),
                             [command](){
                                 warning("(rtlwmbus) child process exited! "
                                         "Command was: \"%s\"\n", command.c_str());
                             });
        break;
    }
    case DEVICE_UNKNOWN:
        warning("No wmbus device found! Exiting!\n");
        if (config->daemon) {
            // If starting as a daemon, wait a bit so that systemd have time to catch up.
            sleep(1);
        }
        exit(1);
        break;
    }

    LinkModeCalculationResult lmcr = calculateLinkModes(config, wmbus.get());
    if (lmcr.type != LinkModeCalculationResultType::Success) {
        error("%s\n", lmcr.msg.c_str());
    }

    wmbus->setLinkModes(config->listen_to_link_modes);
    string using_link_modes = wmbus->getLinkModes().hr();

    verbose("(config) listen to link modes: %s\n", using_link_modes.c_str());

    auto output = unique_ptr<Printer>(new Printer(config->json, config->fields,
                                                  config->separator, config->meterfiles, config->meterfiles_dir,
                                                  config->use_logfile, config->logfile,
                                                  config->shells,
                                                  config->meterfiles_action == MeterFileType::Overwrite));
    vector<unique_ptr<Meter>> meters;

    if (config->meters.size() > 0) {
        for (auto &m : config->meters) {
            const char *keymsg = (m.key[0] == 0) ? "not-encrypted" : "encrypted";
            switch (toMeterType(m.type)) {
#define X(mname,link,info,type,cname) \
                case MeterType::type: \
                meters.push_back(create##cname(wmbus.get(), m)); \
                verbose("(wmbusmeters) configured \"%s\" \"" #mname "\" \"%s\" %s\n", \
                m.name.c_str(), m.id.c_str(), keymsg); \
                meters.back()->addConversions(config->conversions); \
                break;
LIST_OF_METERS
#undef X
            case MeterType::UNKNOWN:
                error("No such meter type \"%s\"\n", m.type.c_str());
                break;
            }

            if (config->list_shell_envs) {
                string ignore1, ignore2, ignore3;
                vector<string> envs;
                Telegram t;
                meters.back()->printMeter(&t,
                                          &ignore1,
                                          &ignore2, config->separator,
                                          &ignore3,
                                          &envs);
                printf("Environment variables provided to shell for meter %s:\n", m.type.c_str());
                for (auto &e : envs) {
                    int p = e.find('=');
                    string key = e.substr(0,p);
                    printf("%s\n", key.c_str());
                }
                exit(0);
            }
            meters.back()->onUpdate([&](Telegram*t,Meter* meter) { output->print(t,meter); });
            meters.back()->onUpdate([&](Telegram*t, Meter* meter) { oneshotCheck(config, manager.get(), t, meter, meters); });
        }
    } else {
        notice("No meters configured. Printing id:s of all telegrams heard!\n\n");

        wmbus->onTelegram([](Telegram *t){t->print();});
    }

    if (type_and_device.first == DEVICE_SIMULATOR) {
        wmbus->simulate();
    }

    if (config->daemon) {
        notice("(wmbusmeters) waiting for telegrams\n");
    }

    manager->waitForStop();

    if (config->daemon) {
        notice("(wmbusmeters) shutting down\n");
    }
}

void oneshotCheck(Configuration *config, SerialCommunicationManager *manager, Telegram *t, Meter *meter, vector<unique_ptr<Meter>> &meters)
{
    if (!config->oneshot) return;

    for (auto &m : meters) {
        if (m->numUpdates() == 0) return;
    }
    // All meters have received at least one update! Stop!
    verbose("(main) all meters have received at least one update, stopping.\n");
    manager->stop();
}

void writePid(string pid_file, int pid)
{
    FILE *pidf = fopen(pid_file.c_str(), "w");
    if (!pidf) {
        error("Could not open pid file \"%s\" for writing!\n", pid_file.c_str());
    }
    if (pid > 0) {
        int n = fprintf(pidf, "%d\n", pid);
        if (!n) {
            error("Could not write pid (%d) to file \"%s\"!\n", pid, pid_file.c_str());
        }
        notice("(wmbusmeters) started %s\n", pid_file.c_str());
    }
    fclose(pidf);
    return;
}

void startDaemon(string pid_file)
{
    setlogmask(LOG_UPTO (LOG_INFO));
    openlog("wmbusmetersd", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);

    enableSyslog();

    // Pre check that the pid file can be writte to.
    // Exit before fork, if it fails.
    writePid(pid_file, 0);

    pid_t pid = fork();
    if (pid < 0)
    {
        error("Could not fork.\n");
    }
    if (pid > 0)
    {
        // Success! The parent stores the pid and exits.
        writePid(pid_file, pid);
        return;
    }

    // Change the file mode mask
    umask(0);

    // Create a new SID for the daemon
    pid_t sid = setsid();
    if (sid < 0) {
        // log
        exit(-1);
    }

    if ((chdir("/")) < 0) {
        error("Could not change to root as current working directory.");
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    startUsingConfigFiles("", true);
}

void startUsingConfigFiles(string root, bool is_daemon)
{
    unique_ptr<Configuration> config = loadConfiguration(root);
    config->daemon = is_daemon;

    startUsingCommandline(config.get());
}
