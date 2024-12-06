#include "Scheduler.hpp"
#include <unordered_map>
#include <climits>
#include <algorithm>  // for std::min_element or std::sort
#include <cfloat>


static unsigned active_machines = 64;

VMType_t Scheduler::GetDefaultVMForCPU(CPUType_t cpu_type) {
    switch (cpu_type) {
        case X86:
            return LINUX;
        case POWER:
            return AIX;
        case ARM:
            return WIN;
        default:
            SimOutput("Scheduler::GetDefaultVMForCPU(): Unknown CPU type " + to_string(cpu_type), 1);
            return VMType_t(-1);
    }
}

void Scheduler::Init() {
    SimOutput("Scheduler::Init(): Initializing scheduler with improved SLA-awareness", 1);
    
    unsigned total_machines = Machine_GetTotal();
    unordered_map<CPUType_t, vector<MachineId_t>> machine_groups;

    for (unsigned i = 0; i < total_machines; i++) {
        MachineInfo_t machine_info = Machine_GetInfo(MachineId_t(i));
        machine_groups[machine_info.cpu].push_back(MachineId_t(i));
    }

    // As before: initialize some machines and put others to S5
    for (auto &group : machine_groups) {
        CPUType_t cpu_type = group.first;
        vector<MachineId_t> &group_machines = group.second;

        unsigned init_vms = std::min((unsigned)(group_machines.size()), (unsigned)(active_machines / (machine_groups.size())));
        for (unsigned i = 0; i < init_vms; i++) {
            MachineId_t machine_id = group_machines[i];
            Machine_SetState(machine_id, S0);

            VMType_t default_vm_type = GetDefaultVMForCPU(cpu_type);
            VMId_t new_vm = VM_Create(default_vm_type, cpu_type);
            VM_Attach(new_vm, machine_id);

            vms.push_back(new_vm);
            this->machines.push_back(machine_id);
        }

        for (unsigned i = init_vms; i < group_machines.size(); i++) {
            Machine_SetState(group_machines[i], S5);
        }
    }

    SimOutput("Scheduler::Init(): Completed initialization with SLA considerations", 1);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // Update your data structure. The VM now can receive new tasks.
    // Possibly record that migration is done and VM can again host new tasks.
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    TaskInfo_t task_info = GetTaskInfo(task_id);
    SimOutput("Scheduler::NewTask(): Handling new task " + to_string(task_id), 3);

    // Record active task info
    ActiveTask at;
    at.task_id = task_id;
    at.sla = task_info.required_sla;
    at.deadline = task_info.target_completion; // from arrival + allowed slack by SLA
    at.vm_id = VMId_t(-1);

    // Assign the task:
    VMId_t assigned_vm = AssignTaskToBestVM(task_id);
    if (assigned_vm == VMId_t(-1)) {
        // If we failed to find a good VM, try activating a new machine
        
for (MachineId_t machine_id : machines) {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    if (machine_info.s_state == S0 &&
        machine_info.cpu == task_info.required_cpu &&
        (machine_info.memory_size - machine_info.memory_used) >= (task_info.required_memory + VM_MEMORY_OVERHEAD)) {

        // Create a VM of the exact required type:
        VMId_t new_vm = VM_Create(task_info.required_vm, task_info.required_cpu);
        VM_Attach(new_vm, machine_id);
        VM_AddTask(new_vm, task_id, task_info.priority);

        vms.push_back(new_vm);
        active_tasks.push_back({task_id, task_info.required_sla, task_info.target_completion, new_vm});
        SimOutput("Scheduler::NewTask(): Created exact required VM " + to_string(new_vm) + 
                  " on machine " + to_string(machine_id) + " for task " + to_string(task_id), 2);
        return;
    }
}
    } else {
        at.vm_id = assigned_vm;
        active_tasks.push_back(at);
        SimOutput("Scheduler::NewTask(): Task " + to_string(task_id) + " assigned to VM " + to_string(assigned_vm), 2);
    }
}

