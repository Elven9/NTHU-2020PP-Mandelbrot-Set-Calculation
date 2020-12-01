# Run Speed Up Data(Prepare Text File)
import os
from extractTextFile import OutputExtractor

# 首先從 Input 拿到不要執行的 Phase
inputf = input("Put the Phase that u not want to execute.(seperate by space) ").split(" ")
if not(len(inputf) == 1 and inputf[0] == ""):
    notExecute = list(map(lambda x: int(x), inputf))
else:
    notExecute = []

OUTPUTDIR = "logs"

# Strict26
DATASET = "out.png 10000 -0.19985997516420825 -0.19673850548118335 -1.0994739550641088 -1.1010040371755099 7680 4320"

# SEQUENCIALVIERSION = f"{DATASET['index']}-Sequencial.txt"

def getOutputPath(fileName: str):
    return f"{OUTPUTDIR}/{fileName}"

def getSingleCoreFileName(i: int):
    return f"Strict26-SingleCore-{i}.txt"

def getSingleCoreFileNameFull(i: int):
    return f"Strict26-SingleCore-{i}-Full.txt"

def getMultipleCoreFileName(i: int):
    return f"Strict26-MultiCore-{i}.txt"

def getMultipleCoreFileNameFull(i: int):
    return f"Strict26-MultiCore-{i}-Full.txt"

print("Program Start.......")
if (os.path.isdir(f"./{OUTPUTDIR}")):
    os.system(f"rm -rf ./{OUTPUTDIR}")
    print(f"{OUTPUTDIR} deleted.")
os.mkdir(OUTPUTDIR)
print(f"{OUTPUTDIR} created.")

# Get Single Nodes Performance
# os.system(f"./seq {DATASET['n']} {DATASET['fIn']} {DATASET['fOut']} > {getOutputPath(SEQUENCIALVIERSION)}")
# print("Sequencial Version Completed.")

if 0 not in notExecute:
    print("Single Node, Multiple Threads Start.")
    # 12 Cores perNode
    for i in range(12):
        os.system(f"srun -n1 -c{i+1} ./hw2a-time-full {DATASET} > {getOutputPath(getSingleCoreFileNameFull(i))}")
        print(f"{i+1} ncpus completed.")
    print("----------------------------------------------------------")

if 1 not in notExecute:
    # 4 & 8 Thread 平均分佈
    os.system(f"srun -n1 -c4 ./hw2a-time {DATASET} > {getOutputPath(getSingleCoreFileName(4))}")
    os.system(f"srun -n1 -c8 ./hw2a-time {DATASET} > {getOutputPath(getSingleCoreFileName(8))}")
    print(f"4, 8 average cases per thread completed.")
    print("----------------------------------------------------------")

if 2 not in notExecute:
    # 12 Thread per Process, one Process Per Node
    for i in range(4):
        os.system(f"srun -N{i+1} -n{i+1} -c12 ./hw2b-time {DATASET} > {getOutputPath(getMultipleCoreFileName(i))}")
        print(f"{i+1} multi-cores Full completed.")
    print("----------------------------------------------------------")

if 3 not in notExecute:
    # 12 Thread per Process, one Process Per Node
    for i in range(4):
        os.system(f"srun -N{i+1} -n{i+1} -c12 ./hw2b-time-full {DATASET} > {getOutputPath(getMultipleCoreFileNameFull(i))}")
        print(f"{i+1} multi-cores completed.")
    print("----------------------------------------------------------")

if 4 not in notExecute:
    for i in range(1000):
        os.system(f"srun -n1 -c12 ./hw2a-time-full {DATASET} {i} > {getOutputPath('blocksize-test.txt')}")
        print(f"i={i} completed.")