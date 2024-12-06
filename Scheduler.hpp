//
//  Scheduler.hpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#ifndef Scheduler_hpp
#define Scheduler_hpp


#include <vector>
#include "Interfaces.h"

class Scheduler {
public:
    Scheduler() {}

    void Init();
    void MigrationComplete(Time_t time, VMId_t vm_id);
    void NewTask(Time_t now, TaskId_t task_id);
    void PeriodicCheck(Time_t now);
    void Shutdown(Time_t now);
    void TaskComplete(Time_t now, TaskId_t task_id);
    VMId_t FindOrCreateVM(MachineId_t machine_id, VMType_t vm_type, CPUType_t cpu_type);
    VMType_t GetDefaultVMForCPU(CPUType_t cpu_type);
    double CalculateMachineLoad(MachineId_t machine_id);
    CPUPerformance_t GetPStateForLoad(double load);

    // New helper methods:
    VMId_t AssignTaskToBestVM(TaskId_t task_id);
    void HandleSLAWarning(Time_t now, TaskId_t task_id);
    void BoostMachinePerformance(MachineId_t machine_id);
    void CheckDeadlinesAndRebalance(Time_t now);

private:
    vector<VMId_t> vms;
    vector<MachineId_t> machines;

    // Track tasks by their SLAs and deadlines:
    // This could be improved with more advanced data structures (priority queues keyed by deadline, etc.)
    struct ActiveTask {
        TaskId_t task_id;
        SLAType_t sla;
        Time_t deadline;    // target_completion from TaskInfo
        VMId_t vm_id;
    };

    vector<ActiveTask> active_tasks; 
};




#endif /* Scheduler_hpp */