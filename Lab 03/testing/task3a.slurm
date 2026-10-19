#!/bin/bash
#SBATCH --job-name=evcns
#SBATCH --time=00:20:00
#SBATCH --mem=16G
#SBATCH --ntasks=26
#SBATCH --cpus-per-task=7
#SBATCH --partition=defq
#SBATCH --output=task3a.out

module load openmpi/4.1.5-gcc-11.2.0-ux65npg

mpicc -fopenmp -o evcns ../evcns.c

export OMP_NUM_THREADS=7

CSV_FILE="task3a.csv"
echo "grid_size,communication_time" > $CSV_FILE

for size in 2 3 4 5; do
    nrows=$size
    ncols=$size
    
    ntasks=$(( (nrows * ncols) + 1 ))
    
    echo "=================================================="
    echo "Testing Mesh Size: ${nrows}x${ncols} | Total Tasks: ${ntasks}"
    echo "=================================================="
    
    output=$(srun -n $ntasks ./evcns $nrows $ncols)
    echo "$output"
    
    comm_time=$(echo "$output" | grep "Base Station" | sed -E 's/.*Comm: ([0-9.]+)s.*/\1/')
    
    if [ -z "$comm_time" ]; then
        comm_time=$(echo "$output" | grep "Charging Nodes Average" | sed -E 's/.*Comm: ([0-9.]+)s.*/\1/')
    fi
    
    if [ ! -z "$comm_time" ]; then
        echo "${nrows}x${ncols},${comm_time}" >> $CSV_FILE
        echo ">>> Successfully recorded: ${nrows}x${ncols} -> Comm Time: ${comm_time}s"
    else
        echo ">>> Warning: Failed to extract Comm Time for ${nrows}x${ncols}"
        echo "${nrows}x${ncols},NA" >> $CSV_FILE
    fi
    
    echo ""
done

echo "===== All Scaling Tests Completed. Results saved to $CSV_FILE ====="