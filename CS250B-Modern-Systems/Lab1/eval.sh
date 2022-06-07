# $1 = thread, $2 = steps, $3 = naive?
if [ "$3" = "n" ]; then
    sudo perf stat -e task-clock,cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses ./obj/stencil $2 $1 init.dat n
else
    sudo perf stat -e task-clock,cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses ./obj/stencil $2 $1 init.dat
fi

echo "Generating render.png..."
echo "\nResult should be 0.9734352827072144 !!"
sudo /usr/bin/python3 ./visualize.py

echo "\nComparing output.dat with true.dat..."
sudo diff --color output.dat true.dat

echo "\nComparing render.png with true.png..."
sudo diff --color render.png true.png
