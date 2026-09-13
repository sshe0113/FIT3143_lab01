# Documentation

To access **Docker:**
```
docker exec -it <ID> bash
```
To access **CAAS:** (Must connect Monash VPN)
```
ssh <credential>@student-caas-headnode.rep.monash.edu
```
To access **AWS:**
```
C:\Users\<username>\apc-ve\Scripts\activate
```
```
pcluster ssh --cluster-name mycluster --region ap-southeast-5 -i <keyname>.pem
```
Change `ap-southeast-5` to your region.

Copy required working directory to **CAAS:**
```
scp <target file> <credential>@student-caas-headnode.rep.monash.edu:<filename>
```
Copy required working directory to **AWS:**
```
scp -i <keyname>.pem <targat file> <username>@<IP address>:<filename>
```
Place `scp -r ...` if you want to copy folder.  

Change the order if you want to copy file/folder from CAAS to your local.  

Before compiling c code, make sure you're in target directory in **CAAS** environment.

## Serial Code
Compile c code at **headnode:**
```
gcc task/task1_Serial.c -o compile/task1_Serial -lm
```

For **single N on CAAS:**
```
srun --nodes=1 --ntasks=1 --cpus-per-task=1 --partition=defq ./compile/task1_Serial <N>
```

For **single N on AWS:**
```
./compile/task1_Serial <N>
```

## POSIX
Compile c code at **headnode:**
```
gcc task/task1_POSIX.c -o compile/task1_POSIX -lm -lpthread
```

For **single N on CAAS:**
```
srun --nodes=1 --ntasks=1 --cpus-per-task=<threads> --partition=defq ./compile/task1_POSIX <N> <threads>
```

For **single N on AWS:**
```
./compile/task1_POSIX <N> <threads>
```

## OpenMP
Compile c code at **headnode:**
```
gcc task/task1_OpenMP.c -o compile/task1_OpenMP -lm -fopenmp
```

For **single N on CAAS:**
```
srun --nodes=1 --ntasks=1 --cpus-per-task=<threads> --partition=defq ./compile/task1_OpenMP <N> <threads>
```

For **single N on AWS:**
```
export OMP_NUM_THREADS=<threads>
./compile/task1_OpenMP <N>
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

For **single N on CAAS:**
```
srun --nodes=1 --ntasks=<processes> --cpus-per-task=1 --partition=defq ./compile/task1_MPI_v1 <N>
```
Notice that CAAS has 14 computation nodes, 16 cores per each node.

For **single N on AWS:**
```
mpirun -np <processes> ./compile/task1_MPI_v1 <N>
```
Notice that AWS EC2 has 8 computation nodes, 2 vCPUs per each node.

## Benchmark Testing for Serial, POSIX, and OpenMP

Copy slurm file to CAAS from docker:
```
scp task1_serial_thread.slurm <credential>@student-caas-headnode.rep.monash.edu:task1_serial_thread.slurm
```

Copy slurm file to AWS from docker:
```
scp -i <keyname>.pem task1_serial_thread.slurm <username>@<IP address>:task1_serial_thread.slurm
```
Use `whoami` and `curl checkip.amazonaws.com` to check username and IP address.

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

Copy slurm file to AWS from docker:
```
scp -i <keyname>.pem task1_mpi.slurm <username>@<IP address>:task1_mpi.slurm
```
Use `whoami` and `curl checkip.amazonaws.com` to check username and IP address.

Compile c code at **headnode:**
```
mpicc task/task1_MPI_v1.c -o compile/task1_MPI_v1 -lm
```

```
sbatch task1_mpi.slurm
```