#!/bin/bash
#SBATCH --job-name=evcns
#SBATCH --time=00:20:00
#SBATCH --mem=16G
#SBATCH --ntasks=21
#SBATCH --cpus-per-task=7
#SBATCH --partition=defq
#SBATCH --output=task3b.out

module load openmpi/4.1.5-gcc-11.2.0-ux65npg

mpicc -fopenmp -o evcns ../evcns.c -lm

export OMP_NUM_THREADS=7

CSV_FILE="task3b.csv"
echo "Types,Grid Size,Communication Time" > $CSV_FILE

for size in 4 8 12 16 20 24; do
    nrows=1
    ncols=$size
    
    ntasks=$(( (nrows * ncols) + 1 ))
    
    echo "=================================================="
    echo "Testing Line Size: ${nrows}x${ncols} | Total Tasks: ${ntasks}"
    echo "=================================================="
    
    output=$(srun -n $ntasks ./evcns $nrows $ncols)
    echo "$output"
    
    bs_comm=$(echo "$output" | grep "\[Base Station\] Overall" | sed -E 's/.*Comm: ([0-9.]+)s.*/\1/')
    
    node_comm=$(echo "$output" | grep "\[Charging Nodes Average\]" | sed -E 's/.*Comm: ([0-9.]+)s.*/\1/')
    
    if [ ! -z "$bs_comm" ] && [ ! -z "$node_comm" ]; then
        echo "Base Station,${nrows}x${ncols},${bs_comm}" >> $CSV_FILE
        echo "Avg Charging Nodes,${nrows}x${ncols},${node_comm}" >> $CSV_FILE
        echo ">>> Successfully recorded: ${nrows}x${ncols}"
        echo "    -> Base Station Comm Time: ${bs_comm}s"
        echo "    -> Avg Nodes Comm Time: ${node_comm}s"
    else
        echo ">>> Warning: Failed to extract Comm Time for ${nrows}x${ncols}"
    fi
    
    echo ""
done

echo "===== All Line Topology Tests Completed. Results saved to $CSV_FILE ====="