# Documentation

```
docker exec -it <ID> bash
```

Make sure you're in **Lab 02** directory.  

## Serial Code
Compile c code
```
gcc task/task1_Serial.c -o compile/task1_Serial -lm
```

For **single N:**
```
echo 10000000 > input/single_Serial.txt
```
```
./compile/task1_Serial input/single_Serial.txt
```

For **multiple N:**
```
bash script/task1_Serial.sh
```
This will automatically generate csv file to record the overall time and computational time for N from 10,000,000 to 40,000,000, increases by 1,000,000.  
The csv file was automatically places under time folder.

## POSIX
Compile c code
```
gcc task/task1_POSIX.c -o compile/task1_POSIX -lm -lpthread
```

For **single N:** 
```
./compile/task1_POSIX <N> <# of threads>
```

Replace `<N>` and `<# of threads>`.

For **multiple N:** 
```
bash script/task1_POSIX_N.sh <# of threads>
```
This will automatically generate csv file to record the overall time and computational time for N from 10,000,000 to 40,000,000, increases by 1,000,000, with `<# of threads>` threads (replace this in command line).  

For **multiple threads** with fixed N = 10,000,000:
```
bash script/task1_POSIX_T.sh <a sequence of # of threads>
```
Example: `bash script/task1_POSIX_T.sh 1 2 4 8 16 32`

## OpenMP
Compile c code
```
gcc task/task1_OpenMP.c -o compile/task1_OpenMP -lm -fopenmp
```

For **single N:**
```
./compile/task1_OpenMP <N> <# of threads>
```

For **multiple N:** 
```
bash script/task1_OpenMP_N.sh <# of threads>
```
This will automatically generate csv file to record the overall time and computational time for N from 10,000,000 to 40,000,000, increases by 1,000,000, with `<# of threads>` threads (replace this in command line).  

For **multiple threads** with fixed N = 10,000,000:
```
bash script/task1_OpenMP_T.sh <a sequence of # of threads>
```
Example: `bash script/task1_OpenMP_T.sh 1 2 4 8 16 32`