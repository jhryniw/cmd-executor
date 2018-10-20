#include <iostream>
#include <string>
#include <sstream>
#include <iterator>
#include <cstdio>
#include <vector>

#include <signal.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <sys/wait.h>

#define CPU_LIMIT_SECONDS 600
#define MAX_JOBS 32

using namespace std;

const char* TAB = "   ";
const char* YELLOW = "\033[1;33m";
const char* DEFAULT = "\033[0m";

// Structure to store job information
struct Job {
    int index;          // The job id
    pid_t hproc;        // pid of the head process for the job
    vector<string> cmd; // Command
    bool terminated;    // Terminated state of the job. Easier than polling this information from the system
};

// Prototypes
void set_cpu_limit(int seconds);
void print_times(const tms& tms_start, const tms& tms_end, clock_t real_start, clock_t real_end);
void run_cmd(const vector<string>& cmd);
Job* get_job(vector<Job>& jobs, std::string str_jobno);
int terminate(Job& job);

int main(void) {
    set_cpu_limit(CPU_LIMIT_SECONDS);

    vector<Job> jobs;

    // "Start" the clock
    tms start_time, end_time;
    clock_t real_start = times(&start_time);

    // Get this process' pid
    pid_t ppid = getpid();

    while (true) {
        string str_cmd;
        cout << YELLOW << "a1jobs[" << DEFAULT << ppid << YELLOW << "]: " << DEFAULT;
        getline(cin, str_cmd);

        istringstream iss(str_cmd);
        vector < string > cmd {
            istream_iterator < string > {iss},
            istream_iterator < string > {}
        };

        if (cmd.size() < 1) {
            continue;
        }

        if (cmd[0] == "list") {
            for (const Job& j : jobs) {
                if (!j.terminated) {
                    ostringstream cmd_oss;
                    copy(j.cmd.begin(), j.cmd.end(),
                              ostream_iterator<string>(cmd_oss, " "));
                    string cmd_str = cmd_oss.str();

                    printf("%2d: (pid = %5d, cmd = %s)\n", j.index, j.hproc, cmd_str.c_str());
                }
            }
        } else if (cmd[0] == "run") {
            // Remove the "run" element of the command
            cmd.erase(cmd.begin());

            if (jobs.size() < MAX_JOBS) {
                pid_t pid = fork();

                if (pid < 0) {
                    cerr << "fork error" << endl;
                } else if (pid > 0) { /* parent */
                    // Create and register the new job
                    Job new_job = {
                            .index = static_cast<int>(jobs.size()),
                            .hproc = pid,
                            .cmd = cmd,
                            .terminated = false,
                    };

                    jobs.emplace_back(move(new_job)); // Yeah, this insertion is quite optimal :)
                } else { /* child */
                    // Run the command
                    run_cmd(cmd);
                    exit(0);
                }
            } else {
                cout << "error: could not admit job -- the maximum "
                     << MAX_JOBS << " jobs are already registered!" << endl;
            }
        } else if (cmd[0] == "suspend") {
            // Sends SIGSTOP to a job by jobno
            if (cmd.size() != 2) {
                cout << TAB << "usage: resume <jobno>" << endl;
                continue;
            }

            if (Job* job = get_job(jobs, cmd[1])) {
                kill(job->hproc, SIGSTOP);
                cout << TAB << "suspended " << job->hproc << endl;
            }
        } else if (cmd[0] == "resume") {
            // Sends SIGCONT to a job by jobno
            if (cmd.size() != 2) {
                cout << TAB << "usage: resume <jobno>" << endl;
                continue;
            }

            if (Job* job = get_job(jobs, cmd[1])) {
                kill(job->hproc, SIGCONT);
                cout << TAB << "resumed " << job->hproc << endl;
            }
        } else if (cmd[0] == "terminate") {
            // Sends SIGKILL to a job by jobno
            if (cmd.size() != 2) {
                cout << TAB << "usage: terminate <jobno>" << endl;
                continue;
            }

            if (Job* job = get_job(jobs, cmd[1])) {
                terminate(*job);
            }
        } else if (cmd[0] == "exit") {
            // Cleanup all non-terminated jobs before exiting
            for (Job& job : jobs) {
                terminate(job);
            }
            break;
        } else if (cmd[0] == "quit") {
            break;
        } else {
            cout << TAB << "Invalid command \'" << cmd[0] << "\'" << endl;
        }
    }

    // Print final time information
    clock_t real_end = times(&end_time);
    print_times(start_time, end_time, real_start, real_end);

    return 0;
}

/**
 * Prints the total real time, as well as total cpu time elapsed for the system
 * and children processes between the start and end times supplied
 */
void print_times(const tms& tms_start, const tms& tms_end, clock_t real_start, clock_t real_end) {
    static long clk_tick = sysconf(_SC_CLK_TCK);

    cout << endl;
    printf("real: %.2f\n", static_cast<float>(real_end - real_start) / clk_tick);
    printf("user: %.2f\n", static_cast<float>(tms_end.tms_utime - tms_start.tms_utime) / clk_tick);
    printf("sys: %.2f\n",  static_cast<float>(tms_end.tms_stime - tms_start.tms_stime) / clk_tick);
    printf("child user: %.2f\n", static_cast<float>(tms_end.tms_cutime - tms_start.tms_cutime) / clk_tick);
    printf("child sys: %.2f\n", static_cast<float>(tms_end.tms_cstime - tms_start.tms_cstime) / clk_tick);

    cout.flush();
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

// Calls a command with up to 4 arguments
void run_cmd(const vector<string>& cmd) {
    switch(cmd.size()) {
        case 0:
            cout << TAB << "error: no command entered" << endl;
            break;
        case 1:
            execlp(cmd[0].c_str(), cmd[0].c_str(), (char *) nullptr);
            break;
        case 2:
            execlp(cmd[0].c_str(), cmd[0].c_str(), cmd[1].c_str(), (char *) nullptr);
            break;
        case 3:
            execlp(cmd[0].c_str(), cmd[0].c_str(), cmd[1].c_str(), cmd[2].c_str(), (char *) nullptr);
            break;
        case 4:
            execlp(cmd[0].c_str(), cmd[0].c_str(), cmd[1].c_str(), cmd[2].c_str(), cmd[3].c_str(), (char *) nullptr);
            break;
        case 5:
            execlp(cmd[0].c_str(), cmd[0].c_str(), cmd[1].c_str(), cmd[2].c_str(), cmd[3].c_str(), cmd[4].c_str(), (char *) nullptr);
            break;
        default:
            cout << TAB << "Too many arguments -- only 4 arguments allowed." << endl;
            break;
    }
}

/**
 * Gets a job instance by job number with proper error handling
 * @param jobs the list of jobs
 * @param str_jobno a string number
 * @return a job with index jobno, or nullptr if it isn't found
 */
Job* get_job(vector<Job>& jobs, std::string str_jobno) {
    int jobno;

    try {
        jobno = stoi(str_jobno);
        return &jobs.at(jobno);
    } catch (invalid_argument e) {
        cout << TAB << "error: invalid job number " << str_jobno << endl;
    } catch (out_of_range e) {
        cout << TAB << "error: job " << jobno << " does not exist" << endl;
    }

    return nullptr;
}

/**
 * Sends a SIGKILL signal to a job's process
 * @param job the job to kill
 * @return the status of the terminated job
 */
int terminate(Job& job) {
    int status = 0;

    if (!job.terminated) {
        kill(job.hproc, SIGKILL);
        job.terminated = true;
        cout << TAB << "terminated " << job.hproc << endl;
        waitpid(job.hproc, &status, 0);
    }

    return status;
}
