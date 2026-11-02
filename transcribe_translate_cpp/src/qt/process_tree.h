// process_tree.h - start a child process so that it and everything it starts
// (ffmpeg, ...) can be killed together. Used by ChildProcess (child_process.h).
#pragma once
#include <QProcess>
#include <vector>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

namespace pk {

// Assign the Windows job during CreateProcess, before any child can escape.
// POSIX tools start in their own process group for the same purpose.
class ProcessTree {
public:
    ProcessTree() {
#ifdef Q_OS_WIN
        job_ = CreateJobObjectW(nullptr, nullptr);
        if (!job_) return;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return;
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        storage_.resize(size);
        attributes_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (!InitializeProcThreadAttributeList(attributes_, 1, 0, &size)) { attributes_ = nullptr; return; }
        ready_ = UpdateProcThreadAttribute(attributes_, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                          &job_, sizeof(job_), nullptr, nullptr);
#endif
    }
    ~ProcessTree() {
#ifdef Q_OS_WIN
        if (attributes_) DeleteProcThreadAttributeList(attributes_);
        if (job_) CloseHandle(job_);
#endif
    }
    ProcessTree(const ProcessTree&) = delete;
    ProcessTree& operator=(const ProcessTree&) = delete;
    bool ready() const {
#ifdef Q_OS_WIN
        return ready_;
#else
        return true;
#endif
    }
    void configure(QProcess* p) {
#ifdef Q_OS_WIN
        p->setCreateProcessArgumentsModifier([this](QProcess::CreateProcessArguments* args) {
            startup_ = {};
            startup_.StartupInfo = *reinterpret_cast<STARTUPINFOW*>(args->startupInfo);
            startup_.StartupInfo.cb = sizeof(startup_);
            startup_.lpAttributeList = attributes_;
            args->flags |= EXTENDED_STARTUPINFO_PRESENT;
            args->startupInfo = reinterpret_cast<Q_STARTUPINFO*>(&startup_);
        });
#else
        p->setChildProcessModifier([] { ::setpgid(0, 0); });
#endif
    }
    void kill(QProcess* p) {
        if (!p) return;
#ifdef Q_OS_WIN
        if (job_) TerminateJobObject(job_, 1);  // also stop descendants after the root has exited
#else
        const qint64 pid = p->processId();
        if (pid > 0) ::kill(-static_cast<pid_t>(pid), SIGKILL);
#endif
        if (p->state() != QProcess::NotRunning) p->kill();
    }
private:
#ifdef Q_OS_WIN
    HANDLE job_ = nullptr;
    std::vector<unsigned char> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST attributes_ = nullptr;
    STARTUPINFOEXW startup_{};
    bool ready_ = false;
#endif
};

}  // namespace pk