// NEW LOGIC: AssignTaskToBestVM
VMId_t Scheduler::AssignTaskToBestVM(TaskId_t task_id) {
    TaskInfo_t task_info = GetTaskInfo(task_id);

    // We want to pick a VM that:
    // 1. Matches CPU and VM type
    // 2. Has enough memory
    // 3. Minimizes expected completion time (e.g., pick a machine that is running in P0 or has fewer tasks)
    // 4. If GPU-capable and task is GPU-capable, prefer that machine

    VMId_t best_vm = VMId_t(-1);
    double best_score = DBL_MAX;

    for (VMId_t vm : vms) {
        VMInfo_t vm_info = VM_GetInfo(vm);
        MachineInfo_t mach_info = Machine_GetInfo(vm_info.machine_id);
        
        if (mach_info.s_state != S0)
            continue; // must be active machine

        if (mach_info.cpu != task_info.required_cpu)
            continue; // must match CPU

        if (vm_info.vm_type != task_info.required_vm)
            continue; // must match VM type

        unsigned needed_mem = task_info.required_memory + VM_MEMORY_OVERHEAD;
        if (mach_info.memory_used + needed_mem > mach_info.memory_size)
            continue; // memory fits?

        // Heuristic to score this VM:
        // factor in load, performance state (p_state), GPU, and time till deadline
        double load = CalculateMachineLoad(mach_info.machine_id);
        // Lower load is better, but we also consider GPU and P-state
        // If GPU present and task can use GPU, huge speed boost:
        double perf_factor = 1.0;
        if (task_info.gpu_capable && mach_info.gpus) {
            perf_factor = 0.5; // just a heuristic: GPU speeds up tasks
        }

        // Adjust for P-state (lower P-state = P3 means slower)
        // performance[P0] would be the highest MIPS, we can scale inversely:
        // For example: 
        unsigned p0_mips = mach_info.performance[0];
        unsigned current_mips = mach_info.performance[mach_info.p_state];
        double speed_ratio = (double)p0_mips / (double)current_mips;
        
        // Combine into a simple score
        double score = load * speed_ratio * perf_factor;

        if (score < best_score) {
            best_score = score;
            best_vm = vm;
        }
    }

    if (best_vm != VMId_t(-1)) {
        // Assign task to this VM
        TaskInfo_t task_info2 = GetTaskInfo(task_id);
        VM_AddTask(best_vm, task_id, task_info2.priority);
    }
    return best_vm;
}

double Scheduler::CalculateMachineLoad(MachineId_t machine_id) {
    // Simple load calculation: (active tasks)
    MachineInfo_t info = Machine_GetInfo(machine_id);
    // Could incorporate CPU frequency or instructions pending
    return (double)info.active_tasks / (double)info.num_cpus;
}

CPUPerformance_t Scheduler::GetPStateForLoad(double load) {
    // Simple heuristic:
    // if load > 0.8: P0
    // if load > 0.5: P1
    // if load > 0.2: P2
    // else: P3
    if (load > 0.8) return P0;
    else if (load > 0.5) return P1;
    else if (load > 0.2) return P2;
    else return P3;
}

void Scheduler::PeriodicCheck(Time_t now) {
    SimOutput("Scheduler::PeriodicCheck(): Checking deadlines and adjusting states", 3);
    CheckDeadlinesAndRebalance(now);

    // Adjust P-states of machines based on load
    for (auto m_id : machines) {
        double load = CalculateMachineLoad(m_id);
        CPUPerformance_t desired_p = GetPStateForLoad(load);
        Machine_SetCorePerformance(m_id, 0, desired_p); // sets all cores to this P-state
    }
}

