This is the repository for the Cloud Simulator project for CS 378. To run this project, you can compile the Scheduler with make scheduler and run make simulator to create your simulator executable. Run ./simulator Input.md to see your results, or you can use the script we made with ./run.sh to do all three at once with multiple input files.

For questions, please reach out to us (umerkhan5110@gmail.com, Saood.Usmani@gmail.com, mootaz@austin.utexas.edu) or Ed Discussion.

### Location of Algorithms:
The algorithms each are located in a folder called Algos. Each are stored as a .txt file. They are baseAlgo.txt, bestAlgo.txt, RoundRobin.txt, DVFS.txt. (all of these use Hour except for RoundRobin.txt which uses GentleHour)

## Note:
Scheduler.hpp was changed to add helper functions and variables.
bestAlgo.txt is loaded into Scheduler.cpp
Our best run (from bestAlgo.txt) has been sent seperatly as well as it can be seen in BEST.txt. We are using hour.md for the best test.

### Location of Tests:
Tests are located in the Test_Cases folder. These are all the ones that Dr. Mootaz provided (*I think*)
You can run the test with run.sh. To skip the hour test case, use the -h flag.