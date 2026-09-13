# Documentation

```
docker exec -it <ID> bash
```
```
ssh <credential>@student-caas-headnode.rep.monash.edu
```

Copy required working directory to CAAS
```
scp <target file> <credential>@student-caas-headnode.rep.monash.edu:<filename>
```
Place `scp -r ...` if you want to copy folder.  

Change the order if you want to copy file/folder from CAAS to your local.  

Before compiling c code, make sure you're in target directory in **CAAS** environment.

## Serial Code
Compile c code at **headnode:**
```
gcc task/task1_Serial.c -o compile/task1_Serial -lm
```

For **single N:**
```
srun --nodes=1 --ntasks=1 --cpus-per-task=1 --partition=defq ./compile/task1_Serial <N>
```

## POSIX
Compile c code at **headnode:**
```
gcc task/task1_POSIX.c -o compile/task1_POSIX -lm -lpthread
```

For **single N:** 
```
srun --nodes=1 --ntasks=1 --cpus-per-task=<threads> --partition=defq ./compile/task1_POSIX <N> <threads>
```

## OpenMP
Compile c code at **headnode:**
```
gcc task/task1_OpenMP.c -o compile/task1_OpenMP -lm -fopenmp
```

For **single N:**
```
srun --nodes=1 --ntasks=1 --cpus-per-task=<threads> --partition=defq ./compile/task1_OpenMP <N> <threads>
```

## Open MPI

Load the specific Open MPI software used by the CAAS cluster:
```
module load openmpi/4.1.5-gcc-11.2.0-ux65npg
```

Compile c code at **headnode:**
```
mpicc task/task1_MPI_v1.c -o compile/task1_MPI_v1 -lm
```
```
srun --nodes=1 --ntasks=<processes> --cpus-per-task=1 --partition=defq ./compile/task1_MPI_v1 <N>
```
Notice that CAAS has 14 computation nodes, 16 cores per each node.

## Benchmark Testing for Serial, POSIX, and OpenMP

Copy slurm file to CAAS from docker:
```
scp task1_serial_thread.slurm <credential>@student-caas-headnode.rep.monash.edu:task1_serial_thread.slurm
```
Compile c code at **headnode:**
```
gcc task/task1_Serial.c -o compile/task1_Serial -lm  
gcc task/task1_POSIX.c -o compile/task1_POSIX -lm -lpthread  
gcc task/task1_OpenMP.c -o compile/task1_OpenMP -lm -fopenmp
```
```
sbatch task1_serial_thread.slurm
```

If it shows an error about invisible characters to mark the end of a line. Run
```
sed -i 's/\r$//' task1_serial_thread.slurm
```

Monitor status use `squeue`.

After job completed, run
```
cat <output filename>.out
```

## Benchmark Testing for Open MPI

Copy slurm file to CAAS from docker:
```
scp task1_mpi.slurm <credential>@student-caas-headnode.rep.monash.edu:task1_mpi.slurm
```

Compile c code at **headnode:**
```
mpicc task/task1_MPI_v1.c -o compile/task1_MPI_v1 -lm
```

```
sbatch task1_mpi.slurm
```

## Open MPI (AWS)

Make sure you're accesses AWS ParallelCluster.  

Copy `task1_MPI_v1.c` into EC2
```
nano task1_MPI_v1.c
```

Copy automate testing bash scripts into EC2
```
nano task1_MPI_N.sh
```
```
nano task1_MPI_P.sh
```
```
nano task1_job.sh
```

Running bash scripts
```
sbatch task1_job.sh
```

Using `squeue` and `sinfo` to see the current status.  

Once completed, run
```
cat mpi-<jobID>.out
```