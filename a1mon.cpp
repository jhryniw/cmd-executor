#include <thread>
#include <chrono>
#include <string>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <map>
#include <vector>
#include <iterator>

#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>

#define CPU_LIMIT_SECONDS 600
#define DEFAULT_INTERVAL 3
#define BUFFER_SIZE 100

using namespace std;

// This is left as a global variable in case we want
// to catch a signal and close the pipe
static FILE* psfile;

// Structure to store process information
struct Process {
 public:
    pid_t pid;        // The pid of the process
    pid_t ppid;       // The pid of the parent process
    std::string cmd;  // The command associated with the process
};

// Prototypes
void set_cpu_limit(int seconds);

// Main loop
int main(int argc, char** argv) {
    set_cpu_limit(CPU_LIMIT_SECONDS);

    pid_t pid, target_pid;
    int interval = DEFAULT_INTERVAL;

    pid = getpid();

    // Parse arguments to get the target_pid and interval (seconds)
    try {
        if (argc == 3) {
            target_pid = (pid_t) stoi(argv[1]);
            interval = stoi(argv[2]);
        } else if (argc == 2) {
            target_pid = (pid_t) stoi(argv[1]);
        } else {
            throw invalid_argument("invalid number of arguments");
        }
    } catch (invalid_argument e) {
        cout << "usage: a1mon targetpid [interval]" << endl;
        exit(1);
    }

    int counter = 0; // Loop counter
    map<pid_t, Process> proc_map; // Maps pids to monitored processes

    while(true) {
        printf("a1mon [counter=%2d, pid=%5d, target_pid=%5d, interval=%2d sec]:\n", counter, pid, target_pid, interval);

        // Run the ps command and open a pipe to get the output
        if (!(psfile = popen("ps -u $USER -o user,pid,ppid,state,start,cmd --sort start", "r"))) {
            cerr << "error: could not open ps" << endl;
            exit(1);
        }

        map<pid_t, Process> new_proc_map;

        // Parse the output of the command. Since the output is sorted, we first accept the target process (the root)
        // then we add all processes with already seen process ids to the process map.
        // This gives us all the
        while (!feof(psfile))
        {
            char psbuffer[BUFFER_SIZE];
            if (fgets(psbuffer, BUFFER_SIZE, psfile) == nullptr) break;

            istringstream iss(string(psbuffer, BUFFER_SIZE));
            vector<string> cmd {
                    istream_iterator<string> {iss},
                    istream_iterator<string> {}
            };

            Process p;

            try {
                // Grab command
                ostringstream cmd_oss;

                // Start from index 5 and drop the last 2 (context variables)
                copy(next(cmd.begin(), 5), prev(cmd.end(), 2),
                     ostream_iterator<string>(cmd_oss, " "));

                // Parse the line into a process object
                p = Process {
                    .pid = (pid_t) stoi(cmd.at(1)),
                    .ppid = (pid_t) stoi(cmd.at(2)),
                    .cmd = cmd_oss.str()
                };
            } catch (invalid_argument e) {
                // Value was non-numeric, skip this entry
                continue;
            } catch (out_of_range e) {
                // Command did not have enough information, skip this entry
                continue;
            }

            // Admits the target process (a1mon) and all decendants
            if (p.pid == target_pid || new_proc_map.find(p.ppid) != new_proc_map.end()) {
                new_proc_map.insert(make_pair(p.pid, p));
            }

            fputs (psbuffer, stdout);
        }

        // Close the pipe
        pclose(psfile);

        // Print a list of monitored processes (does not include the target process)
        cout << "--------------------" << endl;
        cout << "List of monitored processes:" << endl;
        for (const auto& proc : new_proc_map) {
            if(proc.second.pid == target_pid) continue;
            printf("    %5d: %s\n", proc.second.pid, proc.second.cmd.c_str());
        }
        cout << "--------------------" << endl;

        // We determine the target process is terminated if we previously
        // saw the target pid but now we do not
        if (proc_map.find(target_pid) != proc_map.end() && new_proc_map.find(target_pid) == new_proc_map.end()) {
            cout << "a1mon: target appears to have terminated; cleaning up" << endl;
            break;
        }

        proc_map = new_proc_map;

        // Sleep
        this_thread::sleep_for(std::chrono::seconds(interval));
    }

    // Terminate watched processes
    for(const auto& proc : proc_map) {
        if(proc.second.pid == target_pid) continue;

        int status;
        printf("terminating [ %d, %s]\n", proc.first, proc.second.cmd.c_str());
        kill(proc.first, SIGKILL);
        waitpid(proc.first, &status, 0);
    }

    cout << "exiting a1mon" << endl;
}

// Set CPU limit
void set_cpu_limit(int seconds) {
    struct rlimit cpu_limit;

    if (getrlimit(RLIMIT_CPU, &cpu_limit) < 0) {
        std::cerr << "getrlimit error" << std::endl;
        exit(1);
    }

    cpu_limit.rlim_cur = seconds;
    setrlimit(RLIMIT_CPU, &cpu_limit);
}