void Scheduler::CheckDeadlinesAndRebalance(Time_t now) {
    // Find tasks close to missing deadline and take action
    for (auto &task : active_tasks) {
        if (IsTaskCompleted(task.task_id))
            continue; // Skip completed tasks

        TaskInfo_t info = GetTaskInfo(task.task_id);
        if (now > info.target_completion) {
            // Task already late - might need a big intervention, but if it's late, we can’t do much except learn from it.
            continue;
        }

        Time_t remaining_time = info.target_completion - now;
        // If the estimated completion (based on instructions and MIPS) won't meet deadline, consider migrating:
        if (info.remaining_instructions > 0) {
            // Rough estimate of finish time:
            VMInfo_t vm_info = VM_GetInfo(task.vm_id);
            MachineInfo_t mach_info = Machine_GetInfo(vm_info.machine_id);
            unsigned current_mips = mach_info.performance[mach_info.p_state];
            // Time to finish = instructions_remaining / (mips * 1e6)
            double time_to_finish = (double)info.remaining_instructions / ((double)current_mips * 1e6);
            Time_t time_to_finish_us = (Time_t)(time_to_finish * 1000000);

            if (time_to_finish_us > remaining_time / 2) {
                // Try to boost machine performance or migrate this VM to a faster machine:
                BoostMachinePerformance(mach_info.machine_id);
                // Potentially migrate to a better machine if available:
                // (For now we just boost; migration logic would be similar: find a better machine and call VM_Migrate)
            }
        }
    }
}

void Scheduler::HandleSLAWarning(Time_t now, TaskId_t task_id) {
    // On SLA Warning, immediately try boosting performance or migrating:
    TaskInfo_t info = GetTaskInfo(task_id);
    // Find VM and machine:
    for (auto &at : active_tasks) {
        if (at.task_id == task_id) {
            VMInfo_t vm_info = VM_GetInfo(at.vm_id);
            MachineInfo_t mach_info = Machine_GetInfo(vm_info.machine_id);
            BoostMachinePerformance(mach_info.machine_id);
            // Optionally consider migration to a GPU machine if beneficial and not currently on one
            return;
        }
    }
}

void Scheduler::Shutdown(Time_t time) {
    for(auto & vm: vms) {
        // Shutdown all VMs:
        VM_Shutdown(vm);
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now), 4);
    // Remove from active tasks
    active_tasks.erase(std::remove_if(active_tasks.begin(), active_tasks.end(), [task_id](const ActiveTask &t){
        return t.task_id == task_id;
    }), active_tasks.end());
}

// Public interface

static Scheduler Scheduler;

void InitScheduler() {
    SimOutput("InitScheduler(): Initializing scheduler", 4);
    Scheduler.Init();
}

void HandleNewTask(Time_t time, TaskId_t task_id) {
    SimOutput("HandleNewTask(): Received new task " + to_string(task_id) + " at time " + to_string(time), 4);
    Scheduler.NewTask(time, task_id);
}

void HandleTaskCompletion(Time_t time, TaskId_t task_id) {
    SimOutput("HandleTaskCompletion(): Task " + to_string(task_id) + " completed at time " + to_string(time), 4);
    Scheduler.TaskComplete(time, task_id);
}

void MemoryWarning(Time_t time, MachineId_t machine_id) {
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id) + " was detected at time " + to_string(time), 0);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id) + " was completed at time " + to_string(time), 4);
    Scheduler.MigrationComplete(time, vm_id);
}

void SchedulerCheck(Time_t time) {
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
}

void SimulationComplete(Time_t time) {
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;
    cout << "Total Energy " << Machine_GetClusterEnergy() << " KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);

    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
    Scheduler.HandleSLAWarning(time, task_id);
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    SimOutput("StateChangeComplete(): State change for machine " + to_string(machine_id) + " completed at time " + to_string(time), 2);

    // If this callback indicates a machine is now S0, you can now safely proceed with VM shutdown if you were waiting.
    // For example, if you had a deferred shutdown list, now would be the time to perform it.
}

void Scheduler::BoostMachinePerformance(MachineId_t machine_id) {
    // Set machine to highest performance P-state:
    Machine_SetCorePerformance(machine_id, 0, P0);
}